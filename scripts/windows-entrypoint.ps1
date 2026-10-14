#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host "=== Updating git submodules ==="
git submodule update --init --recursive

### --- Version ---------------------------------------------------------------
# SEMVER comes in from the CI, whose version job resolves the tag -- and, on
# the pushbuild.txt path, creates it -- before any build starts. The tag is
# pushed by then but this checkout need not have it, so asking git first would
# come up empty and the build would ship as "Local".
#
# A build outside that (a local one) has nothing passed in and falls back to
# whatever tag HEAD carries. An untagged HEAD is not an error: CMake stamps it
# "Local" and the line below says so.
if ($env:SEMVER) {
    Write-Host "=== Version: $env:SEMVER (from the environment) ==="
} else {
    $tag = $null
    try { $tag = git describe --tags --exact-match 2>$null } catch {}

    if ($tag) {
        $env:SEMVER = $tag
        Write-Host "=== Version: $tag (tag on HEAD) ==="
    } else {
        # Set-but-empty is not the same as unset to CMake, and would stamp the
        # build with an empty version instead of falling back.
        Remove-Item Env:SEMVER -ErrorAction SilentlyContinue
        Write-Host '=== Version: no tag on HEAD, building as "Local" ==='
    }
}

$env:GITHUB_SHA_SHORT = git rev-parse --short HEAD

### --- Load MSVC environment (IMPORTANT) -------------------------------------
Write-Host "=== Loading MSVC environment ==="

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $vsPath) {
    throw "MSVC not found on runner!"
}

$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"

cmd /c "`"$vcvars`" && set" | ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") {
        Set-Item -Path "env:$($matches[1])" -Value $matches[2]
    }
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw "MSVC (cl.exe) not available after environment setup!"
}

Write-Host "MSVC loaded successfully"

### --- Clone SDKs ------------------------------------------------------------
$SDK_DIR = "$env:TEMP\sdk"
$SOURCE2MODSDK_DIR = "$SDK_DIR\source2mod-sdk"
$MMSOURCE_DIR = "$SDK_DIR\metamod-source"

Write-Host "=== Preparing temporary SDK directory ==="
if (Test-Path $SDK_DIR) { Remove-Item -Recurse -Force $SDK_DIR }
New-Item -ItemType Directory -Force $SDK_DIR | Out-Null

Write-Host "=== Downloading Source2Mod-SDK ==="
git clone --recursive https://github.com/SlynxCZ/Source2Mod-sdk.git $SOURCE2MODSDK_DIR

# s2sdk is the SDK's vendor/s2sdk submodule, at the commit the SDK pins.
$S2SDK_DIR = "$SOURCE2MODSDK_DIR\vendor\s2sdk"

# S2SDK_REF moves it to another commit when the pinned one does not build;
# empty = the pin.
$S2SDK_REF = if ($null -ne $env:S2SDK_REF) { $env:S2SDK_REF } else { "" }
if ($S2SDK_REF) {
    Write-Host "=== Moving s2sdk to $S2SDK_REF ==="
    git -C $S2SDK_DIR fetch -q origin $S2SDK_REF
    git -C $S2SDK_DIR checkout -q FETCH_HEAD
    git -C $S2SDK_DIR submodule update -q --init --recursive
}
Write-Host "s2sdk: $(git -C $S2SDK_DIR log -1 --format='%h %s')"

Write-Host "=== Downloading Metamod-Source ==="
git clone --recursive --branch master --single-branch https://github.com/alliedmodders/metamod-source.git $MMSOURCE_DIR

# Hooking plugins refuse to load on a core built with a different khook.hpp
# than the SDK's. A KHook bump that only touched its src/ is fine.
$CORE_KHOOK = git -C "$MMSOURCE_DIR\third_party\khook" rev-parse HEAD
$SDK_KHOOK = git -C "$SOURCE2MODSDK_DIR\vendor\khook" rev-parse HEAD
Write-Host "KHook: metamod-source $CORE_KHOOK, source2mod-sdk $SDK_KHOOK"
$CORE_HPP = [IO.File]::ReadAllText("$MMSOURCE_DIR\third_party\khook\include\khook.hpp").Replace("`r", "")
$SDK_HPP = [IO.File]::ReadAllText("$SOURCE2MODSDK_DIR\vendor\khook\include\khook.hpp").Replace("`r", "")
if ($CORE_HPP -cne $SDK_HPP) {
    Write-Error "khook.hpp differs between metamod-source and source2mod-sdk -- bump the SDK's vendor/khook"
    exit 1
}

### --- Export env vars for CMake ---------------------------------------------
$env:SOURCE2MOD_SDK = $SOURCE2MODSDK_DIR
$env:MMSOURCE_DEV = $MMSOURCE_DIR

Write-Host "Using MMSOURCE_DEV=$env:MMSOURCE_DEV"

### --- Build -----------------------------------------------------------------
$REPO_ROOT = Split-Path -Parent $PSScriptRoot

Write-Host "=== Starting build ==="
Write-Host "REPO_ROOT=$REPO_ROOT"

$BUILD_DIR = "$REPO_ROOT\build"
if (Test-Path $BUILD_DIR) { Remove-Item -Recurse -Force $BUILD_DIR }
New-Item -ItemType Directory $BUILD_DIR | Out-Null
Set-Location $BUILD_DIR

cmake $REPO_ROOT -G Ninja `
    -DCMAKE_C_COMPILER=cl `
    -DCMAKE_CXX_COMPILER=cl `
    -DCMAKE_BUILD_TYPE=RelWithDebInfo

Write-Host "=== Building | RelWithDebInfo ==="
cmake --build $BUILD_DIR -- -j $env:NUMBER_OF_PROCESSORS
