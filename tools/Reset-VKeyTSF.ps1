<#
.SYNOPSIS
    Reset VKey's TSF (Text Services Framework) registration to a clean state.
    Đặt lại đăng ký TSF của VKey về trạng thái sạch.

.DESCRIPTION
    Fixes the "stuck with the old TSF" symptom (#209) that appears after updating
    VKey across the langid change (Vietnamese 0x042A -> English-US 0x0409): the old
    profile and a duplicate input-list entry linger in the registry and a plain
    re-register does not clear them. This script:
      1. Stops VKey processes so the DLL is unlocked.
      2. Unregisters the TIP   (regsvr32 /u  -> DllUnregisterServer).
      3. Purges leftover registry refs to VKey's CLSID / profile under BOTH langids
         (HKLM + HKCU CTF\TIP, CTF\Assemblies, CTF\SortOrder, Classes\CLSID, and the
         per-user input list in Control Panel\International\User Profile*).
      4. Re-registers the fresh 0x0409 profile (regsvr32).
      5. Restarts VKey.exe.
    Then SIGN OUT and back IN (or reboot) so Windows rebuilds the input indicator.

    It only ever deletes keys/values that reference VKey's own GUIDs — nothing else.

.PARAMETER DllPath
    Full path to VKeyTSF.dll. If omitted, the script looks next to itself, then in
    the folder of a running VKey.exe, then at the registered InprocServer32 path.

.PARAMETER NoReinstall
    Purge only — skip the re-register / restart steps (use when uninstalling for good).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\Reset-VKeyTSF.ps1
    powershell -ExecutionPolicy Bypass -File .\Reset-VKeyTSF.ps1 -DllPath "C:\VKey\VKeyTSF.dll"
#>
[CmdletBinding()]
param(
    [string]$DllPath,
    [switch]$NoReinstall
)

# VKey's TSF identifiers — must match src/tsf/Globals.cpp / Globals.h.
$VKeyClsid   = '{DEB18BD1-2331-4F2A-B030-DA9EB0093683}'
$VKeyProfile = '{2FE17DA4-D8E2-4B28-8566-C30E8F04BFD4}'

$ErrorActionPreference = 'Stop'

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "    $msg" -ForegroundColor Green }
function Write-Warn2($msg){ Write-Host "    $msg" -ForegroundColor Yellow }

# ── Self-elevate (HKLM + regsvr32 need admin) ───────────────────────────────
$isAdmin = ([Security.Principal.WindowsPrincipal] `
    [Security.Principal.WindowsIdentity]::GetCurrent()
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Step "Cần quyền Administrator — đang nâng quyền... / Re-launching elevated..."
    $argList = @('-ExecutionPolicy','Bypass','-File',"`"$PSCommandPath`"")
    if ($DllPath)     { $argList += @('-DllPath',"`"$DllPath`"") }
    if ($NoReinstall) { $argList += '-NoReinstall' }
    Start-Process powershell.exe -Verb RunAs -ArgumentList $argList
    return
}

Write-Host ""
Write-Host "  VKey — TSF Reset / Đặt lại TSF" -ForegroundColor White
Write-Host "  CLSID   $VKeyClsid"
Write-Host "  Profile $VKeyProfile"
Write-Host ""

# ── Locate VKeyTSF.dll ──────────────────────────────────────────────────────
function Find-Dll {
    if ($DllPath -and (Test-Path -LiteralPath $DllPath)) { return (Resolve-Path -LiteralPath $DllPath).Path }
    $candidates = @(Join-Path $PSScriptRoot 'VKeyTSF.dll')
    foreach ($p in Get-Process -Name 'VKey','VKeyClassic' -ErrorAction SilentlyContinue) {
        try { $candidates += (Join-Path (Split-Path $p.Path) 'VKeyTSF.dll') } catch {}
    }
    # InprocServer32 already points at the registered DLL (resolves even mid-upgrade).
    # HKLM only (no HKCU): this script runs ELEVATED and regsvr32 below EXECUTES the
    # DLL at this path (DllRegisterServer). HKCU\...\CLSID is user-writable, so trusting
    # an HKCU InprocServer32 value here would let a non-admin pre-point it at a malicious
    # DLL and have it loaded with admin rights. HKLM requires admin to write, so it is the
    # only registry source we trust as a regsvr32 target. (HKCU CLSID is still PURGED in
    # step 3 — deleting a user-owned key is safe; only executing its path was the risk.)
    foreach ($hive in 'HKLM:\SOFTWARE\Classes\CLSID',
                       'HKLM:\SOFTWARE\WOW6432Node\Classes\CLSID') {
        $ip = "$hive\$VKeyClsid\InprocServer32"
        if (Test-Path $ip) {
            $v = (Get-ItemProperty $ip -ErrorAction SilentlyContinue).'(default)'
            if ($v) { $candidates += $v }
        }
    }
    $candidates += "$env:ProgramFiles\VKey\VKeyTSF.dll"
    $candidates += "$env:LOCALAPPDATA\VKey\VKeyTSF.dll"
    foreach ($c in $candidates) {
        if ($c -and (Test-Path -LiteralPath $c)) { return (Resolve-Path -LiteralPath $c).Path }
    }
    return $null
}

$dll = Find-Dll
if ($dll) { Write-Ok "Tìm thấy DLL / DLL found: $dll" }
else      { Write-Warn2 "Không tìm thấy VKeyTSF.dll — sẽ chỉ dọn registry. / DLL not found — purge only." }

# ── 1. Stop VKey processes so the DLL is unlocked ───────────────────────────
Write-Step "Dừng VKey / Stopping VKey processes..."
Get-Process -Name 'VKey','VKeyClassic','VKeyWatchdog' -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 700
Write-Ok "Đã dừng. / Stopped."

# ── 2. Unregister via the DLL (clears HKLM TIP + CLSID, both langids) ────────
if ($dll) {
    Write-Step "Hủy đăng ký / Unregister (regsvr32 /u)..."
    Start-Process regsvr32.exe -ArgumentList '/s','/u',"`"$dll`"" -Wait -ErrorAction SilentlyContinue
    Write-Ok "Xong. / Done."
}

# ── 3a. Delete the predictable exact keys (fast — no recursion) ─────────────
Write-Step "Dọn registry còn sót / Purging leftover registry refs..."
$exactKeys = @(
    "HKLM:\SOFTWARE\Microsoft\CTF\TIP\$VKeyClsid",
    "HKLM:\SOFTWARE\WOW6432Node\Microsoft\CTF\TIP\$VKeyClsid",
    "HKCU:\SOFTWARE\Microsoft\CTF\TIP\$VKeyClsid",
    "HKLM:\SOFTWARE\Classes\CLSID\$VKeyClsid",
    "HKLM:\SOFTWARE\WOW6432Node\Classes\CLSID\$VKeyClsid",
    "HKCU:\SOFTWARE\Classes\CLSID\$VKeyClsid"
)
foreach ($k in $exactKeys) {
    if (Test-Path $k) {
        Remove-Item $k -Recurse -Force -ErrorAction SilentlyContinue
        Write-Ok "xóa key / removed key: $k"
    }
}

# ── 3b. Recursively scrub the small input-list trees ────────────────────────
# Removes ONLY subkeys/values whose name or (string) data references our CLSID or
# profile GUID — the stale 0x042A profile and the duplicate 'VIE' entry. These
# trees are small (a few dozen keys), so full recursion is cheap and safe.
function Purge-Refs([string]$base) {
    if (-not (Test-Path $base)) { return }
    foreach ($sub in Get-ChildItem $base -ErrorAction SilentlyContinue) {
        $n = $sub.PSChildName
        if ($n -match [regex]::Escape($VKeyClsid) -or $n -match [regex]::Escape($VKeyProfile)) {
            Remove-Item $sub.PSPath -Recurse -Force -ErrorAction SilentlyContinue
            Write-Ok "xóa key / removed key: $($sub.Name)"
        } else {
            Purge-Refs $sub.PSPath
        }
    }
    $item = Get-Item $base -ErrorAction SilentlyContinue
    if ($item) {
        foreach ($vn in $item.GetValueNames()) {
            $hit = ($vn -match [regex]::Escape($VKeyClsid)) -or ($vn -match [regex]::Escape($VKeyProfile))
            if (-not $hit) {
                $data = $null
                try { $data = [string]$item.GetValue($vn) } catch {}
                if ($data -and ($data -match [regex]::Escape($VKeyClsid) -or $data -match [regex]::Escape($VKeyProfile))) { $hit = $true }
            }
            if ($hit) {
                Remove-ItemProperty -Path $base -Name $vn -Force -ErrorAction SilentlyContinue
                Write-Ok "xóa value / removed value: $vn"
            }
        }
    }
}
foreach ($t in @(
    'HKCU:\SOFTWARE\Microsoft\CTF\Assemblies',
    'HKCU:\SOFTWARE\Microsoft\CTF\SortOrder',
    'HKCU:\Control Panel\International\User Profile',
    'HKCU:\Control Panel\International\User Profile System Backup'
)) { Purge-Refs $t }
Write-Ok "Dọn xong. / Purge complete."

# ── 4. Re-register the fresh 0x0409 profile ─────────────────────────────────
if ($dll -and -not $NoReinstall) {
    Write-Step "Đăng ký lại / Re-register (regsvr32)..."
    Start-Process regsvr32.exe -ArgumentList '/s',"`"$dll`"" -Wait -ErrorAction SilentlyContinue
    Write-Ok "Đã đăng ký lại dưới 0x0409. / Re-registered under 0x0409."

    $exe = Join-Path (Split-Path $dll) 'VKey.exe'
    if (Test-Path $exe) {
        Write-Step "Khởi động lại VKey / Restarting VKey.exe..."
        Start-Process $exe
        Write-Ok "Đã chạy. / Launched."
    }
} elseif ($NoReinstall) {
    Write-Warn2 "Bỏ qua đăng ký lại (-NoReinstall). / Skipped re-register."
}

Write-Host ""
Write-Host "  HOÀN TẤT. Hãy ĐĂNG XUẤT rồi đăng nhập lại (hoặc khởi động lại máy)" -ForegroundColor Green
Write-Host "  để Windows nạp lại danh sách bộ gõ và xóa mục 'VIE' trùng." -ForegroundColor Green
Write-Host "  DONE. SIGN OUT and back in (or reboot) so Windows rebuilds the" -ForegroundColor Green
Write-Host "  input list and drops the duplicate 'VIE' entry." -ForegroundColor Green
Write-Host ""
Read-Host "Nhấn Enter để đóng / Press Enter to close" | Out-Null
