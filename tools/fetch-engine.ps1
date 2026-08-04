# Fetch the prebuilt engine into build-engine/, ready for
# -DVKEY_ENGINE_ROOT=build-engine.
#
# Reads the tag from extern/vkey_engine/engine.release and the token from
# $env:VKEY_ENGINE_TOKEN, so there is nothing to type. Already fetched and
# matching is a no-op; pass -Force to fetch again.
#
# Usage:
#   .\tools\fetch-engine.ps1
#   .\tools\fetch-engine.ps1 -Force

param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$engineRoot = Join-Path $root "build-engine"
$engineDll = Join-Path $engineRoot "lib\win-x64\vkey_engine.dll"

if ((Test-Path $engineDll) -and -not $Force) {
    Write-Host "engine already present: $engineDll" -ForegroundColor DarkGray
    Write-Host "configure with -DVKEY_USE_RUST_ENGINE=ON -DVKEY_ENGINE_ROOT=build-engine"
    exit 0
}

if (-not $env:VKEY_ENGINE_TOKEN) {
    Write-Host "VKEY_ENGINE_TOKEN is not set." -ForegroundColor Red
    Write-Host "The engine release is in a private repository, so the fetch needs a token:"
    Write-Host '  setx VKEY_ENGINE_TOKEN "<token>"    # then open a new terminal'
    Write-Host ""
    Write-Host "To build without the Rust engine instead, configure with"
    Write-Host "-DVKEY_USE_RUST_ENGINE=OFF (that is the default)."
    exit 1
}

# python3 is not a thing on Windows; py is the launcher. Take whichever exists.
$python = @("python", "py", "python3") |
    Where-Object { Get-Command $_ -ErrorAction SilentlyContinue } |
    Select-Object -First 1
if (-not $python) {
    Write-Host "no python interpreter on PATH (tried python, py, python3)" -ForegroundColor Red
    exit 1
}

$tag = (Get-Content (Join-Path $root "extern\vkey_engine\engine.release") -Raw).Trim()
Write-Host "fetching engine $tag ..." -ForegroundColor Yellow

& $python (Join-Path $root "tools\fetch_engine.py") `
    --lock (Join-Path $root "extern\vkey_engine\engine.lock") `
    --dest $engineRoot `
    --repo phatMT97/VKey-rs `
    --tag $tag `
    --token $env:VKEY_ENGINE_TOKEN
if ($LASTEXITCODE -ne 0) {
    Write-Host "engine fetch FAILED" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "now configure with:" -ForegroundColor Green
Write-Host '  cmake -B build -G "Visual Studio 18 2026" -A x64 -DVKEY_USE_RUST_ENGINE=ON -DVKEY_ENGINE_ROOT=build-engine'
