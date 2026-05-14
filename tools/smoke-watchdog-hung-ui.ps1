# ============================================================
# Smoke 3 — Watchdog: UI hung but alive
# ============================================================
# Suspends ALL VKey processes (simulating UI freeze).
# Watchdog must observe stale heartbeat + alive process and
# REFUSE to respawn (avoid kill-respawn loops on hangs).
#
# Usage:
#   Run in PowerShell as Administrator (VKey may be admin):
#     powershell.exe -ExecutionPolicy Bypass -File tools\smoke-watchdog-hung-ui.ps1
#   Or paste content into elevated PS session.
#
# Pass criteria (auto-checked at end):
#   - VKey PIDs unchanged (no respawn happened)
#   - watchdog.log contains "Heartbeat stale but process alive"
#   - watchdog.log does NOT contain "RespawnVKey: spawned"
# ============================================================

# 0. Admin check
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "PS not admin. Quit + Run as Administrator." -ForegroundColor Red
    exit 1
}
Write-Host "PS admin OK" -ForegroundColor Green

# 0b. Resolve exe root (try UNC variants + Z:)
$logPath = "$env:LOCALAPPDATA\VKey\watchdog.log"
$candidates = @(
    "\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\VKey\build\Release",
    "\\wsl`$\Ubuntu-24.04\home\phatmt\code\VKey\build\Release",
    "Z:\home\phatmt\code\VKey\build\Release"
)
$exeRoot = $candidates | Where-Object { Test-Path "$_\VKey.exe" } | Select-Object -First 1
if (-not $exeRoot) {
    Write-Host "VKey.exe not found in any candidate path:" -ForegroundColor Red
    $candidates | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host "exeRoot = $exeRoot" -ForegroundColor Green

# 0c. P/Invoke (idempotent)
if (-not ('SmokeWD.N' -as [type])) {
    Add-Type -Namespace SmokeWD -Name N -MemberDefinition @"
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)] public static extern System.IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, uint dwProcessId);
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)] public static extern bool CloseHandle(System.IntPtr hObject);
[System.Runtime.InteropServices.DllImport("ntdll.dll")] public static extern uint NtSuspendProcess(System.IntPtr hProcess);
[System.Runtime.InteropServices.DllImport("ntdll.dll")] public static extern uint NtResumeProcess(System.IntPtr hProcess);
"@
}

# 1. Ensure VKey running
$nkProcs = @(Get-Process VKey -ErrorAction SilentlyContinue)
if ($nkProcs.Count -eq 0) {
    Write-Host "Starting VKey..."
    & "$exeRoot\VKey.exe"
    Start-Sleep -Seconds 5
    $nkProcs = @(Get-Process VKey -ErrorAction SilentlyContinue)
    if ($nkProcs.Count -eq 0) { Write-Host "VKey không start được" -ForegroundColor Red; exit 1 }
}
$nkPids = @($nkProcs | Select-Object -ExpandProperty Id)
Write-Host "VKey PIDs: $($nkPids -join ', ')" -ForegroundColor Green

# 2. Ensure watchdog running
$wd = Get-Process VKeyWatchdog -ErrorAction SilentlyContinue
if (-not $wd) {
    Write-Host "Starting watchdog..."
    & "$exeRoot\VKeyWatchdog.exe"
    Write-Host "Waiting 35s for POST_INIT_GRACE_MS..."
    Start-Sleep -Seconds 35
} else {
    Write-Host "Watchdog already running PID=$($wd.Id)" -ForegroundColor Green
}

# 3. Snapshot log size right before suspend
$sizeBefore = if (Test-Path $logPath) { (Get-Item $logPath).Length } else { 0 }
Write-Host "Log size before suspend: $sizeBefore bytes"

# 4. Suspend all VKey processes
$handles = @()
foreach ($p in $nkPids) {
    $h = [SmokeWD.N]::OpenProcess(0x0800, $false, [uint32]$p)
    if ($h -eq [System.IntPtr]::Zero) {
        $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        Write-Host "OpenProcess($p) failed err=$err" -ForegroundColor Red
        continue
    }
    [SmokeWD.N]::NtSuspendProcess($h) | Out-Null
    $handles += @{ pid_ = $p; handle = $h }
    Write-Host "Suspended PID $p" -ForegroundColor Yellow
}
if ($handles.Count -eq 0) { Write-Host "Không suspend được process nào" -ForegroundColor Red; exit 1 }

# 5. Wait through heartbeat timeout
Write-Host "Sleeping 100s ($(Get-Date -Format 'HH:mm:ss'))..."
Start-Sleep -Seconds 100

# 6. Verify state
Write-Host "`n--- Process state ($(Get-Date -Format 'HH:mm:ss')) ---" -ForegroundColor Cyan
Get-Process VKey | Select-Object Id, StartTime | Format-Table | Out-String | Write-Host

Write-Host "--- Watchdog log NEW lines ---" -ForegroundColor Cyan
$bytes = [System.IO.File]::ReadAllBytes($logPath)
$newLog = if ($bytes.Length -gt $sizeBefore) {
    [System.Text.Encoding]::Unicode.GetString($bytes, $sizeBefore, $bytes.Length - $sizeBefore)
} else { "" }
Write-Host $newLog

# 7. Resume + cleanup
foreach ($x in $handles) {
    [SmokeWD.N]::NtResumeProcess($x.handle) | Out-Null
    [SmokeWD.N]::CloseHandle($x.handle) | Out-Null
    Write-Host "Resumed PID $($x.pid_)" -ForegroundColor Green
}

# 8. Auto verdict
Write-Host "`n========== VERDICT ==========" -ForegroundColor Cyan
$nowPids = @((Get-Process VKey -ErrorAction SilentlyContinue) | Select-Object -ExpandProperty Id)
$samePids = -not (Compare-Object $nkPids $nowPids -SyncWindow 0)
$hasNotRespawnLog = $newLog -match "Heartbeat stale but process alive"
$hasRespawnLog = $newLog -match "RespawnVKey: spawned"

if ($samePids -and $hasNotRespawnLog -and -not $hasRespawnLog) {
    Write-Host "Smoke 3 PASS — UI hung detected, process not respawned" -ForegroundColor Green
    exit 0
} else {
    Write-Host "Smoke 3 FAIL" -ForegroundColor Red
    Write-Host "  PIDs same?              $samePids   (before=$($nkPids -join ','), after=$($nowPids -join ','))"
    Write-Host "  Has 'NOT respawning'?   $hasNotRespawnLog"
    Write-Host "  Has 'spawned PID='?     $hasRespawnLog (BAD if true)"
    exit 1
}
