#Requires -Version 5.1
<#
.SYNOPSIS
    Stress test for the word "lôĩ" with different Telex key orderings.
.DESCRIPTION
    Tests 3 scenarios at high speed (30 words each with spaces):
        Scenario 1: l-o-o-i-x   (looix)  — circumflex first, then tilde
        Scenario 2: l-o-i-x-o   (loixo)  — tilde first, then circumflex
        Scenario 3: l-o-i-o-x   (loiox)  — vowel cluster first, then marks
    Each scenario types 30 words separated by spaces into Notepad,
    then reads back the text and checks every word == "lôĩ".
.USAGE
    powershell -ExecutionPolicy Bypass -File tools\test_loix.ps1
    powershell -ExecutionPolicy Bypass -File tools\test_loix.ps1 -WordCount 50
    powershell -ExecutionPolicy Bypass -File tools\test_loix.ps1 -DelayMs 0
.PARAMETERS
    -WordCount  Number of words per scenario (default: 30)
    -DelayMs    Inter-key delay in ms (default: 0 — max speed)
    -WordGapMs  Delay after each space/word commit (default: 5)
#>

param(
    [int]$WordCount = 30,
    [int]$DelayMs   = 0,
    [int]$WordGapMs = 5
)

$ErrorActionPreference = "Stop"

# --- Win32 SendInput P/Invoke -------------------------------------------

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class K {
    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT {
        public ushort wVk;
        public ushort wScan;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MOUSEINPUT {
        public int dx; public int dy; public uint mouseData;
        public uint dwFlags; public uint time; public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct HARDWAREINPUT {
        public uint uMsg; public ushort wParamL; public ushort wParamH;
    }

    [StructLayout(LayoutKind.Explicit)]
    public struct INPUT_UNION {
        [FieldOffset(0)] public KEYBDINPUT ki;
        [FieldOffset(0)] public MOUSEINPUT mi;
        [FieldOffset(0)] public HARDWAREINPUT hi;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct INPUT {
        public uint type;
        public INPUT_UNION u;
    }

    public const uint INPUT_KEYBOARD = 1;
    public const uint KEYEVENTF_KEYUP = 0x0002;

    [DllImport("user32.dll", SetLastError = true)]
    public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string lpClassName, string lpWindowName);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowExW(IntPtr hWndParent, IntPtr hWndChildAfter,
                                               string lpszClass, string lpszWindow);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int SendMessageW(IntPtr hWnd, uint Msg, IntPtr wParam,
                                           StringBuilder lParam);

    [DllImport("user32.dll")]
    public static extern int SendMessageW(IntPtr hWnd, uint Msg, IntPtr wParam,
                                           IntPtr lParam);

    private static int _sz = Marshal.SizeOf(typeof(INPUT));

    /// <summary>Send key down + key up as a single SendInput batch (fastest).</summary>
    public static void Key(ushort vk) {
        INPUT[] inp = new INPUT[2];
        inp[0].type = INPUT_KEYBOARD;
        inp[0].u.ki.wVk = vk;
        inp[1].type = INPUT_KEYBOARD;
        inp[1].u.ki.wVk = vk;
        inp[1].u.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inp, _sz);
    }

    /// <summary>Batch-send an entire word as one giant SendInput call.</summary>
    public static void SendWordBatch(ushort[] vks) {
        INPUT[] inp = new INPUT[vks.Length * 2];
        for (int i = 0; i < vks.Length; i++) {
            inp[i * 2].type = INPUT_KEYBOARD;
            inp[i * 2].u.ki.wVk = vks[i];
            inp[i * 2 + 1].type = INPUT_KEYBOARD;
            inp[i * 2 + 1].u.ki.wVk = vks[i];
            inp[i * 2 + 1].u.ki.dwFlags = KEYEVENTF_KEYUP;
        }
        SendInput((uint)inp.Length, inp, _sz);
    }
}
"@ -ErrorAction Stop

# --- VK map --------------------------------------------------------------

$VK = @{}
for ($c = [int][char]'a'; $c -le [int][char]'z'; $c++) {
    $VK[[char]$c] = [uint16]($c - 32)
}
$VK[' '] = [uint16]0x20

$VK_RETURN = [uint16]0x0D
$VK_DELETE = [uint16]0x2E
$VK_CTRL   = [uint16]0x11
$VK_A      = [uint16]0x41

# --- Helpers --------------------------------------------------------------

function Clear-Notepad {
    [K]::Key($VK_CTRL)   # release any stuck modifier
    Start-Sleep -Milliseconds 50
    # Ctrl+A → Delete
    $inp = [K+INPUT[]]::new(4)
    $inp[0].type = [K]::INPUT_KEYBOARD; $inp[0].u.ki.wVk = $VK_CTRL
    $inp[1].type = [K]::INPUT_KEYBOARD; $inp[1].u.ki.wVk = $VK_A
    $inp[2].type = [K]::INPUT_KEYBOARD; $inp[2].u.ki.wVk = $VK_A; $inp[2].u.ki.dwFlags = [K]::KEYEVENTF_KEYUP
    $inp[3].type = [K]::INPUT_KEYBOARD; $inp[3].u.ki.wVk = $VK_CTRL; $inp[3].u.ki.dwFlags = [K]::KEYEVENTF_KEYUP
    [K]::SendInput(4, $inp, [System.Runtime.InteropServices.Marshal]::SizeOf([type][K+INPUT])) | Out-Null
    Start-Sleep -Milliseconds 100
    [K]::Key($VK_DELETE)
    Start-Sleep -Milliseconds 200
}

function Get-NotepadText([IntPtr]$editHwnd) {
    $WM_GETTEXTLENGTH = 0x000E
    $WM_GETTEXT       = 0x000D
    $len = [K]::SendMessageW($editHwnd, $WM_GETTEXTLENGTH, [IntPtr]::Zero, [IntPtr]::Zero)
    if ($len -eq 0) { return "" }
    $sb = New-Object System.Text.StringBuilder ($len + 1)
    [K]::SendMessageW($editHwnd, $WM_GETTEXT, [IntPtr]($len + 1), $sb) | Out-Null
    return $sb.ToString()
}

function Send-Scenario {
    param(
        [string]$Keys,         # e.g. "looix"
        [int]$Count,
        [int]$InterKeyMs,
        [int]$WordGapMs
    )

    # Build VK array for: <keys> + <space>
    $chars = $Keys.ToCharArray() + @(' ')
    $vks = [uint16[]]::new($chars.Length)
    for ($i = 0; $i -lt $chars.Length; $i++) {
        $vks[$i] = $VK[$chars[$i]]
    }

    for ($w = 0; $w -lt $Count; $w++) {
        if ($InterKeyMs -le 0) {
            # Fastest path: batch all key events in one SendInput call
            [K]::SendWordBatch($vks)
        } else {
            foreach ($v in $vks) {
                [K]::Key($v)
                Start-Sleep -Milliseconds $InterKeyMs
            }
        }
        if ($WordGapMs -gt 0) {
            Start-Sleep -Milliseconds $WordGapMs
        }
    }
}

# --- Find / open Notepad --------------------------------------------------

$proc = Get-Process -Name "notepad","Notepad" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) {
    Write-Host "Opening Notepad..." -ForegroundColor DarkGray
    Start-Process notepad.exe
    Start-Sleep -Seconds 2
    $proc = Get-Process -Name "notepad","Notepad" -ErrorAction SilentlyContinue | Select-Object -First 1
}
if (-not $proc) { throw "Cannot find Notepad process" }
$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { throw "Notepad has no window handle" }

# Find edit control
$edit = [IntPtr]::Zero
foreach ($cls in @("Edit", "RichEditD2DPT", "RICHEDIT50W", "RichEdit20WPT")) {
    $edit = [K]::FindWindowExW($hwnd, [IntPtr]::Zero, $cls, $null)
    if ($edit -ne [IntPtr]::Zero) { break }
}
if ($edit -eq [IntPtr]::Zero) { $edit = $hwnd }

[K]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 500

# --- Scenarios ------------------------------------------------------------

# Build expected string from Unicode codepoints to avoid PS 5.1 encoding issues
# l (0x6C) + o-circumflex (0xF4) + i-tilde (0x0129)
$Expected = "l" + [char]0xF4 + [char]0x0129
$scenarios = @(
    @{ Name = "l-o-o-i-x (looix)"; Keys = "looix" },
    @{ Name = "l-o-i-x-o (loixo)"; Keys = "loixo" },
    @{ Name = "l-o-i-o-x (loiox)"; Keys = "loiox" }
)

Write-Host ""
Write-Host ("=" * 72) -ForegroundColor Cyan
Write-Host "  STRESS TEST: ""$Expected""  |  $WordCount words/scenario  |  key-delay=${DelayMs}ms  word-gap=${WordGapMs}ms" -ForegroundColor Cyan
Write-Host ("=" * 72) -ForegroundColor Cyan
Write-Host ""
Write-Host "  Starting in 2 seconds... (don't touch keyboard!)" -ForegroundColor Yellow
Start-Sleep -Seconds 2

$totalPass = 0
$totalFail = 0
$failDetails = @()

foreach ($sc in $scenarios) {
    $name = $sc.Name
    $keys = $sc.Keys

    Write-Host ""
    Write-Host ("--- Scenario: $name ---") -ForegroundColor Yellow

    # Clear notepad
    Clear-Notepad
    Start-Sleep -Milliseconds 300

    # Type
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    Send-Scenario -Keys $keys -Count $WordCount -InterKeyMs $DelayMs -WordGapMs $WordGapMs
    $sw.Stop()

    # Wait for IME to finish processing
    Start-Sleep -Milliseconds 500

    # Read output
    $text = Get-NotepadText $edit
    $text = $text.Trim()

    # Parse words
    $words = $text -split '\s+' | Where-Object { $_ -ne "" }
    $passCount = 0
    $failCount = 0
    $badWords  = @()

    for ($i = 0; $i -lt $words.Count; $i++) {
        if ($words[$i] -eq $Expected) {
            $passCount++
        } else {
            $failCount++
            if ($badWords.Count -lt 10) {
                $badWords += "[#$($i+1)] ""$($words[$i])"""
            }
        }
    }

    $totalPass += $passCount
    $totalFail += $failCount

    $wps = if ($sw.Elapsed.TotalSeconds -gt 0) {
        [math]::Round($WordCount / $sw.Elapsed.TotalSeconds, 1)
    } else { 0 }

    $colour = if ($failCount -eq 0) { "Green" } else { "Red" }
    Write-Host ("  Words typed: {0}  |  Time: {1:F0}ms  |  Speed: {2} words/sec" -f `
        $words.Count, $sw.Elapsed.TotalMilliseconds, $wps) -ForegroundColor DarkGray
    Write-Host ("  PASS: {0}  FAIL: {1}" -f $passCount, $failCount) -ForegroundColor $colour

    if ($failCount -gt 0) {
        Write-Host ("  Bad words: {0}" -f ($badWords -join ", ")) -ForegroundColor Red
        $failDetails += [PSCustomObject]@{
            Scenario  = $name
            Failures  = $failCount
            Examples  = ($badWords -join ", ")
            RawOutput = $text
        }
    }
}

# --- Summary ---------------------------------------------------------------

Write-Host ""
Write-Host ("=" * 72) -ForegroundColor Cyan
Write-Host "  SUMMARY" -ForegroundColor Cyan
Write-Host ("=" * 72) -ForegroundColor Cyan
Write-Host ("  Total words: {0}" -f ($totalPass + $totalFail))
Write-Host ("  PASS: {0}   FAIL: {1}" -f $totalPass, $totalFail) -ForegroundColor $(if ($totalFail -eq 0) { "Green" } else { "Red" })

if ($failDetails.Count -gt 0) {
    Write-Host ""
    Write-Host "  Failed scenarios:" -ForegroundColor Red
    foreach ($fd in $failDetails) {
        Write-Host ("    {0}  --  {1} failures" -f $fd.Scenario, $fd.Failures) -ForegroundColor Red
        Write-Host ("      Examples: {0}" -f $fd.Examples) -ForegroundColor DarkGray
    }
    Write-Host ""
    # Dump raw output for debugging
    Write-Host "  Raw output per failed scenario:" -ForegroundColor Yellow
    foreach ($fd in $failDetails) {
        Write-Host ("    [{0}]" -f $fd.Scenario) -ForegroundColor Yellow
        Write-Host ("    {0}" -f $fd.RawOutput) -ForegroundColor DarkGray
    }
}

Write-Host ("=" * 72) -ForegroundColor Cyan
Write-Host ""

if ($totalFail -gt 0) { exit 1 }
exit 0
