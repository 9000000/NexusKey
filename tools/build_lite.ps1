# NexusKey Lite (Classic UI) — Build & Run script
# Usage: .\tools\build_lite.ps1 [-Clean] [-Release] [-Run]
#
# Examples:
#   .\tools\build_lite.ps1              # Build Debug
#   .\tools\build_lite.ps1 -Run         # Build Debug + Run
#   .\tools\build_lite.ps1 -Clean -Run  # Clean rebuild + Run
#   .\tools\build_lite.ps1 -Release     # Build Release

param(
    [switch]$Clean,
    [switch]$Release,
    [switch]$Run
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$buildDir = Join-Path $root "build-lite"
$config = if ($Release) { "Release" } else { "Debug" }
$exe = Join-Path $buildDir "$config\NexusKeyClassic.exe"

Write-Host "=== NexusKey Lite Build ===" -ForegroundColor Cyan
Write-Host "  Root:   $root"
Write-Host "  Build:  $buildDir"
Write-Host "  Config: $config"
Write-Host ""

# Clean
if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "[1/3] Cleaning build-lite..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

# Configure (if needed)
if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
    Write-Host "[1/3] Configuring CMake..." -ForegroundColor Yellow
    cmake -S $root -B $buildDir -G "Visual Studio 18 2026" -A x64 -DNEXUSKEY_LITE_MODE=ON
    if ($LASTEXITCODE -ne 0) { Write-Host "CMake configure FAILED" -ForegroundColor Red; exit 1 }
} else {
    Write-Host "[1/3] CMake already configured (use -Clean to reconfigure)" -ForegroundColor DarkGray
}

# Build
Write-Host "[2/3] Building NextKeyLite ($config)..." -ForegroundColor Yellow
$buildOutput = cmake --build $buildDir --target NextKeyLite --config $config 2>&1
$buildCode = $LASTEXITCODE

# Show only errors/warnings (clean output)
$buildOutput | Select-String -Pattern "error |warning C|fatal error|Build SUCCEEDED|Build FAILED" | ForEach-Object { Write-Host $_.Line }

if ($buildCode -ne 0) {
    Write-Host "Build FAILED" -ForegroundColor Red
    exit 1
}
Write-Host "Build OK: $exe" -ForegroundColor Green

# Run
if ($Run) {
    Write-Host "[3/3] Launching..." -ForegroundColor Yellow

    # Kill existing instance
    $proc = Get-Process -Name "NexusKeyClassic" -ErrorAction SilentlyContinue
    if ($proc) {
        Write-Host "  Killing existing NexusKeyClassic..." -ForegroundColor DarkYellow
        $proc | Stop-Process -Force
        Start-Sleep -Milliseconds 500
    }

    Start-Process -FilePath $exe
    Write-Host "  Running: $exe" -ForegroundColor Green
} else {
    Write-Host "[3/3] Skip run (use -Run to launch)" -ForegroundColor DarkGray
}
