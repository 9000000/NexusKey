# VKey — one entry point.
#
#   .\vkey.ps1 local           build and run here, with the Rust engine
#   .\vkey.ps1 local -Clean    ... after wiping the CMake cache
#   .\vkey.ps1 local -NoRun    ... build only, do not launch
#   .\vkey.ps1 test            release the current engine and start a test build
#
# Everything else under tools/ is machinery this calls. You should not need it.

param(
    [Parameter(Position = 0)]
    [string]$Mode,
    [switch]$Clean,
    [switch]$NoRun
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vkeyRs = Join-Path (Split-Path -Parent $root) "VKey-rs"
$engineRoot = Join-Path $root "build-engine"
$buildDir = Join-Path $root "build"

# Accept local / --local / -local, and the same for test.
$mode = ($Mode -replace '^-+', '').ToLower()

function Fail($message) {
    Write-Host $message -ForegroundColor Red
    exit 1
}

function Find-Python {
    $found = @("python", "py", "python3") |
        Where-Object { Get-Command $_ -ErrorAction SilentlyContinue } |
        Select-Object -First 1
    if (-not $found) { Fail "no python on PATH (tried python, py, python3)" }
    return $found
}

switch ($mode) {

    "local" {
        Write-Host "=== VKey — local build ===" -ForegroundColor Cyan

        if ($Clean -and (Test-Path $buildDir)) {
            Write-Host "[1/4] wiping the CMake cache" -ForegroundColor Yellow
            Remove-Item -Recurse -Force (Join-Path $buildDir "CMakeCache.txt"),
                                        (Join-Path $buildDir "CMakeFiles") -ErrorAction SilentlyContinue
        }

        Write-Host "[2/4] engine" -ForegroundColor Yellow
        & (Join-Path $root "tools\fetch-engine.ps1")
        if ($LASTEXITCODE -ne 0) { Fail "could not get the engine" }

        Write-Host "[3/4] configure" -ForegroundColor Yellow
        cmake -B $buildDir -G "Visual Studio 18 2026" -A x64 `
              -DVKEY_USE_RUST_ENGINE=ON -DVKEY_ENGINE_ROOT="$engineRoot"
        if ($LASTEXITCODE -ne 0) { Fail "configure failed" }

        Write-Host "[4/4] build" -ForegroundColor Yellow
        cmake --build $buildDir --config Release
        if ($LASTEXITCODE -ne 0) { Fail "build failed" }

        $exe = Join-Path $buildDir "Release\VKey.exe"
        Write-Host "`nbuilt: $exe" -ForegroundColor Green
        if (-not $NoRun) {
            Write-Host "starting it — close it to return here" -ForegroundColor DarkGray
            & $exe
        }
    }

    "test" {
        Write-Host "=== VKey — release the engine and start a test build ===" -ForegroundColor Cyan

        if (-not (Test-Path $vkeyRs)) { Fail "no VKey-rs beside this repository (looked in $vkeyRs)" }
        if (-not $env:VKEY_ENGINE_TOKEN) {
            Fail @"
VKEY_ENGINE_TOKEN is not set.
  setx VKEY_ENGINE_TOKEN "<token>"     # then open a new terminal
"@
        }

        # ship_engine.py does the whole chain: bump the engine version, tag, wait
        # for the release workflow, publish, repoint this repository's lock and
        # tag, commit, push, and dispatch the build.
        $python = Find-Python
        & $python (Join-Path $vkeyRs "tools\ship_engine.py") --nexuskey $root --build
        if ($LASTEXITCODE -ne 0) { Fail "shipping the engine failed — nothing further was started" }

        Write-Host "`nthe test build is running." -ForegroundColor Green
        Write-Host "watch it:  gh run list --workflow=build.yml"
        Write-Host "when it finishes, the artifact is on the run page for testers to download."
    }

    default {
        Write-Host @"
VKey

  .\vkey.ps1 local           build and run here, with the Rust engine
  .\vkey.ps1 local -Clean    ... after wiping the CMake cache
  .\vkey.ps1 local -NoRun    ... build only, do not launch
  .\vkey.ps1 test            release the current engine and start a test build

local  builds on this machine against the engine named in
       extern/vkey_engine/engine.release, fetching it once.

test   cuts the next engine release from VKey-rs, points this repository at it,
       pushes, and starts the GitHub build whose artifact testers download.
       Use this when an engine change needs to reach someone else — pushing a
       NexusKey commit alone does not carry one.

Both need VKEY_ENGINE_TOKEN; the engine release repository is private.
"@
        if ($mode) { exit 1 }
    }
}
