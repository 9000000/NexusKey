#Requires -Version 5.1
<#
.SYNOPSIS
    NexusKey chaos sweep harness — drive `NextKeyTestRunner.exe` against
    multiple host apps and aggregate per-host JUnit / perf-CSV reports.

.DESCRIPTION
    Replaces the manual workflow described in the README "Run / Full
    corpus + reports" section: per host you used to (1) start NexusKey,
    (2) focus the target window, (3) run the runner, (4) wait for the
    "stop NexusKey" prompt, (5) kill NexusKey, (6) press Enter, (7)
    rename the output. This script automates 1–6 across N hosts and
    leaves output filenames tagged with -Tag.

    Hosts notepad / notepadpp / chrome are launched fresh if not
    already running. Hosts discord / gpt require the user to have the
    app already open and signed in (network + auth state out of scope
    for the harness).

.PARAMETERS
    -Tag           Required. Identifier embedded in output filenames:
                   report-{Tag}-{host}.xml + perf-{Tag}-{host}.csv.
                   Suggestion: PR / branch nickname (e.g. "channeltraits").

    -Hosts         Subset of @(notepad, notepadpp, chrome, discord, gpt).
                   Default: all five.

    -Corpus        Path to chaos TOML. Default: tools/NextKeyTestRunner/
                   corpus/chaos.toml relative to repo root.

    -NexusKeyExe   Path to NexusKey.exe. Default: build/Debug/NexusKey.exe.

    -RunnerExe     Path to NextKeyTestRunner.exe. Default:
                   build/tools/NextKeyTestRunner/Debug/NextKeyTestRunner.exe.

    -HookLog       Path NexusKey writes its debug log to (must match
                   NexusKey's compiled-in OpenHookLog target). Default:
                   build/Debug/NexusKey_hook.log.

    -OutDir        Where report-*.xml + perf-*.csv land. Default: repo root.

.USAGE
    # Full sweep, tag "channeltraits"
    pwsh tools/run-chaos.ps1 -Tag channeltraits

    # Quick local-only run (skip discord/gpt — no login required)
    pwsh tools/run-chaos.ps1 -Tag dev -Hosts notepad,notepadpp,chrome

    # Single-host smoke test
    pwsh tools/run-chaos.ps1 -Tag smoke -Hosts notepad

.OUTPUTS
    For each host: report-{Tag}-{host}.xml, perf-{Tag}-{host}.csv,
    runner-{Tag}-{host}.log (full runner stdout for post-mortem).

    Final exit code is non-zero if any host reported failures > 0 or
    a host was unreachable.

.NOTES
    Per-host preconditions:
        notepad / notepadpp / chrome — script launches if not running.
        discord                       — user must have Discord open + signed in.
        gpt                           — user must have a Chrome tab open
                                         on chat.openai.com / chatgpt.com,
                                         signed in, prompt textarea ready.
    During each run DO NOT touch keyboard / mouse — the runner needs
    target focus for the entire 11-case sequence.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$Tag,

    [ValidateSet("notepad","notepadpp","chrome","discord","gpt")]
    [string[]]$Hosts = @("notepad","notepadpp","chrome","discord","gpt"),

    [string]$Corpus,
    [string]$NexusKeyExe,
    [string]$RunnerExe,
    [string]$HookLog,
    [string]$OutDir
)

$ErrorActionPreference = "Stop"

# ─── Repo root + path defaults ─────────────────────────────────────────
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Corpus)      { $Corpus      = Join-Path $repoRoot "tools/NextKeyTestRunner/corpus/chaos.toml" }
if (-not $NexusKeyExe) { $NexusKeyExe = Join-Path $repoRoot "build/Debug/NexusKey.exe" }
if (-not $RunnerExe)   { $RunnerExe   = Join-Path $repoRoot "build/tools/NextKeyTestRunner/Debug/NextKeyTestRunner.exe" }
if (-not $HookLog)     { $HookLog     = Join-Path $repoRoot "build/Debug/NexusKey_hook.log" }
if (-not $OutDir)      { $OutDir      = $repoRoot }

foreach ($p in @($Corpus, $NexusKeyExe, $RunnerExe)) {
    if (-not (Test-Path $p)) {
        throw "Required artefact missing: $p (build NexusKey + NextKeyTestRunner first)"
    }
}
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

# ─── Win32 P/Invoke for window focus ───────────────────────────────────
# We only need: find a window by class or title-pattern, then bring it
# to the foreground. Keystroke driving lives in NextKeyTestRunner — the
# script never sends keys itself.
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class Win32 {
    [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);
}
"@

function Find-WindowByTitleSubstring {
    param([string]$Substring)
    $found = [IntPtr]::Zero
    $proc = [Win32+EnumWindowsProc]{
        param($hWnd, $lParam)
        if (-not [Win32]::IsWindowVisible($hWnd)) { return $true }
        $sb = New-Object System.Text.StringBuilder 256
        [void][Win32]::GetWindowText($hWnd, $sb, $sb.Capacity)
        if ($sb.ToString() -like "*$Substring*") {
            $script:foundHwnd = $hWnd
            return $false  # stop enumeration
        }
        return $true
    }
    $script:foundHwnd = [IntPtr]::Zero
    [void][Win32]::EnumWindows($proc, [IntPtr]::Zero)
    return $script:foundHwnd
}

function Focus-Window {
    param([IntPtr]$Hwnd)
    if ($Hwnd -eq [IntPtr]::Zero) { return $false }
    [void][Win32]::ShowWindow($Hwnd, 9)  # SW_RESTORE
    [void][Win32]::SetForegroundWindow($Hwnd)
    Start-Sleep -Milliseconds 200
    return $true
}

# ─── Per-host launchers ────────────────────────────────────────────────
# Returns the HWND of the focused window (or [IntPtr]::Zero on failure).
function Open-Host {
    param([string]$Name)

    switch ($Name) {
        "notepad" {
            # Win11 default Notepad is the new RichEditD2DPT app — exactly
            # the host we want for that test slot. Win10 falls back to
            # legacy notepad.exe; both bind to a window with "Notepad" in
            # the title.
            Start-Process -FilePath "notepad.exe" | Out-Null
            Start-Sleep -Seconds 1
            return (Find-WindowByTitleSubstring "Notepad")
        }
        "notepadpp" {
            $candidates = @(
                "C:\Program Files\Notepad++\notepad++.exe",
                "C:\Program Files (x86)\Notepad++\notepad++.exe"
            )
            $exe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
            if (-not $exe) { throw "Notepad++ not found in default install paths" }
            Start-Process -FilePath $exe | Out-Null
            Start-Sleep -Seconds 1
            return (Find-WindowByTitleSubstring "Notepad++")
        }
        "chrome" {
            # Reuse if running (avoid wiping user tabs); otherwise launch.
            $hwnd = Find-WindowByTitleSubstring "Google Chrome"
            if ($hwnd -eq [IntPtr]::Zero) {
                Start-Process -FilePath "chrome.exe" | Out-Null
                Start-Sleep -Seconds 2
                $hwnd = Find-WindowByTitleSubstring "Google Chrome"
            }
            return $hwnd
        }
        "discord" {
            # User precondition: Discord open + signed in.
            $hwnd = Find-WindowByTitleSubstring "Discord"
            if ($hwnd -eq [IntPtr]::Zero) {
                Write-Warning "Discord window not found. Open Discord and sign in before re-running this host."
            }
            return $hwnd
        }
        "gpt" {
            # User precondition: a Chrome tab on chat.openai.com or
            # chatgpt.com, signed in, prompt textarea visible.
            $hwnd = Find-WindowByTitleSubstring "ChatGPT"
            if ($hwnd -eq [IntPtr]::Zero) {
                $hwnd = Find-WindowByTitleSubstring "chat.openai.com"
            }
            if ($hwnd -eq [IntPtr]::Zero) {
                Write-Warning "ChatGPT browser tab not found. Open the tab + sign in, then re-run this host."
            }
            return $hwnd
        }
    }
    return [IntPtr]::Zero
}

# ─── NexusKey lifecycle ────────────────────────────────────────────────
function Start-NexusKey {
    if (Get-Process -Name "NexusKey" -ErrorAction SilentlyContinue) {
        # Already running — kill first so each host starts with a fresh
        # hook-log buffer and consistent classifier state.
        Stop-NexusKey
    }
    Start-Process -FilePath $NexusKeyExe | Out-Null
    Start-Sleep -Seconds 2
    if (-not (Get-Process -Name "NexusKey" -ErrorAction SilentlyContinue)) {
        throw "NexusKey.exe failed to start — check $NexusKeyExe"
    }
}

function Stop-NexusKey {
    Get-Process -Name "NexusKey" -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}

# ─── Per-host runner driver ────────────────────────────────────────────
function Invoke-ChaosForHost {
    param([string]$HostName)

    Write-Host "`n=== HOST: $HostName ===" -ForegroundColor Cyan

    Start-NexusKey

    $hwnd = Open-Host -Name $HostName
    if ($hwnd -eq [IntPtr]::Zero) {
        Write-Warning "[$HostName] no target window — skipping"
        Stop-NexusKey
        return [PSCustomObject]@{ Host = $HostName; Tests = 0; Failures = -1; Reason = "no target window" }
    }
    if (-not (Focus-Window -Hwnd $hwnd)) {
        Write-Warning "[$HostName] failed to focus target — skipping"
        Stop-NexusKey
        return [PSCustomObject]@{ Host = $HostName; Tests = 0; Failures = -1; Reason = "focus failed" }
    }

    $reportPath = Join-Path $OutDir "report-$Tag-$HostName.xml"
    $perfPath   = Join-Path $OutDir "perf-$Tag-$HostName.csv"
    $logPath    = Join-Path $OutDir "runner-$Tag-$HostName.log"

    $argv = @(
        "--corpus", $Corpus,
        "--hook-log", $HookLog,
        "--junit", $reportPath,
        "--perf-csv", $perfPath
    )

    # Build a single Arguments string with each value double-quoted —
    # ProcessStartInfo.ArgumentList is .NET 6+ / PS 7+ only, so on
    # PS 5.1 we have to assemble Arguments by hand. Repo paths may
    # contain spaces (Program Files, OneDrive folders, …) so quote
    # unconditionally to be safe.
    $quoted = @()
    foreach ($a in $argv) { $quoted += '"' + $a + '"' }
    $argString = [string]::Join(' ', $quoted)

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName  = $RunnerExe
    $psi.Arguments = $argString
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true

    $proc = [System.Diagnostics.Process]::Start($psi)

    # Re-focus the target — Process.Start can briefly steal focus even
    # with CreateNoWindow=true depending on shell hosts.
    [void](Focus-Window -Hwnd $hwnd)

    # Stream stdout line-by-line, watching for the prompt that gates the
    # report-write phase. Mirror everything to log file + console.
    $logWriter = [System.IO.StreamWriter]::new($logPath)
    try {
        $sawStopPrompt = $false
        while (-not $proc.HasExited) {
            $line = $proc.StandardOutput.ReadLine()
            if ($null -eq $line) { Start-Sleep -Milliseconds 50; continue }
            $logWriter.WriteLine($line)
            $logWriter.Flush()
            Write-Host "[$HostName] $line" -ForegroundColor DarkGray
            if (-not $sawStopPrompt -and ($line -match "stop NexusKey|Press Enter")) {
                $sawStopPrompt = $true
                Stop-NexusKey
                $proc.StandardInput.WriteLine("")
                $proc.StandardInput.Flush()
            }
        }
        # Drain remaining stdout + stderr.
        $tail = $proc.StandardOutput.ReadToEnd()
        if ($tail) { $logWriter.WriteLine($tail); Write-Host $tail }
        $err = $proc.StandardError.ReadToEnd()
        if ($err) { $logWriter.WriteLine("STDERR:"); $logWriter.WriteLine($err) }
    } finally {
        $logWriter.Close()
    }

    $exitCode = $proc.ExitCode
    Stop-NexusKey   # belt-and-braces: ensure killed even if the prompt was missed

    if (-not (Test-Path $reportPath)) {
        Write-Warning "[$HostName] report file not produced (exit=$exitCode)"
        return [PSCustomObject]@{ Host = $HostName; Tests = 0; Failures = -1; ExitCode = $exitCode; Reason = "no report" }
    }

    [xml]$xml = Get-Content $reportPath
    $tests    = [int]$xml.testsuites.tests
    $failures = [int]$xml.testsuites.failures
    # PS 5.1 doesn't support `if` as an expression — pre-compute the colour.
    $colour = if ($failures -eq 0 -and $exitCode -eq 0) { 'Green' } else { 'Red' }
    Write-Host "[$HostName] tests=$tests failures=$failures exit=$exitCode" -ForegroundColor $colour
    return [PSCustomObject]@{ Host = $HostName; Tests = $tests; Failures = $failures; ExitCode = $exitCode }
}

# ─── Main ──────────────────────────────────────────────────────────────
$results = @()
try {
    foreach ($h in $Hosts) {
        $results += Invoke-ChaosForHost -HostName $h
    }
} finally {
    Stop-NexusKey
}

Write-Host "`n=== SUMMARY (tag=$Tag) ===" -ForegroundColor Cyan
$results | Format-Table -AutoSize Host, Tests, Failures, ExitCode, Reason

$totalTests    = ($results | Measure-Object -Property Tests    -Sum).Sum
$totalFailures = ($results | Measure-Object -Property Failures -Sum).Sum
$badHosts      = $results | Where-Object { $_.Failures -ne 0 -or $_.ExitCode -ne 0 }

Write-Host "Total: $totalTests tests across $($results.Count) hosts; failures=$totalFailures"
if ($badHosts.Count -gt 0) {
    Write-Host "FAIL: $($badHosts.Count) host(s) regressed or unreachable" -ForegroundColor Red
    exit 1
}
Write-Host "PASS: all hosts clean" -ForegroundColor Green
exit 0
