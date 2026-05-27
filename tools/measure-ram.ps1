#Requires -Version 5.1
<#
.SYNOPSIS
    VKey idle-RAM benchmark - auto-launches each version, idles, samples, kills, compares.

.DESCRIPTION
    Sequence per binary:
      1. Pre-flight kill all VKey/NexusKey processes (only the one we launch is measured).
      2. Start the binary, wait WarmupSeconds for init + tray icon.
      3. Sample Working Set + Private Working Set + Commit + Threads + Handles every
         IntervalSeconds, for DurationMinutes total. WALK AWAY DURING THIS WINDOW.
      4. Kill the binary, wait CooldownSeconds.
      5. Repeat for the next binary.

    After both runs, prints side-by-side comparison and a verdict on whether Windows
    actually trimmed the Private Working Set over the idle window.

.PARAMETER BenchDir
    Folder containing the binaries. Default C:\Benchmark.
.PARAMETER Binaries
    Array of exe filenames inside BenchDir. Default NexusKey.exe, VKey.exe.
.PARAMETER Labels
    Array of labels for CSV/output. Default v2.1.24, v3.0.0.
.PARAMETER DurationMinutes
    Idle window per binary. Default 5.
.PARAMETER IntervalSeconds
    Sample cadence. Default 10.
.PARAMETER WarmupSeconds
    Wait after launch before first sample. Default 8.
.PARAMETER CooldownSeconds
    Wait after kill before next launch. Default 5.
.PARAMETER SkipPreflightKill
    Skip killing stray VKey/NexusKey processes.

.NOTES
    Pre-flight kills any running VKey / NexusKey processes (including any daily-driver
    instance). Save your work first.
#>

param(
    [string]$BenchDir         = "C:\Benchmark",
    [string[]]$Binaries       = @("NexusKey.exe","VKey.exe"),
    [string[]]$Labels         = @("v2.1.24","v3.0.0"),
    [int]$DurationMinutes     = 5,
    [int]$IntervalSeconds     = 10,
    [int]$WarmupSeconds       = 8,
    [int]$CooldownSeconds     = 5,
    [switch]$SkipPreflightKill
)

$ErrorActionPreference = "Stop"

if ($Binaries.Count -ne $Labels.Count) {
    Write-Error "Binaries and Labels arrays must be the same length."
    exit 1
}

# --- Output dir ------------------------------------------------------------

$OutDir = Join-Path $BenchDir "ram-measurements"
if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

# --- Helpers ---------------------------------------------------------------

function Format-MB([double]$bytes) {
    return [math]::Round($bytes / 1MB, 2)
}

function Get-PerfCounterPath([string]$procName) {
    $candidates = @(
        "\Process($procName)\Working Set - Private",
        "\Process($procName)\Working Set Private"
    )
    foreach ($c in $candidates) {
        try {
            Get-Counter -Counter $c -SampleInterval 1 -MaxSamples 1 -ErrorAction Stop | Out-Null
            return $c
        } catch {
            continue
        }
    }
    return $null
}

function Stop-AllInstancesByName {
    param([string[]]$Names)
    foreach ($n in $Names) {
        $running = Get-Process -Name $n -ErrorAction SilentlyContinue
        if ($running) {
            Write-Host ("  Killing {0} stray '{1}' process(es)..." -f $running.Count, $n) -ForegroundColor DarkGray
            Stop-Process -Name $n -Force -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Seconds 2
}

function Summarize-Csv([string]$path) {
    if (-not (Test-Path $path)) { throw "CSV not found: $path" }
    $rows = Import-Csv $path
    if ($rows.Count -eq 0) { throw "CSV is empty: $path" }
    $first = $rows[0]
    $last  = $rows[-1]
    $ws  = $rows | ForEach-Object { [double]$_.WorkingSetMB }
    $pws = $rows | ForEach-Object { [double]$_.PrivateWSMB }
    [PSCustomObject]@{
        Label              = $first.Label
        Samples            = $rows.Count
        WS_Start_MB        = [double]$first.WorkingSetMB
        WS_End_MB          = [double]$last.WorkingSetMB
        WS_Min_MB          = ($ws | Measure-Object -Minimum).Minimum
        WS_Max_MB          = ($ws | Measure-Object -Maximum).Maximum
        WS_Delta_MB        = [math]::Round([double]$last.WorkingSetMB - [double]$first.WorkingSetMB, 2)
        PrivateWS_Start_MB = [double]$first.PrivateWSMB
        PrivateWS_End_MB   = [double]$last.PrivateWSMB
        PrivateWS_Min_MB   = ($pws | Measure-Object -Minimum).Minimum
        PrivateWS_Max_MB   = ($pws | Measure-Object -Maximum).Maximum
        PrivateWS_Delta_MB = [math]::Round([double]$last.PrivateWSMB - [double]$first.PrivateWSMB, 2)
        Commit_Start_MB    = [double]$first.CommitMB
        Commit_End_MB      = [double]$last.CommitMB
        Threads_Start      = [int]$first.Threads
        Threads_End        = [int]$last.Threads
    }
}

function Run-Measurement {
    param(
        [string]$ExePath,
        [string]$Label,
        [string]$CsvPath,
        [int]$DurationMinutes,
        [int]$IntervalSeconds,
        [int]$WarmupSeconds
    )

    $exeName = [System.IO.Path]::GetFileNameWithoutExtension($ExePath)

    Write-Host ""
    Write-Host ("=== Run: {0} ({1}) ===" -f $Label, $ExePath) -ForegroundColor Cyan

    Write-Host "  Launching..."
    $proc = Start-Process -FilePath $ExePath -WorkingDirectory (Split-Path -Parent $ExePath) -PassThru
    if (-not $proc) {
        throw "Start-Process returned null for $ExePath"
    }
    Write-Host ("  Started PID {0}. Warming up {1} s..." -f $proc.Id, $WarmupSeconds)
    Start-Sleep -Seconds $WarmupSeconds

    $alive = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    if (-not $alive) {
        throw "$Label ($exeName.exe) died during warmup. Check the binary."
    }

    $instances = @(Get-Process -Name $exeName -ErrorAction SilentlyContinue)
    if ($instances.Count -gt 1) {
        throw ("Multiple '{0}' processes detected ({1}). Pre-flight kill failed?" -f $exeName, $instances.Count)
    }

    $counter = Get-PerfCounterPath -procName $exeName
    if (-not $counter) {
        Write-Warning "  Private Working Set counter unavailable. PrivateWSMB column will be 0."
    } else {
        Write-Host ("  Counter: {0}" -f $counter)
    }

    $totalSeconds = $DurationMinutes * 60
    $samples = [math]::Floor($totalSeconds / $IntervalSeconds)
    Write-Host ("  Sampling {0} times every {1} s (~{2} min)..." -f $samples, $IntervalSeconds, $DurationMinutes) -ForegroundColor Yellow
    Write-Host "  *** WALK AWAY. No typing, no focus changes, no clicks. ***" -ForegroundColor Yellow

    "Label,Timestamp,ElapsedSec,WorkingSetMB,PrivateWSMB,CommitMB,VirtualMB,Threads,Handles" |
        Out-File -FilePath $CsvPath -Encoding UTF8

    $start = Get-Date
    for ($i = 0; $i -lt $samples; $i++) {
        $elapsed = ((Get-Date) - $start).TotalSeconds

        $p = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
        if (-not $p) {
            Write-Warning ("  Process died at {0}s. Stopping early." -f [math]::Round($elapsed,1))
            break
        }

        $privWS = 0
        if ($counter) {
            try {
                $sample = Get-Counter -Counter $counter -SampleInterval 1 -MaxSamples 1 -ErrorAction Stop
                $privWS = $sample.CounterSamples[0].CookedValue
            } catch {
                $privWS = 0
            }
        }

        $ts          = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
        $wsMB        = Format-MB $p.WorkingSet64
        $privWSMB    = [math]::Round($privWS / 1MB, 2)
        $commitMB    = Format-MB $p.PrivateMemorySize64
        $virtMB      = Format-MB $p.VirtualMemorySize64
        $threads     = $p.Threads.Count
        $handles     = $p.HandleCount
        $elapsedR    = [math]::Round($elapsed, 1)

        "$Label,$ts,$elapsedR,$wsMB,$privWSMB,$commitMB,$virtMB,$threads,$handles" |
            Out-File -FilePath $CsvPath -Append -Encoding UTF8

        $line = "    [{0,5}s]  WS={1,7:N2}  PrivWS={2,7:N2}  Commit={3,7:N2}  T={4,3}  H={5,5}" -f $elapsedR, $wsMB, $privWSMB, $commitMB, $threads, $handles
        Write-Host $line

        Start-Sleep -Seconds $IntervalSeconds
    }

    Write-Host ("  Killing PID {0}..." -f $proc.Id)
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
}

# --- Main orchestration ----------------------------------------------------

Write-Host ""
Write-Host "VKey idle-RAM benchmark" -ForegroundColor Cyan
Write-Host ("  BenchDir:  {0}" -f $BenchDir)
Write-Host ("  Binaries:  {0}" -f ($Binaries -join ', '))
Write-Host ("  Labels:    {0}" -f ($Labels -join ', '))
Write-Host ("  Duration:  {0} min/binary  ({1} s interval)" -f $DurationMinutes, $IntervalSeconds)
Write-Host ("  Output:    {0}" -f $OutDir)
Write-Host ""

$exePaths = @()
for ($i = 0; $i -lt $Binaries.Count; $i++) {
    $p = Join-Path $BenchDir $Binaries[$i]
    if (-not (Test-Path $p)) {
        Write-Error "Binary not found: $p"
        exit 1
    }
    $exePaths += $p
}

if (-not $SkipPreflightKill) {
    Write-Host "Pre-flight: killing any existing VKey/NexusKey processes..." -ForegroundColor DarkGray
    $namesToKill = $Binaries | ForEach-Object { [System.IO.Path]::GetFileNameWithoutExtension($_) } | Sort-Object -Unique
    Stop-AllInstancesByName -Names $namesToKill
}

$samplesPerRun = [math]::Floor($DurationMinutes * 60 / $IntervalSeconds)
$perRunSec = $WarmupSeconds + ($samplesPerRun * ($IntervalSeconds + 1)) + $CooldownSeconds
$totalMin = [math]::Round(($perRunSec * $Binaries.Count) / 60, 1)
Write-Host ("Estimated total runtime: ~{0} min" -f $totalMin) -ForegroundColor DarkGray
Write-Host ""

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$csvPaths = @()
for ($i = 0; $i -lt $exePaths.Count; $i++) {
    $csv = Join-Path $OutDir ("{0}-{1}.csv" -f $Labels[$i], $timestamp)
    $csvPaths += $csv
    Run-Measurement -ExePath $exePaths[$i] -Label $Labels[$i] -CsvPath $csv -DurationMinutes $DurationMinutes -IntervalSeconds $IntervalSeconds -WarmupSeconds $WarmupSeconds

    if ($i -lt $exePaths.Count - 1) {
        Write-Host ("  Cooldown {0} s..." -f $CooldownSeconds)
        Start-Sleep -Seconds $CooldownSeconds
    }
}

# --- Comparison ------------------------------------------------------------

Write-Host ""
Write-Host "=== Comparison ===" -ForegroundColor Cyan

$summaries = $csvPaths | ForEach-Object { Summarize-Csv $_ }
$summaries | Format-Table -AutoSize

Write-Host ""
Write-Host "Verdict - did Windows trim Private Working Set during idle?" -ForegroundColor Yellow
foreach ($s in $summaries) {
    $delta = $s.PrivateWS_Delta_MB
    if ($delta -lt -1) {
        $verdict = "TRIMMED {0} MB" -f [math]::Abs($delta)
        $color = "Green"
    } elseif ($delta -gt 1) {
        $verdict = "GREW {0} MB (something kept allocating)" -f $delta
        $color = "Yellow"
    } else {
        $verdict = "FLAT (no trim)"
        $color = "Red"
    }
    $line = "  {0,-12}  start={1,7:N2}  end={2,7:N2}  min={3,7:N2}  -> {4}" -f $s.Label, $s.PrivateWS_Start_MB, $s.PrivateWS_End_MB, $s.PrivateWS_Min_MB, $verdict
    Write-Host $line -ForegroundColor $color
}

Write-Host ""
Write-Host "CSV files:" -ForegroundColor DarkGray
foreach ($p in $csvPaths) { Write-Host ("  {0}" -f $p) -ForegroundColor DarkGray }
Write-Host ""
