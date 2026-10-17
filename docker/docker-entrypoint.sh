#!/bin/bash
set -e

git submodule update --init --recursive

### --- Version -------------------------------------------------------------
# SEMVER comes in from the CI, whose version job resolves the tag -- and, on
# the pushbuild.txt path, creates it -- before any build starts. The tag is
# pushed by then but this checkout need not have it, so asking git first would
# come up empty and the build would ship as "Local".
#
# A build outside that (a local one, a manual docker compose up) has nothing
# passed in and falls back to whatever tag HEAD carries. An untagged HEAD is
# not an error: CMake stamps it "Local" and the line below says so.
if [ -n "${SEMVER:-}" ]; then
  export SEMVER
  echo "=== Version: $SEMVER (from the environment) ==="
elif SEMVER="$(git describe --tags --exact-match 2>/dev/null)"; then
  export SEMVER
  echo "=== Version: $SEMVER (tag on HEAD) ==="
else
  # Exported-but-empty is not the same as unset to CMake, and would stamp the
  # build with an empty version instead of falling back.
  unset SEMVER
  echo '=== Version: no tag on HEAD, building as "Local" ==='
fi

export GITHUB_SHA_SHORT="$(git rev-parse --short HEAD)"

### --- Download Source2Mod-SDK + Metamod-Source ----------------------
SDK_DIR="/tmp/sdk"
SOURCE2MODSDK_DIR="$SDK_DIR/source2mod-sdk"
MMSOURCE_DIR="$SDK_DIR/metamod-source"

echo "=== Preparing temporary SDK directory ==="
rm -rf "$SDK_DIR"
mkdir -p "$SDK_DIR"

echo "=== Downloading Source2Mod-SDK ==="
git clone --recursive https://github.com/Source2Mod/source2mod-sdk.git "$SOURCE2MODSDK_DIR"
# s2sdk is the SDK's vendor/s2sdk submodule, at the commit the SDK pins.
S2SDK_DIR="$SOURCE2MODSDK_DIR/vendor/s2sdk"

# S2SDK_REF moves it to another commit when the pinned one does not build;
# empty = the pin.
S2SDK_REF="${S2SDK_REF-}"
if [ -n "$S2SDK_REF" ]; then
  echo "=== Moving s2sdk to $S2SDK_REF ==="
  git -C "$S2SDK_DIR" fetch -q origin "$S2SDK_REF"
  git -C "$S2SDK_DIR" checkout -q FETCH_HEAD
  git -C "$S2SDK_DIR" submodule update -q --init --recursive
fi
echo "s2sdk: $(git -C "$S2SDK_DIR" log -1 --format='%h %s')"

echo "=== Downloading Metamod-Source ==="
git clone --recursive --branch master --single-branch https://github.com/Source2Mod/metamod-source.git "$MMSOURCE_DIR"

# Hooking plugins are built against the SDK's vendor/khook and refuse to load
# on a core built with a different khook.hpp, so a header drift here must fail
# the release. A KHook bump that only touched its src/ is fine.
CORE_KHOOK="$(git -C "$MMSOURCE_DIR/third_party/khook" rev-parse HEAD)"
SDK_KHOOK="$(git -C "$SOURCE2MODSDK_DIR/vendor/khook" rev-parse HEAD)"
echo "KHook: metamod-source $CORE_KHOOK, source2mod-sdk $SDK_KHOOK"
if ! cmp -s <(tr -d '\r' < "$MMSOURCE_DIR/third_party/khook/include/khook.hpp") \
            <(tr -d '\r' < "$SOURCE2MODSDK_DIR/vendor/khook/include/khook.hpp"); then
  echo "khook.hpp differs between metamod-source and source2mod-sdk -- bump the SDK's vendor/khook" >&2
  exit 1
fi

### --- Export env vars for CMake ------------------------------------------
export SOURCE2MOD_SDK="$SOURCE2MODSDK_DIR"
export S2SDK="$S2SDK_DIR"
export MMSOURCE_DEV="$MMSOURCE_DIR"

echo "Using SOURCE2MOD_SDK=$SOURCE2MOD_SDK"
echo "Using S2SDK=$S2SDK"
echo "Using MMSOURCE_DEV=$MMSOURCE_DEV"

### --- Build ---------------------------------------------------------------
echo "=== Starting build ==="

rm -rf build
mkdir build
cd build

cmake .. -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang-18 \
  -DCMAKE_CXX_COMPILER=clang++-18

echo "=== Building with Clang | RelWithDebInfo ==="

cmake --build . -j"$(nproc)"