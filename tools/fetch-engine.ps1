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

$engineSig = "$engineDll.sig"

# A DLL without its signature is only half an engine: Release verifies the
# signature and refuses the hash, so treat a missing .sig as "not fetched".
if ((Test-Path $engineDll) -and (Test-Path $engineSig) -and -not $Force) {
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

# Windows ships a Microsoft Store stub at WindowsApps\python.exe: Get-Command
# finds it and running it prints an advert, so existence is not enough. Each
# candidate has to actually report a Python 3 version.
$python = $null
foreach ($candidate in @("py", "python", "python3")) {
    if (-not (Get-Command $candidate -ErrorAction SilentlyContinue)) { continue }
    $reported = & $candidate --version 2>&1
    if ($LASTEXITCODE -eq 0 -and "$reported" -match "^Python 3") { $python = $candidate; break }
}
if (-not $python) {
    Write-Host "No working Python on PATH." -ForegroundColor Red
    Write-Host "  py, python and python3 were tried; any that exist only answered with"
    Write-Host "  the Microsoft Store stub, which is not Python."
    Write-Host ""
    Write-Host "Either install Python for Windows (python.org, tick 'Add to PATH'),"
    Write-Host "or fetch the engine from WSL instead:"
    Write-Host ""
    Write-Host '  cd ~/code/NexusKey && python3 tools/fetch_engine.py \'
    Write-Host '      --lock extern/vkey_engine/engine.lock --dest build-engine \'
    Write-Host '      --repo phatMT97/VKey-rs --tag $(cat extern/vkey_engine/engine.release) \'
    Write-Host '      --token $VKEY_ENGINE_TOKEN'
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
