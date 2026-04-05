#Requires -Version 5.1
<#
.SYNOPSIS
    NexusKey vs UniKey — End-to-End IME Benchmark (PowerShell)
.DESCRIPTION
    Measures keystroke-to-commit latency by injecting keystrokes into Notepad.
    Run once with NexusKey, once with UniKey, compare results.
.USAGE
    1. Switch to the IME you want to test
    2. Run: .\benchmark_ime.ps1 -IME "NexusKey"
    3. Switch IME, run: .\benchmark_ime.ps1 -IME "UniKey"
    4. Compare: .\benchmark_ime.ps1 -Compare
#>

param(
    [string]$IME = "",
    [int]$Rounds = 5,
    [int]$DelayMs = 80,
    [switch]$Compare,
    [switch]$NoEnglish
)

# --- Win32 SendInput via P/Invoke ----------------------------------------

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class NativeMethods {
    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT {
        public ushort wVk;
        public ushort wScan;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Explicit)]
    public struct INPUT_UNION {
        [FieldOffset(0)] public KEYBDINPUT ki;
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

    [DllImport("user32.dll")]
    public static extern IntPtr FindWindowW(string lpClassName, string lpWindowName);

    [DllImport("user32.dll")]
    public static extern IntPtr FindWindowExW(IntPtr hWndParent, IntPtr hWndChildAfter, string lpszClass, string lpszWindow);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int SendMessageW(IntPtr hWnd, uint Msg, IntPtr wParam, System.Text.StringBuilder lParam);

    [DllImport("user32.dll")]
    public static extern int SendMessageW(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

    public static void SendKey(ushort vk) {
        INPUT[] inputs = new INPUT[2];

        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].u.ki.wVk = vk;
        inputs[0].u.ki.dwFlags = 0;

        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].u.ki.wVk = vk;
        inputs[1].u.ki.dwFlags = KEYEVENTF_KEYUP;

        SendInput(2, inputs, Marshal.SizeOf(typeof(INPUT)));
    }

    public static void SendKeyDown(ushort vk) {
        INPUT[] inputs = new INPUT[1];
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].u.ki.wVk = vk;
        inputs[0].u.ki.dwFlags = 0;
        SendInput(1, inputs, Marshal.SizeOf(typeof(INPUT)));
    }

    public static void SendKeyUp(ushort vk) {
        INPUT[] inputs = new INPUT[1];
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].u.ki.wVk = vk;
        inputs[0].u.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, inputs, Marshal.SizeOf(typeof(INPUT)));
    }
}
"@ -ErrorAction SilentlyContinue

# --- Helper functions ----------------------------------------------------

$VK = @{}
for ($c = [int][char]'a'; $c -le [int][char]'z'; $c++) {
    $VK[[char]$c] = [uint16]($c - 32)  # VK_A=0x41
}
for ($c = [int][char]'0'; $c -le [int][char]'9'; $c++) {
    $VK[[char]$c] = [uint16]$c
}
$VK[' '] = [uint16]0x20
$VK[','] = [uint16]0xBC
$VK['.'] = [uint16]0xBE
$VK['['] = [uint16]0xDB
$VK[']'] = [uint16]0xDD

$VK_RETURN = [uint16]0x0D
$VK_DELETE = [uint16]0x2E
$VK_CTRL   = [uint16]0x11
$VK_A      = [uint16]0x41

function Send-Char([char]$c) {
    $lower = [char]::ToLower($c)
    $code = $VK[$lower]
    if ($null -ne $code) {
        [NativeMethods]::SendKey($code)
    }
}

function Send-Word([string]$word) {
    foreach ($c in $word.ToCharArray()) {
        Send-Char $c
    }
}

function Clear-Notepad {
    # Ctrl+A then Delete
    [NativeMethods]::SendKeyDown($VK_CTRL)
    [NativeMethods]::SendKey($VK_A)
    [NativeMethods]::SendKeyUp($VK_CTRL)
    Start-Sleep -Milliseconds 100
    [NativeMethods]::SendKey($VK_DELETE)
    Start-Sleep -Milliseconds 200
}

function Get-NotepadText([IntPtr]$editHwnd) {
    $WM_GETTEXTLENGTH = 0x000E
    $WM_GETTEXT = 0x000D
    $len = [NativeMethods]::SendMessageW($editHwnd, $WM_GETTEXTLENGTH, [IntPtr]::Zero, [IntPtr]::Zero)
    if ($len -eq 0) { return "" }
    $sb = New-Object System.Text.StringBuilder ($len + 1)
    [NativeMethods]::SendMessageW($editHwnd, $WM_GETTEXT, [IntPtr]($len + 1), $sb) | Out-Null
    return $sb.ToString()
}

# --- Benchmark data ------------------------------------------------------

$TelexWords = @(
    "xin",        # xin
    "chaof",      # chao`
    "tooi",       # toi^
    "laaf",       # la`
    "mootj",      # mo^.t
    "laapj",      # la^.p
    "trinhf",     # tri`nh
    "vieen",      # vie^n
    "vieejt",     # vie^.t
    "nam",        # nam
    "ddaay",      # d-a^y
    "nguwowif",   # nguoi`
    "thuwf",      # thu*`
    "nghieem",    # nghie^m
    "truwowngf",  # truong`
    "hoocj",      # ho.c
    "ddoongj",    # d-o^.ng
    "baawn",      # ba^?n
    "coongs",     # co^ng'
    "nghees"      # nghe^.
)

$EnglishWords = @(
    "the", "quick", "brown", "fox", "jumps",
    "over", "the", "lazy", "dog", "today",
    "code", "test", "build", "fast", "run",
    "type", "word", "line", "file", "save"
)

# --- Compare mode --------------------------------------------------------

if ($Compare) {
    $files = Get-ChildItem -Path (Get-Location) -Filter "benchmark_*.json" | Sort-Object LastWriteTime
    if ($files.Count -lt 2) {
        Write-Host "`nNeed at least 2 benchmark result files to compare." -ForegroundColor Red
        Write-Host "Run the benchmark with two different IMEs first."
        exit 1
    }

    $results = $files | ForEach-Object { Get-Content $_.FullName | ConvertFrom-Json }

    # Build header
    $nameWidth = 20
    $colWidth = 14
    $header = "{0,-$nameWidth}" -f ""
    $separator = "{0,-$nameWidth}" -f ""
    foreach ($r in $results) {
        $header += ("{0,$colWidth}" -f $r.ime)
        $separator += ("{0,$colWidth}" -f ("-" * ($colWidth - 2)))
    }

    Write-Host ""
    Write-Host ("=" * ($nameWidth + $colWidth * $results.Count)) -ForegroundColor Cyan
    Write-Host "  IME BENCHMARK COMPARISON ($($results.Count) runs)" -ForegroundColor Cyan
    Write-Host ("=" * ($nameWidth + $colWidth * $results.Count)) -ForegroundColor Cyan
    Write-Host ""
    Write-Host $header
    Write-Host $separator

    # Vietnamese row
    $vnRow = "{0,-$nameWidth}" -f "Vietnamese (us/key)"
    $vnValues = @()
    foreach ($r in $results) {
        $val = $r.vietnamese.avg_per_key_us
        $vnValues += $val
        $vnRow += ("{0,$($colWidth - 3):F1} us" -f $val)
    }
    Write-Host $vnRow

    # English row
    $enRow = "{0,-$nameWidth}" -f "English (us/key)"
    $enValues = @()
    foreach ($r in $results) {
        if ($null -ne $r.english) {
            $val = $r.english.avg_per_key_us
            $enValues += $val
            $enRow += ("{0,$($colWidth - 3):F1} us" -f $val)
        } else {
            $enValues += 0
            $enRow += ("{0,$colWidth}" -f "N/A")
        }
    }
    Write-Host $enRow

    # Vietnamese total ms row
    $vnMsRow = "{0,-$nameWidth}" -f "Vietnamese (total ms)"
    foreach ($r in $results) {
        $vnMsRow += ("{0,$($colWidth - 3):F1} ms" -f $r.vietnamese.avg_ms)
    }
    Write-Host $vnMsRow

    # English total ms row
    $enMsRow = "{0,-$nameWidth}" -f "English (total ms)"
    foreach ($r in $results) {
        if ($null -ne $r.english) {
            $enMsRow += ("{0,$($colWidth - 3):F1} ms" -f $r.english.avg_ms)
        } else {
            $enMsRow += ("{0,$colWidth}" -f "N/A")
        }
    }
    Write-Host $enMsRow

    # Find best/worst for Vietnamese and English
    Write-Host ""
    $vnMin = ($vnValues | Measure-Object -Minimum).Minimum
    $vnMax = ($vnValues | Measure-Object -Maximum).Maximum
    $vnBestIdx = [array]::IndexOf($vnValues, $vnMin)
    $vnWorstIdx = [array]::IndexOf($vnValues, $vnMax)
    $vnPct = [math]::Round(($vnMax - $vnMin) / $vnMax * 100, 1)
    Write-Host ("  Vietnamese best:  {0} ({1:F1} us)" -f $results[$vnBestIdx].ime, $vnMin) -ForegroundColor Green
    Write-Host ("  Vietnamese worst: {0} ({1:F1} us)  [{2:F1}% gap]" -f $results[$vnWorstIdx].ime, $vnMax, $vnPct) -ForegroundColor Yellow

    if ($enValues.Count -gt 0 -and ($enValues | Where-Object { $_ -gt 0 }).Count -ge 2) {
        $enFiltered = $enValues | Where-Object { $_ -gt 0 }
        $enMin = ($enFiltered | Measure-Object -Minimum).Minimum
        $enMax = ($enFiltered | Measure-Object -Maximum).Maximum
        $enBestIdx = [array]::IndexOf($enValues, $enMin)
        $enWorstIdx = [array]::IndexOf($enValues, $enMax)
        $enPct = [math]::Round(($enMax - $enMin) / $enMax * 100, 1)
        Write-Host ("  English best:     {0} ({1:F1} us)" -f $results[$enBestIdx].ime, $enMin) -ForegroundColor Green
        Write-Host ("  English worst:    {0} ({1:F1} us)  [{2:F1}% gap]" -f $results[$enWorstIdx].ime, $enMax, $enPct) -ForegroundColor Yellow
    }

    Write-Host ""
    exit 0
}

# --- Main benchmark ------------------------------------------------------

if (-not $IME) {
    $IME = Read-Host "Enter IME name (e.g. NexusKey, UniKey)"
    if (-not $IME) { $IME = "Unknown" }
}

Write-Host ""
Write-Host "--- IME Benchmark: $IME ---" -ForegroundColor Cyan
Write-Host "Rounds: $Rounds | Delay: ${DelayMs}ms between words"
Write-Host ""

# Find or open Notepad
$proc = Get-Process -Name "notepad","Notepad" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) {
    Write-Host "Opening Notepad..."
    Start-Process notepad.exe
    Start-Sleep -Seconds 3
    $proc = Get-Process -Name "notepad","Notepad" -ErrorAction SilentlyContinue | Select-Object -First 1
}

if (-not $proc) {
    Write-Host "ERROR: Cannot find Notepad process!" -ForegroundColor Red
    exit 1
}

$proc.Refresh()
$hwnd = $proc.MainWindowHandle
Write-Host "  Notepad PID: $($proc.Id)  HWND: $hwnd"

if ($hwnd -eq [IntPtr]::Zero) {
    Write-Host "ERROR: Notepad has no window handle!" -ForegroundColor Red
    exit 1
}

# Find edit control (for reading text back)
$edit = [IntPtr]::Zero
foreach ($cls in @("Edit", "RichEditD2DPT", "RICHEDIT50W", "RichEdit20WPT")) {
    $edit = [NativeMethods]::FindWindowExW($hwnd, [IntPtr]::Zero, $cls, $null)
    if ($edit -ne [IntPtr]::Zero) { break }
}
if ($edit -eq [IntPtr]::Zero) { $edit = $hwnd }

# Focus
[NativeMethods]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 500

Write-Host "Starting in 2 seconds... (don't touch keyboard!)" -ForegroundColor Yellow
Start-Sleep -Seconds 2

Clear-Notepad

# --- Benchmark function --------------------------------------------------

function Run-Benchmark([string[]]$words, [string]$label, [int]$rounds, [int]$delayMs) {
    $results = @()
    $totalKeys = ($words | ForEach-Object { $_.Length + 1 } | Measure-Object -Sum).Sum

    for ($r = 1; $r -le $rounds; $r++) {
        $sw = [System.Diagnostics.Stopwatch]::new()
        $wordTimes = @()

        foreach ($word in $words) {
            $sw.Restart()
            Send-Word $word
            [NativeMethods]::SendKey($VK[' '])  # Space to commit
            $sw.Stop()
            $wordTimes += $sw.Elapsed.TotalMilliseconds
            Start-Sleep -Milliseconds $delayMs
        }

        $totalMs = ($wordTimes | Measure-Object -Sum).Sum
        $perKeyUs = ($totalMs / $totalKeys) * 1000

        $results += [PSCustomObject]@{
            Round      = $r
            TotalMs    = [math]::Round($totalMs, 2)
            PerKeyUs   = [math]::Round($perKeyUs, 1)
            TotalKeys  = $totalKeys
            WordTimes  = $wordTimes
        }

        # New line between rounds
        [NativeMethods]::SendKey($VK_RETURN)
        Start-Sleep -Milliseconds 300
    }

    # Print results
    Write-Host ""
    Write-Host ("=" * 60) -ForegroundColor Cyan
    Write-Host "  $label" -ForegroundColor Cyan
    Write-Host ("=" * 60) -ForegroundColor Cyan

    foreach ($r in $results) {
        Write-Host ("  Round {0}: {1,8:F2} ms  ({2,6:F1} us/key, {3} keys)" -f $r.Round, $r.TotalMs, $r.PerKeyUs, $r.TotalKeys)
    }

    $avgMs = ($results.TotalMs | Measure-Object -Average).Average
    $avgPerKey = ($results.PerKeyUs | Measure-Object -Average).Average
    $minMs = ($results.TotalMs | Measure-Object -Minimum).Minimum
    $maxMs = ($results.TotalMs | Measure-Object -Maximum).Maximum

    Write-Host ("  " + ("-" * 56))
    Write-Host ("  Average:  {0,8:F2} ms  ({1,6:F1} us/key)" -f $avgMs, $avgPerKey) -ForegroundColor Green
    Write-Host ("  Best:     {0,8:F2} ms" -f $minMs)
    Write-Host ("  Worst:    {0,8:F2} ms" -f $maxMs)

    return @{
        label          = $label
        avg_ms         = [math]::Round($avgMs, 2)
        avg_per_key_us = [math]::Round($avgPerKey, 1)
        min_ms         = [math]::Round($minMs, 2)
        max_ms         = [math]::Round($maxMs, 2)
        rounds         = $rounds
    }
}

# --- Run benchmarks ------------------------------------------------------

Write-Host "`nRunning Vietnamese typing benchmark..." -ForegroundColor Yellow
$vnResult = Run-Benchmark -words $TelexWords -label "Vietnamese Telex - $IME" -rounds $Rounds -delayMs $DelayMs

$enResult = $null
if (-not $NoEnglish) {
    Start-Sleep -Milliseconds 500
    [NativeMethods]::SendKey($VK_RETURN)
    [NativeMethods]::SendKey($VK_RETURN)
    Start-Sleep -Milliseconds 300

    Write-Host "`nRunning English typing benchmark..." -ForegroundColor Yellow
    $enResult = Run-Benchmark -words $EnglishWords -label "English - $IME" -rounds $Rounds -delayMs $DelayMs
}

# --- Verify output -------------------------------------------------------

Start-Sleep -Milliseconds 500
$text = Get-NotepadText $edit
if ($text) {
    Write-Host ("`n  Notepad: {0} chars typed" -f $text.Length)
}

# --- Save JSON -----------------------------------------------------------

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$filename = "benchmark_{0}_{1}.json" -f ($IME.ToLower() -replace ' ', '_'), $timestamp
$filepath = Join-Path (Get-Location) $filename

$report = @{
    ime        = $IME
    date       = (Get-Date).ToString("o")
    config     = @{ rounds = $Rounds; delay_ms = $DelayMs }
    vietnamese = $vnResult
    english    = $enResult
} | ConvertTo-Json -Depth 5

$report | Out-File -FilePath $filepath -Encoding utf8
Write-Host "`n  Results saved: $filename" -ForegroundColor Green

# --- Summary -------------------------------------------------------------

Write-Host ""
Write-Host ("=" * 60) -ForegroundColor Cyan
Write-Host "  SUMMARY - $IME" -ForegroundColor Cyan
Write-Host ("=" * 60) -ForegroundColor Cyan
Write-Host ("  Vietnamese: {0,8:F2} ms avg ({1,5:F1} us/key)" -f $vnResult.avg_ms, $vnResult.avg_per_key_us)
if ($enResult) {
    Write-Host ("  English:    {0,8:F2} ms avg ({1,5:F1} us/key)" -f $enResult.avg_ms, $enResult.avg_per_key_us)
}
Write-Host ("=" * 60) -ForegroundColor Cyan
Write-Host ""
Write-Host "Switch to the other IME and run again to compare!" -ForegroundColor Yellow
Write-Host "Then run: .\benchmark_ime.ps1 -Compare" -ForegroundColor Yellow
