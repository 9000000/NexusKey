# VKey - one entry point.
#
#   .\vkey.cmd local              Release build with the locally synced Rust engine
#   .\vkey.cmd local -Engine Released
#                                ... Release build with the published, signed engine
#   .\vkey.cmd local -Lite        ... the Classic Win32 UI (VKeyClassic.exe)
#   .\vkey.cmd local -DebugBuild  ... Debug, which serves the Sciter UI from files
#   .\vkey.cmd local -Clean       ... after wiping the CMake cache
#   .\vkey.cmd local -NoRun       ... build only, do not launch
#   .\vkey.cmd local -FullLog     ... with the full CMake and MSBuild output
#   .\vkey.cmd test               release the current engine and start a test build
#   .\vkey.cmd test -DryRun       ... say what that would do, push nothing
#
# Go through vkey.cmd. PowerShell refuses unsigned scripts, and this repository
# usually sits on a mapped WSL drive, which Windows treats as remote - so even
# RemoteSigned blocks it. The .cmd is a batch file, which the policy does not
# cover.
#
# Kept to plain ASCII and to constructs Windows PowerShell 5.1 accepts: no
# here-string passed straight to a command (5.1 reads the @ as splatting), and no
# nested double quotes inside a subexpression in a string. PowerShell 7 accepts
# both, so a parse check there does not prove this runs on 5.1.
#
# Everything else under tools/ is machinery this calls. You should not need it.

param(
    [Parameter(Position = 0)]
    [string]$Mode,
    [ValidateSet("Local", "Released")]
    [string]$Engine = "Local",
    [switch]$Clean,
    [switch]$NoRun,
    # Classic Win32 UI instead of Sciter. CMakeLists only declares the VKeyLite
    # target under VKEY_LITE_MODE, so this has to reach configure, not just the
    # build step.
    [switch]$Lite,
    # -Debug itself is a PowerShell common parameter and cannot be redefined.
    [switch]$DebugBuild,
    # -Verbose is a common parameter too, hence the name.
    [switch]$FullLog,
    # test only: print what would happen, push nothing.
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vkeyRs = Join-Path (Split-Path -Parent $root) "VKey-rs"
$engineRoot = Join-Path $root "build-engine"
# -Lite builds into its own tree. The two configurations differ by
# VKEY_LITE_MODE, so sharing one cache would force a full reconfigure every
# time you switched. build-lite is the same directory
# internal/tools/build_lite.ps1 uses, with the same engine root, so the two
# scripts share a cache rather than fighting over one.
$buildName = "build"
if ($Lite) { $buildName = "build-lite" }
$buildDir = Join-Path $root $buildName

# Accept local / --local / -local, and the same for test.
$mode = ($Mode -replace '^-+', '').ToLower()

function Fail($message) {
    Write-Host $message -ForegroundColor Red
    exit 1
}

function Find-Python {
    # Windows ships a Microsoft Store stub at WindowsApps\python.exe. Get-Command
    # finds it, and running it prints an advert and exits, so existence is not
    # enough - each candidate has to actually report a Python 3 version.
    foreach ($candidate in @("py", "python", "python3")) {
        if (-not (Get-Command $candidate -ErrorAction SilentlyContinue)) { continue }
        $reported = & $candidate --version 2>&1
        if ($LASTEXITCODE -eq 0 -and "$reported" -match "^Python 3") { return $candidate }
    }
    return $null
}

function Get-PythonOrFail($whatFor) {
    $found = Find-Python
    if ($found) { return $found }
    $lines = @(
        "No working Python on PATH.",
        "  py, python and python3 were tried; any that exist only answered with the",
        "  Microsoft Store stub, which is not Python.",
        "",
        "Either install Python for Windows (python.org, tick 'Add to PATH'),",
        "or run this one step inside WSL instead:",
        "",
        "  " + $whatFor
    )
    Fail ($lines -join [Environment]::NewLine)
}

if ($mode -eq "local") {

    $uiName = "Sciter"
    if ($Lite) { $uiName = "Classic" }
    Write-Host "=== VKey - local build ($uiName UI) ===" -ForegroundColor Cyan

    if ($Clean -and (Test-Path $buildDir)) {
        Write-Host "[1/4] wiping the CMake cache" -ForegroundColor Yellow
        $cache = Join-Path $buildDir "CMakeCache.txt"
        $cacheDir = Join-Path $buildDir "CMakeFiles"
        Remove-Item -Recurse -Force $cache, $cacheDir -ErrorAction SilentlyContinue
    }

    Write-Host "[2/4] engine ($Engine)" -ForegroundColor Yellow
    $allowUnsignedLocalEngine = "OFF"
    if ($Engine -eq "Local") {
        $engineDll = Join-Path $engineRoot "lib\win-x64\vkey_engine.dll"
        $engineHeader = Join-Path $engineRoot "include\vkey_engine.h"
        $engineLock = Join-Path $engineRoot "engine.lock"
        if (-not (Test-Path $engineDll) -or
            -not (Test-Path $engineHeader) -or
            -not (Test-Path $engineLock)) {
            $lines = @(
                "The local engine has not been synced to build-engine.",
                "Run this in WSL first:",
                "",
                "  cd ~/code/VKey-rs",
                "  bash tools/sync_nexuskey_engine.sh",
                "",
                "To use the published engine instead:",
                "  .\vkey.cmd local -Engine Released"
            )
            Fail ($lines -join [Environment]::NewLine)
        }

        # sync_nexuskey_engine.sh deliberately gives development locks counter 1.
        # Refuse a released/stale build-engine directory instead of silently
        # claiming that it is the engine from the adjacent source checkout.
        $lockText = Get-Content $engineLock -Raw
        if ($lockText -notmatch "(?m)^counter=1\r?$") {
            $lines = @(
                "build-engine does not contain a locally synced engine (counter=1).",
                "Sync it from VKey-rs, or select the published engine explicitly:",
                "  .\vkey.cmd local -Engine Released"
            )
            Fail ($lines -join [Environment]::NewLine)
        }

        # A local DLL is unsigned. This opt-in keeps the Release optimizer while
        # binding the loader to the exact byte length and SHA-256 in engine.lock.
        $allowUnsignedLocalEngine = "ON"
        Write-Host "using locally synced engine: $engineDll" -ForegroundColor DarkGray
        Write-Host "Release will trust only the exact hash in build-engine\engine.lock" -ForegroundColor DarkGray
    }
    else {
        $fetch = Join-Path $root "tools\fetch-engine.ps1"
        & $fetch
        if ($LASTEXITCODE -ne 0) { Fail "could not get the released engine" }
    }

    # Quiet by default. --log-level=WARNING drops CMake's STATUS chatter, and
    # MSBuild's ErrorsOnly console logger drops the per-file compile spam. Nothing
    # that matters is lost: CMakeLists sets /W4 /WX globally, so any warning worth
    # seeing has already become an error and still prints. -FullLog restores both.
    $cmakeQuiet = @()
    $buildQuiet = @()
    if (-not $FullLog) {
        $cmakeQuiet = @("--log-level=WARNING")
        $buildQuiet = @("--", "/nologo", "/clp:ErrorsOnly;Summary")
    }

    $liteConfigure = @()
    $liteTarget = @()
    if ($Lite) {
        $liteConfigure = @("-DVKEY_LITE_MODE=ON")
        # Without an explicit target this would build every target the lite
        # cache declares, VKeyTSF and the tests included.
        $liteTarget = @("--target", "VKeyLite")
    }

    Write-Host "[3/4] configure" -ForegroundColor Yellow
    cmake -S $root -B $buildDir -G "Visual Studio 18 2026" -A x64 -DVKEY_USE_RUST_ENGINE=ON -DVKEY_ENGINE_ROOT="$engineRoot" "-DVKEY_ALLOW_UNSIGNED_LOCAL_ENGINE=$allowUnsignedLocalEngine" @liteConfigure @cmakeQuiet
    if ($LASTEXITCODE -ne 0) { Fail "configure failed" }

    # Debug serves the Sciter UI from ui/ next to the exe instead of the
    # packfolder blob compiled into it, so editing HTML or CSS only needs the app
    # restarted, not rebuilt. Release embeds it.
    $config = "Release"
    if ($DebugBuild) { $config = "Debug" }

    Write-Host "[4/4] build ($config)" -ForegroundColor Yellow
    cmake --build $buildDir --config $config @liteTarget @buildQuiet
    if ($LASTEXITCODE -ne 0) { Fail "build failed (re-run with -FullLog to see everything)" }

    # CMakeLists sets OUTPUT_NAME "VKeyClassic" on the VKeyLite target.
    $exeName = "VKey.exe"
    if ($Lite) { $exeName = "VKeyClassic.exe" }
    $exe = Join-Path $buildDir "$config\$exeName"
    Write-Host ""
    Write-Host "built: $exe" -ForegroundColor Green

    # Classic draws its dialogs with native Win32 calls, so there is no ui/
    # directory to serve from and nothing about -DebugBuild to explain.
    if ($DebugBuild -and -not $Lite) {
        $uiDir = Join-Path $buildDir "$config\ui"
        Write-Host "Sciter UI is served from $uiDir" -ForegroundColor DarkGray
        Write-Host "edit HTML/CSS there and restart the app - no rebuild needed" -ForegroundColor DarkGray
    }

    if (-not $NoRun) {
        Write-Host "starting it - close it to return here" -ForegroundColor DarkGray
        & $exe
    }

}
elseif ($mode -eq "test") {

    Write-Host "=== VKey - release the engine and start a test build ===" -ForegroundColor Cyan

    if (-not (Test-Path $vkeyRs)) { Fail "no VKey-rs beside this repository (looked in $vkeyRs)" }
    if (-not $env:VKEY_ENGINE_TOKEN) {
        $line1 = "VKEY_ENGINE_TOKEN is not set."
        $line2 = '  setx VKEY_ENGINE_TOKEN "<token>"     # then open a new terminal'
        Fail ($line1 + [Environment]::NewLine + $line2)
    }

    # ship_engine.py does the whole chain: bump the engine version, tag, wait for
    # the release workflow, publish, repoint this repository's lock and tag,
    # commit, push, and dispatch the build.
    $python = Get-PythonOrFail "cd ~/code/VKey-rs && python3 tools/ship_engine.py --nexuskey ~/code/NexusKey --build"
    $ship = Join-Path $vkeyRs "tools\ship_engine.py"
    if ($DryRun) {
        & $python $ship --nexuskey $root --build --dry-run
        exit $LASTEXITCODE
    }
    & $python $ship --nexuskey $root --build
    if ($LASTEXITCODE -ne 0) { Fail "shipping the engine failed - nothing further was started" }

    Write-Host ""
    Write-Host "the test build is running." -ForegroundColor Green
    Write-Host "watch it:  gh run list --workflow=build.yml"
    Write-Host "when it finishes, the artifact is on the run page for testers to download."

}
else {

    $help = @'
VKey

  .\vkey.cmd local              Release build with the locally synced Rust engine
  .\vkey.cmd local -Engine Released
                                ... use the published, signed Rust engine
  .\vkey.cmd local -DebugBuild  ... Debug, which serves the Sciter UI from files
  .\vkey.cmd local -Clean       ... after wiping the CMake cache
  .\vkey.cmd local -NoRun       ... build only, do not launch
  .\vkey.cmd local -FullLog     ... with the full CMake and MSBuild output
  .\vkey.cmd test               release the current engine and start a test build
  .\vkey.cmd test -DryRun       ... say what that would do, push nothing

local  builds on this machine. It uses the engine synced from the adjacent
       VKey-rs checkout by default, keeps the Release optimizer, and never
       replaces that engine with a download. Use -Engine Released to fetch and
       use the engine named in extern/vkey_engine/engine.release instead.
       -DebugBuild still selects Debug and serves the Sciter UI from files.

test   cuts the next engine release from VKey-rs, points this repository at it,
       pushes, and starts the GitHub build whose artifact testers download.
       Use this when an engine change needs to reach someone else - pushing a
       NexusKey commit alone does not carry one.

Build output is errors only. /W4 /WX is set globally, so a warning that matters
is already an error and still prints; -FullLog brings back everything.

Released and test need VKEY_ENGINE_TOKEN; the engine release repository is private.
'@
    Write-Host $help
    if ($mode) { exit 1 }

}
