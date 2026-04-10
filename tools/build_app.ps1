# NexusKey App (Sciter UI) — Build & Run script
# Usage: .\tools\build_app.ps1 [-Clean] [-Release] [-Run]

param(
    [switch]$Clean,
    [switch]$Release,
    [switch]$Run
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$buildDir = Join-Path $root "build"
$config = if ($Release) { "Release" } else { "Debug" }
$exe = Join-Path $buildDir "$config\NexusKey.exe"

Write-Host "=== NexusKey App Build ===" -ForegroundColor Cyan
Write-Host "  Root:   $root"
Write-Host "  Build:  $buildDir"
Write-Host "  Config: $config"
Write-Host ""

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "[1/3] Cleaning build..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
    Write-Host "[1/3] Configuring CMake..." -ForegroundColor Yellow
    cmake -S $root -B $buildDir -G "Visual Studio 18 2026" -A x64
    if ($LASTEXITCODE -ne 0) { Write-Host "CMake configure FAILED" -ForegroundColor Red; exit 1 }
} else {
    Write-Host "[1/3] CMake already configured (use -Clean to reconfigure)" -ForegroundColor DarkGray
}

Write-Host "[2/3] Building NexusKey ($config)..." -ForegroundColor Yellow
$buildOutput = cmake --build $buildDir --target NextKeyApp --config $config 2>&1
$buildCode = $LASTEXITCODE

$buildOutput | Select-String -Pattern "error |warning C|fatal error|Build SUCCEEDED|Build FAILED" | ForEach-Object { Write-Host $_.Line }

if ($buildCode -ne 0) {
    Write-Host "Build FAILED" -ForegroundColor Red
    exit 1
}
Write-Host "Build OK: $exe" -ForegroundColor Green

if ($Run) {
    Write-Host "[3/3] Launching..." -ForegroundColor Yellow
    $proc = Get-Process -Name "NexusKey" -ErrorAction SilentlyContinue
    if ($proc) {
        Write-Host "  Killing existing NexusKey..." -ForegroundColor DarkYellow
        $proc | Stop-Process -Force
        Start-Sleep -Milliseconds 500
    }
    Start-Process -FilePath $exe
    Write-Host "  Running: $exe" -ForegroundColor Green
} else {
    Write-Host "[3/3] Skip run (use -Run to launch)" -ForegroundColor DarkGray
}
