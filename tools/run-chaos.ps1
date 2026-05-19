#Requires -Version 5.1
<#
.SYNOPSIS
    VKey chaos sweep harness -- drive `VKeyTestRunner.exe` against
    multiple host apps and aggregate per-host JUnit / perf-CSV reports.

.DESCRIPTION
    Replaces the manual workflow described in the README "Run / Full
    corpus + reports" section: per host you used to (1) start VKey,
    (2) focus the target window, (3) run the runner, (4) wait for the
    "stop VKey" prompt, (5) kill VKey, (6) press Enter, (7)
    rename the output. This script automates 1-6 across N hosts and
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

    -Corpus        Path to chaos TOML. Default: tools/VKeyTestRunner/
                   corpus/chaos.toml relative to repo root.

    -VKeyExe   Path to VKey.exe. Default: build/Debug/VKey.exe.

    -RunnerExe     Path to VKeyTestRunner.exe. Default:
                   build/tools/Debug/VKeyTestRunner.exe (CMake
                   RUNTIME_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/tools").

    -HookLog       Override path VKey writes its debug log to. By default
                   the script auto-resolves to `<install dir>\VKey_VKey_<pid>.log`
                   (Logger.cpp:112 format — brand prefix + process tag +
                   actual PID of the VKey.exe process this script spawned).
                   Pass this only if VKey writes elsewhere (e.g. when
                   PathOverride is set or AppData fallback fires).

    -OutDir        Where report-*.xml + perf-*.csv land. Default: repo root.

.USAGE
    # Full sweep, tag "channeltraits".
    # Use `powershell` (Windows PowerShell 5.1, built-in) -- `pwsh` only
    # exists if PowerShell 7+ is separately installed.
    powershell -ExecutionPolicy Bypass -File tools\run-chaos.ps1 -Tag channeltraits

    # Quick local-only run (skip discord/gpt -- no login required)
    powershell -ExecutionPolicy Bypass -File tools\run-chaos.ps1 -Tag dev -Hosts notepad,notepadpp,chrome

    # Single-host smoke test
    powershell -ExecutionPolicy Bypass -File tools\run-chaos.ps1 -Tag smoke -Hosts notepad

.OUTPUTS
    For each host: report-{Tag}-{host}.xml, perf-{Tag}-{host}.csv,
    runner-{Tag}-{host}.log (full runner stdout for post-mortem).

    Final exit code is non-zero if any host reported failures > 0 or
    a host was unreachable.

.NOTES
    Per-host preconditions:
        notepad / notepadpp / chrome -- script launches if not running.
        discord                       -- user must have Discord open + signed in.
        gpt                           -- user must have a Chrome tab open
                                         on chat.openai.com / chatgpt.com,
                                         signed in, prompt textarea ready.
    During each run DO NOT touch keyboard / mouse -- the runner needs
    target focus for the entire 11-case sequence.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$Tag,

    [ValidateSet("notepad","notepadpp","chrome","discord","gpt")]
    [string[]]$Hosts = @("notepad","notepadpp","chrome","discord","gpt"),

    [string]$Corpus,
    [string]$VKeyExe,
    [string]$RunnerExe,
    [string]$HookLog,
    [string]$OutDir,

    # Phase 4 chaos extension: bump SharedState.configGeneration every N ms
    # during the test, forcing HookEngine through the QuickSync slow path +
    # worker-tick deferred reload (Phase 3c contract). 0 = disabled (default).
    # See docs/plans/2026-05-19-architecture-review-design.md §Phase 4
    # production chaos extension.
    [int]$InjectConfigReloadMs = 0
)

$ErrorActionPreference = "Stop"

# --- Repo root + path defaults -----------------------------------------
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Corpus)      { $Corpus      = Join-Path $repoRoot "tools/VKeyTestRunner/corpus/chaos.toml" }
if (-not $VKeyExe) { $VKeyExe = Join-Path $repoRoot "build/Debug/VKey.exe" }
if (-not $RunnerExe)   { $RunnerExe   = Join-Path $repoRoot "build/tools/Debug/VKeyTestRunner.exe" }
# $HookLog resolved per-host inside Invoke-ChaosForHost — depends on the PID
# of the VKey.exe process spawned by Start-VKey (Logger.cpp:112 writes
# `<install dir>\VKey_VKey_<pid>.log`). Honor explicit -HookLog if user passed
# one (override for sideloaded log targets / PathOverride scenarios).
if (-not $OutDir)      { $OutDir      = $repoRoot }

# Fallback search: VKey.exe + VKeyTestRunner.exe are both EXCLUDE_FROM_ALL
# targets, so a plain `cmake --build` skips them. User may have built
# Release instead of Debug — try common alternates before failing.
function Find-ArtefactFallback {
    param([string]$ExpectedPath, [string]$Pattern)
    if (Test-Path $ExpectedPath) { return $ExpectedPath }
    # Look in sibling Release/ dir, then anywhere under build/ matching pattern.
    $altRelease = $ExpectedPath -replace '\\Debug\\', '\Release\'
    if ($altRelease -ne $ExpectedPath -and (Test-Path $altRelease)) { return $altRelease }
    $buildRoot = Join-Path $repoRoot "build"
    if (Test-Path $buildRoot) {
        $hit = Get-ChildItem -Path $buildRoot -Filter $Pattern -Recurse -ErrorAction SilentlyContinue |
               Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

$VKeyExe = Find-ArtefactFallback -ExpectedPath $VKeyExe   -Pattern "VKey.exe"
if (-not $VKeyExe) {
    throw "VKey.exe not found. Build it first:`n  cmake --build build --config Debug --target VKeyApp`nThen re-run, or pass -VKeyExe <path>."
}
$RunnerExe = Find-ArtefactFallback -ExpectedPath $RunnerExe -Pattern "VKeyTestRunner.exe"
if (-not $RunnerExe) {
    throw "VKeyTestRunner.exe not found. Build it first (EXCLUDE_FROM_ALL target):`n  cmake --build build --config Debug --target VKeyTestRunner`nThen re-run, or pass -RunnerExe <path>."
}
if (-not (Test-Path $Corpus)) {
    throw "Corpus TOML not found: $Corpus`nCheck the path or pass -Corpus <path>."
}
Write-Host "VKey      : $VKeyExe"      -ForegroundColor DarkGray
Write-Host "Runner    : $RunnerExe"    -ForegroundColor DarkGray
Write-Host "Corpus    : $Corpus"       -ForegroundColor DarkGray
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

# --- Win32 P/Invoke for window focus -----------------------------------
# We only need: find a window by class or title-pattern, then bring it
# to the foreground. Keystroke driving lives in VKeyTestRunner -- the
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

    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool PostMessageW(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
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

# --- Per-host launchers ------------------------------------------------
# Returns the HWND of the focused window (or [IntPtr]::Zero on failure).
function Open-Host {
    param([string]$Name)

    switch ($Name) {
        "notepad" {
            # Win11 default Notepad is the new RichEditD2DPT app -- exactly
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

# --- VKey lifecycle ------------------------------------------------
# Returns the PID of the spawned VKey.exe process so the caller can derive
# the per-instance hook-log filename (Logger.cpp:112 writes
# `<install dir>\VKey_VKey_<pid>.log`).
function Start-VKey {
    if (Get-Process -Name "VKey" -ErrorAction SilentlyContinue) {
        # Already running -- kill first so each host starts with a fresh
        # hook-log buffer and consistent classifier state.
        # Use -Force here: we don't need the hook log from the stale instance.
        Stop-VKey -Force
    }
    $proc = Start-Process -FilePath $VKeyExe -PassThru
    Start-Sleep -Seconds 2
    # Re-query: Start-Process returns the launcher PID; on Win11 + some
    # AppContainer setups VKey may relaunch itself (e.g., update relaunch
    # path). The live tray-owning process is the one Get-Process finds.
    $alive = Get-Process -Name "VKey" -ErrorAction SilentlyContinue
    if (-not $alive) {
        throw "VKey.exe failed to start -- check $VKeyExe"
    }
    # If multiple VKey processes are alive (unlikely after Stop-VKey -Force
    # above, but possible if Start-Process returned a stale launcher PID
    # and another instance came up), prefer the one Start-Process reported
    # when it's still alive; otherwise fall back to the newest by StartTime.
    # NOTE: `$pid` is a PowerShell automatic variable (current process ID) —
    # use a distinct name to avoid shadowing.
    $vkeyPid = if ($proc -and ($alive.Id -contains $proc.Id)) {
        $proc.Id
    } else {
        ($alive | Sort-Object StartTime -Descending | Select-Object -First 1).Id
    }
    return $vkeyPid
}

# Compute the hook-log path Logger.cpp:112 will write to for a given
# VKey.exe PID. Caller passes -Override to honor an explicit -HookLog
# param from the script command line (sideloaded log targets).
function Resolve-HookLog {
    param(
        [int]$VKeyPid,
        [string]$Override
    )
    if ($Override) { return $Override }
    $installDir = Split-Path -Parent $VKeyExe
    return Join-Path $installDir ("VKey_VKey_{0}.log" -f $VKeyPid)
}

function Stop-VKey {
    param([switch]$Force)

    if ($Force) {
        # Hard kill -- TerminateProcess, no buffer flush.
        Get-Process -Name "VKey" -ErrorAction SilentlyContinue |
            Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
        return
    }

    # Graceful shutdown: send WM_CLOSE to the VKeyTrayClass window.
    # Requires VKey built with ChangeWindowMessageFilterEx(WM_CLOSE,
    # MSGFLT_ALLOW) so UIPI allows the message when VKey is elevated.
    # Clean path: WM_CLOSE → PostQuitMessage → message loop exit →
    # HookEngine::Stop() → CloseHookLog() → fflush + fclose → hook log
    # buffer written to disk.
    $nk = Get-Process -Name "VKey" -ErrorAction SilentlyContinue
    if (-not $nk) { return }

    $trayHwnd = [Win32]::FindWindow("VKeyTrayClass", $null)
    if ($trayHwnd -ne [IntPtr]::Zero) {
        [void][Win32]::PostMessageW($trayHwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)

        # Wait up to 5s for graceful exit
        $deadline = (Get-Date).AddSeconds(5)
        while ((Get-Date) -lt $deadline) {
            if (-not (Get-Process -Name "VKey" -ErrorAction SilentlyContinue)) {
                Start-Sleep -Milliseconds 200   # Let OS finish flushing file handles
                return
            }
            Start-Sleep -Milliseconds 100
        }
        Write-Warning "VKey did not exit within 5s after WM_CLOSE -- force-killing"
    }

    # Fallback: hard kill (no buffer flush -- hook log will be empty)
    Get-Process -Name "VKey" -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}

# --- Per-host runner driver --------------------------------------------
# ──────────────────────────────────────────────────────────────────────────
# Phase 4 chaos extension — config reload injector.
#
# Opens the existing "Local\VKeySharedState" memory map and runs a background
# loop that, every IntervalMs, performs the seqlock-write dance for
# configGeneration:
#
#   epoch += 1   (odd → writers see "write in progress")
#   configGeneration += 1  (the value HookEngine compares against)
#   epoch += 1   (even → readers see stable)
#
# Offsets are locked by `offsetof` static_asserts in SharedState.h:
#   epoch              → offset 12  (uint32_t)
#   configGeneration   → offset 33  (uint8_t)
#
# HookEngine's QuickSync slow path sees the bump on its next call; on the
# hook thread it sets pendingConfigReload_, the worker tick drains it →
# ReloadFromToml runs on worker. Per Phase 3c, this exercises the
# "TOML parse off hook" contract under typing load.
# ──────────────────────────────────────────────────────────────────────────
function Start-ConfigReloadInjector {
    param([int]$IntervalMs)

    $rs = [runspacefactory]::CreateRunspace()
    $rs.ApartmentState = "STA"
    $rs.Open()

    $ps = [powershell]::Create()
    $ps.Runspace = $rs

    [void]$ps.AddScript({
        param([int]$Interval)
        $mmf = $null
        try {
            $mmf = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting("Local\VKeySharedState")
        } catch {
            # VKey not yet running, or no permission. Silent — main test
            # will exit on its own if VKey actually failed to start.
            return
        }
        $accessor = $mmf.CreateViewAccessor()
        try {
            while ($true) {
                Start-Sleep -Milliseconds $Interval
                # Seqlock: bump epoch odd → write configGeneration → bump epoch even.
                # Matches SharedStateManager::Write() pattern (SharedStateManager.cpp:193).
                $epoch = $accessor.ReadUInt32(12)
                $accessor.Write(12, [uint32]($epoch + 1))   # odd: writing
                $gen = $accessor.ReadByte(33)
                $accessor.Write(33, [byte](($gen + 1) -band 0xFF))
                $accessor.Write(12, [uint32]($epoch + 2))   # even: stable
            }
        } finally {
            $accessor.Dispose()
            $mmf.Dispose()
        }
    }).AddArgument($IntervalMs)

    $handle = $ps.BeginInvoke()
    return @{
        PS       = $ps
        Handle   = $handle
        Runspace = $rs
    }
}

function Stop-ConfigReloadInjector {
    param($Injector)
    if ($null -eq $Injector) { return }
    try {
        # Stop() aborts the runspace's busy loop + releases the MMF handle
        # via the script's `finally` block (or our `try/finally` here as
        # a safety net for forcible termination).
        $Injector.PS.Stop()
    } catch { }
    try { $Injector.Runspace.Close() } catch { }
    try { $Injector.PS.Dispose() } catch { }
}

function Invoke-ChaosForHost {
    param([string]$HostName)

    Write-Host "`n=== HOST: $HostName ===" -ForegroundColor Cyan

    $vkeyPid = Start-VKey
    $resolvedHookLog = Resolve-HookLog -VKeyPid $vkeyPid -Override $HookLog
    Write-Host "[$HostName] VKey PID=$vkeyPid hook-log=$resolvedHookLog" -ForegroundColor DarkGray

    $hwnd = Open-Host -Name $HostName
    if ($hwnd -eq [IntPtr]::Zero) {
        Write-Warning "[$HostName] no target window -- skipping"
        Stop-VKey -Force
        return [PSCustomObject]@{ Host = $HostName; Tests = 0; Failures = -1; Reason = "no target window" }
    }
    if (-not (Focus-Window -Hwnd $hwnd)) {
        Write-Warning "[$HostName] failed to focus target -- skipping"
        Stop-VKey -Force
        return [PSCustomObject]@{ Host = $HostName; Tests = 0; Failures = -1; Reason = "focus failed" }
    }

    $reportPath = Join-Path $OutDir "report-$Tag-$HostName.xml"
    $perfPath   = Join-Path $OutDir "perf-$Tag-$HostName.csv"
    $logPath    = Join-Path $OutDir "runner-$Tag-$HostName.log"

    $argv = @(
        "--corpus", $Corpus,
        "--hook-log", $resolvedHookLog,
        "--junit", $reportPath,
        "--perf-csv", $perfPath
    )

    # Build a single Arguments string with each value double-quoted --
    # ProcessStartInfo.ArgumentList is .NET 6+ / PS 7+ only, so on
    # PS 5.1 we have to assemble Arguments by hand. Repo paths may
    # contain spaces (Program Files, OneDrive folders, ...) so quote
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

    # Re-focus the target -- Process.Start can briefly steal focus even
    # with CreateNoWindow=true depending on shell hosts.
    [void](Focus-Window -Hwnd $hwnd)

    # Phase 4 chaos: spawn the config-reload injector if requested. Starts
    # AFTER the runner is up (so VKey has registered the SharedState map)
    # and is torn down in the finally below regardless of test outcome.
    $configInjector = $null
    if ($InjectConfigReloadMs -gt 0) {
        $configInjector = Start-ConfigReloadInjector -IntervalMs $InjectConfigReloadMs
        Write-Host "[$HostName] config-reload injector active (interval=${InjectConfigReloadMs}ms)" -ForegroundColor DarkYellow
    }

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
            if (-not $sawStopPrompt -and ($line -match "stop VKey|Press Enter")) {
                $sawStopPrompt = $true
                Stop-VKey
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
        Stop-ConfigReloadInjector -Injector $configInjector
    }

    $exitCode = $proc.ExitCode
    Stop-VKey -Force   # belt-and-braces: ensure killed even if the prompt was missed

    if (-not (Test-Path $reportPath)) {
        Write-Warning "[$HostName] report file not produced (exit=$exitCode)"
        return [PSCustomObject]@{ Host = $HostName; Tests = 0; Failures = -1; ExitCode = $exitCode; Reason = "no report" }
    }

    [xml]$xml = Get-Content $reportPath
    $tests    = [int]$xml.testsuites.tests
    $failures = [int]$xml.testsuites.failures
    # PS 5.1 doesn't support `if` as an expression -- pre-compute the colour.
    $colour = if ($failures -eq 0 -and $exitCode -eq 0) { 'Green' } else { 'Red' }
    Write-Host "[$HostName] tests=$tests failures=$failures exit=$exitCode" -ForegroundColor $colour
    return [PSCustomObject]@{ Host = $HostName; Tests = $tests; Failures = $failures; ExitCode = $exitCode }
}

# --- Main --------------------------------------------------------------
$results = @()
try {
    foreach ($h in $Hosts) {
        $results += Invoke-ChaosForHost -HostName $h
    }
} finally {
    Stop-VKey -Force
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
