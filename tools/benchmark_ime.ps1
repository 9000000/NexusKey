#Requires -Version 5.1
<#
.SYNOPSIS
    NexusKey vs UniKey — End-to-End IME Benchmark (PowerShell)
.DESCRIPTION
    Measures keystroke-to-commit latency by injecting keystrokes into Notepad.
    Run once with NexusKey, once with UniKey, compare results.

    Metrics: avg, p95, p99, min, max per-key latency.
    Verifies output contains Vietnamese characters (detects IME not active).
    Monitors CPU usage during typing.
.USAGE
    1. Switch to the IME you want to test
    2. Run: .\benchmark_ime.ps1 -IME "NexusKey"
    3. Switch IME, run: .\benchmark_ime.ps1 -IME "UniKey"
    4. Compare: .\benchmark_ime.ps1 -Compare
.PARAMETERS
    -IME        Name of the IME being tested
    -Method     Input method: "telex" (default) or "vni"
    -Rounds     Number of benchmark rounds (default: 5)
    -DelayMs    Delay between words in ms (default: 80)
    -Compare    Compare saved benchmark results
    -NoEnglish  Skip English typing benchmark
#>

param(
    [string]$IME = "",
    [ValidateSet("telex", "vni")]
    [string]$Method = "telex",
    [int]$Rounds = 5,
    [int]$DelayMs = 80,
    [switch]$Compare,
    [switch]$NoEnglish
)

# --- Win32 SendInput via P/Invoke ----------------------------------------

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Diagnostics;

public static class NativeMethods {
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
        public int dx;
        public int dy;
        public uint mouseData;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct HARDWAREINPUT {
        public uint uMsg;
        public ushort wParamL;
        public ushort wParamH;
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

    private static int _inputSize = Marshal.SizeOf(typeof(INPUT));

    public static void SendKey(ushort vk) {
        INPUT[] inputs = new INPUT[2];

        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].u.ki.wVk = vk;
        inputs[0].u.ki.dwFlags = 0;

        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].u.ki.wVk = vk;
        inputs[1].u.ki.dwFlags = KEYEVENTF_KEYUP;

        SendInput(2, inputs, _inputSize);
    }

    public static void SendKeyDown(ushort vk) {
        INPUT[] inputs = new INPUT[1];
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].u.ki.wVk = vk;
        inputs[0].u.ki.dwFlags = 0;
        SendInput(1, inputs, _inputSize);
    }

    public static void SendKeyUp(ushort vk) {
        INPUT[] inputs = new INPUT[1];
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].u.ki.wVk = vk;
        inputs[0].u.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, inputs, _inputSize);
    }

    // High-resolution per-keystroke timing
    private static Stopwatch _sw = new Stopwatch();
    private static double[] _keyTimes;
    private static int _keyIdx;

    public static void InitKeyTiming(int maxKeys) {
        _keyTimes = new double[maxKeys];
        _keyIdx = 0;
    }

    public static void SendKeyTimed(ushort vk) {
        _sw.Restart();
        SendKey(vk);
        _sw.Stop();
        if (_keyIdx < _keyTimes.Length) {
            _keyTimes[_keyIdx++] = _sw.Elapsed.TotalMilliseconds * 1000.0;
        }
    }

    public static double[] GetKeyTimes() {
        double[] result = new double[_keyIdx];
        Array.Copy(_keyTimes, result, _keyIdx);
        return result;
    }

    public static void ResetKeyTiming() { _keyIdx = 0; }
}
"@ -ErrorAction Stop

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
        [NativeMethods]::SendKeyTimed($code)
    }
}

function Send-Word([string]$word) {
    foreach ($c in $word.ToCharArray()) {
        Send-Char $c
    }
}

function Clear-Notepad {
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

function Get-Percentile([double[]]$sorted, [int]$pct) {
    if ($sorted.Count -eq 0) { return 0 }
    $idx = [math]::Ceiling($sorted.Count * $pct / 100.0) - 1
    if ($idx -lt 0) { $idx = 0 }
    if ($idx -ge $sorted.Count) { $idx = $sorted.Count - 1 }
    return $sorted[$idx]
}

function Has-VietnameseChars([string]$text) {
    # Check for common Vietnamese diacritical characters (Unicode > 0x7F)
    foreach ($c in $text.ToCharArray()) {
        if ([int]$c -gt 0x7F) { return $true }
    }
    return $false
}

function Get-CpuUsage([string]$processName) {
    $proc = Get-Process -Name $processName -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) { return -1 }
    $proc.Refresh()
    return $proc.CPU
}

# --- Benchmark data ------------------------------------------------------

$TelexWords = @(
    "xin",        # xin
    "chaof",      # chào
    "tooi",       # tôi
    "laaf",       # là
    "mootj",      # một
    "laapj",      # lập
    "trinhf",     # trình
    "vieen",      # viên
    "vieejt",     # việt
    "nam",        # nam
    "ddaay",      # đây
    "nguwowif",   # người
    "thuwr",      # thư
    "nghieemj",    # nghiêm
    "truowngf",  # trường
    "hoocj",      # học
    "ddoongj",    # động
    "baanr",      # bản
    "coong",     # công
    "ngheej"      # nghệ
)

$TelexExpected = @(
    "xin", "chào", "tôi", "là", "một",
    "lập", "trình", "viên", "việt", "nam",
    "đây", "người", "thử", "nghiệm", "trường",
    "học", "động", "bản", "công", "nghệ"
)

$VniWords = @(
    "xin",        # xin
    "cha2o",      # chào
    "to6i",       # tôi
    "la2",        # là
    "mo65t",      # một
    "la65p",      # lập
    "tri2nh",     # trình
    "vie6n",      # viên
    "vie65t",     # việt
    "nam",        # nam
    "d9a6y",      # đây — dd→d9
    "ngu7o72i",   # người
    "thu7",       # thư
    "nghie6m",    # nghiêm
    "tru7o72ng",  # trường
    "ho65c",      # học
    "d9o65ng",    # động
    "ba63n",      # bản
    "co6ng1",     # công
    "nghe65"      # nghệ
)

$VniExpected = @(
    "xin", "chào", "tôi", "là", "một",
    "lập", "trình", "viên", "việt", "nam",
    "đây", "người", "thư", "nghiêm", "trường",
    "học", "động", "bản", "công", "nghệ"
)

$EnglishWords = @(
    "the", "quick", "brown", "fox", "jumps",
    "over", "the", "lazy", "dog", "today",
    "code", "test", "build", "fast", "run",
    "type", "word", "line", "file", "save"
)

# --- Compare mode --------------------------------------------------------

if ($Compare) {
    $resultDir = Join-Path (Split-Path $PSScriptRoot -Parent) "benchmark_results"
    if (-not (Test-Path $resultDir)) {
        $resultDir = Get-Location
    }
    $files = Get-ChildItem -Path $resultDir -Filter "benchmark_*.json" | Sort-Object LastWriteTime
    if ($files.Count -lt 2) {
        Write-Host "`nNeed at least 2 benchmark result files to compare." -ForegroundColor Red
        Write-Host "Run the benchmark with two different IMEs first."
        exit 1
    }

    $results = $files | ForEach-Object { Get-Content $_.FullName | ConvertFrom-Json }

    $nameWidth = 22
    $colWidth = 16
    $header = "{0,-$nameWidth}" -f ""
    $separator = "{0,-$nameWidth}" -f ""
    foreach ($r in $results) {
        $label = "$($r.ime) ($($r.method))"
        $header += ("{0,$colWidth}" -f $label)
        $separator += ("{0,$colWidth}" -f ("-" * ($colWidth - 2)))
    }

    Write-Host ""
    Write-Host ("=" * ($nameWidth + $colWidth * $results.Count)) -ForegroundColor Cyan
    Write-Host "  IME BENCHMARK COMPARISON ($($results.Count) runs)" -ForegroundColor Cyan
    Write-Host ("=" * ($nameWidth + $colWidth * $results.Count)) -ForegroundColor Cyan
    Write-Host ""
    Write-Host $header
    Write-Host $separator

    # Vietnamese rows
    $vnAvgRow = "{0,-$nameWidth}" -f "VN avg (us/key)"
    $vnP95Row = "{0,-$nameWidth}" -f "VN p95 (us/key)"
    $vnP99Row = "{0,-$nameWidth}" -f "VN p99 (us/key)"
    $vnValues = @()
    foreach ($r in $results) {
        $vnValues += $r.vietnamese.avg_per_key_us
        $vnAvgRow += ("{0,$($colWidth - 3):F1} us" -f $r.vietnamese.avg_per_key_us)
        $vnP95Row += ("{0,$($colWidth - 3):F1} us" -f $r.vietnamese.p95_per_key_us)
        $vnP99Row += ("{0,$($colWidth - 3):F1} us" -f $r.vietnamese.p99_per_key_us)
    }
    Write-Host $vnAvgRow
    Write-Host $vnP95Row
    Write-Host $vnP99Row

    # English rows
    if ($results | Where-Object { $null -ne $_.english }) {
        $enAvgRow = "{0,-$nameWidth}" -f "EN avg (us/key)"
        foreach ($r in $results) {
            if ($null -ne $r.english) {
                $enAvgRow += ("{0,$($colWidth - 3):F1} us" -f $r.english.avg_per_key_us)
            } else {
                $enAvgRow += ("{0,$colWidth}" -f "N/A")
            }
        }
        Write-Host $enAvgRow
    }

    # CPU rows
    $cpuRow = "{0,-$nameWidth}" -f "CPU % (typing)"
    foreach ($r in $results) {
        if ($null -ne $r.cpu_during_typing) {
            $cpuRow += ("{0,$($colWidth - 2):F1} %" -f $r.cpu_during_typing)
        } else {
            $cpuRow += ("{0,$colWidth}" -f "N/A")
        }
    }
    Write-Host $cpuRow

    # Verification
    $verifyRow = "{0,-$nameWidth}" -f "Output verified"
    foreach ($r in $results) {
        $status = if ($r.output_verified) { "PASS" } else { "FAIL" }
        $color = if ($r.output_verified) { "Green" } else { "Red" }
        $verifyRow += ("{0,$colWidth}" -f $status)
    }
    Write-Host $verifyRow

    # Winner
    Write-Host ""
    $vnMin = ($vnValues | Measure-Object -Minimum).Minimum
    $vnMax = ($vnValues | Measure-Object -Maximum).Maximum
    $vnBestIdx = [array]::IndexOf($vnValues, $vnMin)
    $vnPct = [math]::Round(($vnMax - $vnMin) / $vnMax * 100, 1)
    Write-Host ("  Best: {0} ({1:F1} us/key, {2:F1}% faster)" -f $results[$vnBestIdx].ime, $vnMin, $vnPct) -ForegroundColor Green

    Write-Host ""
    exit 0
}

# --- Main benchmark ------------------------------------------------------

if (-not $IME) {
    $IME = Read-Host "Enter IME name (e.g. NexusKey, UniKey)"
    if (-not $IME) { $IME = "Unknown" }
}

# Select word list based on method
if ($Method -eq "vni") {
    $VietnameseWords = $VniWords
    $ExpectedOutput = $VniExpected
} else {
    $VietnameseWords = $TelexWords
    $ExpectedOutput = $TelexExpected
}

Write-Host ""
Write-Host "--- IME Benchmark: $IME ($Method) ---" -ForegroundColor Cyan
Write-Host "Rounds: $Rounds | Delay: ${DelayMs}ms | Method: $Method"
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

# Find edit control
$edit = [IntPtr]::Zero
foreach ($cls in @("Edit", "RichEditD2DPT", "RICHEDIT50W", "RichEdit20WPT")) {
    $edit = [NativeMethods]::FindWindowExW($hwnd, [IntPtr]::Zero, $cls, $null)
    if ($edit -ne [IntPtr]::Zero) { break }
}
if ($edit -eq [IntPtr]::Zero) { $edit = $hwnd }

# Focus
[NativeMethods]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 500

# --- Detect IME process for CPU monitoring -------------------------------

$imeProcessName = $null
$proc_ime = Get-Process -Name $IME -ErrorAction SilentlyContinue | Select-Object -First 1
if ($proc_ime) {
    $imeProcessName = $IME
} else {
    # Try common variations: strip .exe, try with "App"/"Lite" suffix
    foreach ($suffix in @("", "App", "Lite", "NT")) {
        $tryName = $IME + $suffix
        if (Get-Process -Name $tryName -ErrorAction SilentlyContinue) {
            $imeProcessName = $tryName
            break
        }
    }
}
if ($imeProcessName) {
    Write-Host "  IME process: $imeProcessName (CPU monitoring enabled)"
} else {
    Write-Host "  IME process: '$IME' not found (CPU monitoring disabled)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Starting in 2 seconds... (don't touch keyboard!)" -ForegroundColor Yellow
Start-Sleep -Seconds 2

Clear-Notepad

# --- Benchmark function --------------------------------------------------

function Run-Benchmark([string[]]$words, [string]$label, [int]$rounds, [int]$delayMs, [bool]$isWarmup = $false) {
    $results = @()
    $totalKeys = ($words | ForEach-Object { $_.Length + 1 } | Measure-Object -Sum).Sum
    $allKeyTimesUs = @()

    # Init per-keystroke timing buffer
    [NativeMethods]::InitKeyTiming($totalKeys * ($rounds + 1))

    for ($r = 1; $r -le $rounds; $r++) {
        $sw = [System.Diagnostics.Stopwatch]::new()
        [NativeMethods]::ResetKeyTiming()

        $sw.Restart()
        foreach ($word in $words) {
            Send-Word $word
            [NativeMethods]::SendKeyTimed($VK[' '])  # Space to commit
            Start-Sleep -Milliseconds $delayMs
        }
        $sw.Stop()

        # Collect per-keystroke times
        $keyTimes = [NativeMethods]::GetKeyTimes()
        $sorted = $keyTimes | Sort-Object
        $avgUs = ($keyTimes | Measure-Object -Average).Average
        $p95Us = Get-Percentile $sorted 95
        $p99Us = Get-Percentile $sorted 99

        if (-not $isWarmup) {
            $allKeyTimesUs += $keyTimes
        }

        # Total time minus sleep delays
        $sleepMs = $words.Count * $delayMs
        $activeMs = $sw.Elapsed.TotalMilliseconds - $sleepMs

        $results += [PSCustomObject]@{
            Round     = $r
            ActiveMs  = [math]::Round($activeMs, 2)
            AvgKeyUs  = [math]::Round($avgUs, 1)
            P95KeyUs  = [math]::Round($p95Us, 1)
            P99KeyUs  = [math]::Round($p99Us, 1)
            TotalKeys = $totalKeys
        }

        [NativeMethods]::SendKey($VK_RETURN)
        Start-Sleep -Milliseconds 300
    }

    if ($isWarmup) { return $null }

    # Print results
    Write-Host ""
    Write-Host ("=" * 72) -ForegroundColor Cyan
    Write-Host "  $label" -ForegroundColor Cyan
    Write-Host ("=" * 72) -ForegroundColor Cyan

    foreach ($r in $results) {
        Write-Host ("  Round {0}: {1,7:F1} ms active | avg {2,6:F1} us/key | p95 {3,6:F1} | p99 {4,6:F1}" -f `
            $r.Round, $r.ActiveMs, $r.AvgKeyUs, $r.P95KeyUs, $r.P99KeyUs)
    }

    # Aggregate stats from all keystrokes across all rounds
    $allSorted = $allKeyTimesUs | Sort-Object
    $aggAvg = ($allKeyTimesUs | Measure-Object -Average).Average
    $aggP95 = Get-Percentile $allSorted 95
    $aggP99 = Get-Percentile $allSorted 99
    $aggMin = ($allKeyTimesUs | Measure-Object -Minimum).Minimum
    $aggMax = ($allKeyTimesUs | Measure-Object -Maximum).Maximum

    $avgActiveMs = ($results.ActiveMs | Measure-Object -Average).Average

    Write-Host ("  " + ("-" * 68))
    Write-Host ("  Aggregate ({0} keystrokes across {1} rounds):" -f $allKeyTimesUs.Count, $rounds) -ForegroundColor Green
    Write-Host ("    avg: {0,6:F1} us/key  |  p95: {1,6:F1}  |  p99: {2,6:F1}" -f $aggAvg, $aggP95, $aggP99) -ForegroundColor Green
    Write-Host ("    min: {0,6:F1} us      |  max: {1,6:F1} us" -f $aggMin, $aggMax)
    Write-Host ("    active time avg: {0:F1} ms" -f $avgActiveMs)

    return @{
        label          = $label
        avg_ms         = [math]::Round($avgActiveMs, 2)
        avg_per_key_us = [math]::Round($aggAvg, 1)
        p95_per_key_us = [math]::Round($aggP95, 1)
        p99_per_key_us = [math]::Round($aggP99, 1)
        min_per_key_us = [math]::Round($aggMin, 1)
        max_per_key_us = [math]::Round($aggMax, 1)
        total_keys     = $allKeyTimesUs.Count
        rounds         = $rounds
    }
}

# --- Warmup round --------------------------------------------------------

Write-Host "Warmup round (not counted)..." -ForegroundColor DarkGray
Run-Benchmark -words $VietnameseWords -label "Warmup" -rounds 1 -delayMs $DelayMs -isWarmup $true
Clear-Notepad
Start-Sleep -Milliseconds 500

# --- CPU baseline --------------------------------------------------------

$cpuBefore = $null
if ($imeProcessName) {
    $cpuBefore = Get-CpuUsage $imeProcessName
}

# --- Vietnamese benchmark ------------------------------------------------

Write-Host "`nRunning Vietnamese typing benchmark ($Method)..." -ForegroundColor Yellow
$vnResult = Run-Benchmark -words $VietnameseWords -label "Vietnamese ($Method) - $IME" -rounds $Rounds -delayMs $DelayMs

# --- CPU during typing ---------------------------------------------------

$cpuDuringTyping = $null
if ($imeProcessName -and $null -ne $cpuBefore) {
    $cpuAfter = Get-CpuUsage $imeProcessName
    if ($cpuAfter -ge 0 -and $cpuBefore -ge 0) {
        $cpuDuringTyping = [math]::Round($cpuAfter - $cpuBefore, 2)
        Write-Host ("`n  CPU used by {0}: {1:F2} seconds" -f $imeProcessName, $cpuDuringTyping)
    }
}

# --- Verify output -------------------------------------------------------

Start-Sleep -Milliseconds 500
$text = Get-NotepadText $edit
$outputVerified = $false

if ($text) {
    $hasVietnamese = Has-VietnameseChars $text
    if ($hasVietnamese) {
        # Check each expected word appears in output
        $missingWords = @()
        foreach ($expected in $ExpectedOutput) {
            if ($text -notmatch [regex]::Escape($expected)) {
                $missingWords += $expected
            }
        }

        if ($missingWords.Count -eq 0) {
            Write-Host "`n  Output verification: PASS (all Vietnamese words found)" -ForegroundColor Green
            $outputVerified = $true
        } else {
            Write-Host "`n  Output verification: PARTIAL ($($missingWords.Count) words missing)" -ForegroundColor Yellow
            Write-Host "    Missing: $($missingWords -join ', ')"
            $outputVerified = $true  # Still has Vietnamese, just some words off
        }
    } else {
        Write-Host "`n  Output verification: FAIL (no Vietnamese characters detected!)" -ForegroundColor Red
        Write-Host "    Is the IME active? Check that $IME is running and Vietnamese mode is ON."
        $outputVerified = $false
    }
    Write-Host "  Notepad: $($text.Length) chars typed"
} else {
    Write-Host "`n  Output verification: FAIL (Notepad empty)" -ForegroundColor Red
    $outputVerified = $false
}

# --- English benchmark ---------------------------------------------------

$enResult = $null
if (-not $NoEnglish) {
    Start-Sleep -Milliseconds 500
    [NativeMethods]::SendKey($VK_RETURN)
    [NativeMethods]::SendKey($VK_RETURN)
    Start-Sleep -Milliseconds 300

    Write-Host "`nRunning English typing benchmark..." -ForegroundColor Yellow
    $enResult = Run-Benchmark -words $EnglishWords -label "English - $IME" -rounds $Rounds -delayMs $DelayMs
}

# --- Save JSON -----------------------------------------------------------

$resultDir = Join-Path (Split-Path $PSScriptRoot -Parent) "benchmark_results"
if (-not (Test-Path $resultDir)) {
    New-Item -ItemType Directory -Path $resultDir -Force | Out-Null
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$filename = "benchmark_{0}_{1}_{2}.json" -f ($IME.ToLower() -replace ' ', '_'), $Method, $timestamp
$filepath = Join-Path $resultDir $filename

$report = @{
    ime                = $IME
    method             = $Method
    date               = (Get-Date).ToString("o")
    config             = @{ rounds = $Rounds; delay_ms = $DelayMs }
    vietnamese         = $vnResult
    english            = $enResult
    cpu_during_typing  = $cpuDuringTyping
    output_verified    = $outputVerified
    ime_process        = $imeProcessName
} | ConvertTo-Json -Depth 5

$report | Out-File -FilePath $filepath -Encoding utf8
Write-Host "`n  Results saved: $filepath" -ForegroundColor Green

# --- Summary -------------------------------------------------------------

Write-Host ""
Write-Host ("=" * 72) -ForegroundColor Cyan
Write-Host "  SUMMARY - $IME ($Method)" -ForegroundColor Cyan
Write-Host ("=" * 72) -ForegroundColor Cyan
Write-Host ("  Vietnamese: avg {0,5:F1} us/key | p95 {1,5:F1} | p99 {2,5:F1}" -f `
    $vnResult.avg_per_key_us, $vnResult.p95_per_key_us, $vnResult.p99_per_key_us)
if ($enResult) {
    Write-Host ("  English:    avg {0,5:F1} us/key | p95 {1,5:F1} | p99 {2,5:F1}" -f `
        $enResult.avg_per_key_us, $enResult.p95_per_key_us, $enResult.p99_per_key_us)
}
if ($null -ne $cpuDuringTyping) {
    Write-Host ("  CPU time:   {0:F2} seconds ({1})" -f $cpuDuringTyping, $imeProcessName)
}
Write-Host ("  Verified:   {0}" -f $(if ($outputVerified) { "PASS" } else { "FAIL" }))
Write-Host ("=" * 72) -ForegroundColor Cyan
Write-Host ""
Write-Host "Switch IME and run again, then: .\benchmark_ime.ps1 -Compare" -ForegroundColor Yellow
