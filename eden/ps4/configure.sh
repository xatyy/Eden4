#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DOLPHIN_PS4_ROOT="${DOLPHIN_PS4_ROOT:-$(cd "$HERE/../../runtime" && pwd)}"
source "$DOLPHIN_PS4_ROOT/scripts/ps4-env.sh"
export CC="$DOLPHIN_PS4_ROOT/toolchain/bin/ps4-cc"
export CXX="$DOLPHIN_PS4_ROOT/toolchain/bin/ps4-c++"
export AR="$PS4_AR" RANLIB="$PS4_RANLIB"
BUILD="${EDEN_PS4_BUILD:-$PS4_BUILD_ROOT/build/eden}"
# Works in a fresh repository too (no commit yet).
REVISION="$(git -C "$HERE/.." rev-parse --verify -q HEAD 2>/dev/null)" || REVISION=unknown
BRANCH="$(git -C "$HERE/.." symbolic-ref --short -q HEAD 2>/dev/null)" || BRANCH=main
TAG="$(git -C "$HERE/.." describe --tags --abbrev=0 2>/dev/null || echo experimental)"
ps4_cmake -S "$HERE/.." -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DOLPHIN_PS4_ROOT/toolchain/ps4-love-modern.cmake" \
    "-DCMAKE_PROJECT_yuzu_INCLUDE=$DOLPHIN_PS4_ROOT/toolchain/ps4-love-style.cmake" \
    -DCMAKE_SYSROOT="$OPENORBIS" -DDOLPHIN_PS4_ROOT="$DOLPHIN_PS4_ROOT" \
    -DSCM_REVISION="$REVISION" -DSCM_BRANCH="$BRANCH" -DSCM_TAG="$TAG" \
    -DYUZU_PS4=ON -DYUZU_CMD=OFF -DENABLE_QT=OFF -DYUZU_ROOM=OFF \
    -DENABLE_OPENGL=OFF -DENABLE_CUBEB=OFF -DENABLE_LIBUSB=OFF \
    -DENABLE_WEB_SERVICE=OFF -DENABLE_UPDATE_CHECKER=OFF -DUSE_DISCORD_PRESENCE=OFF \
    -DENABLE_RESHADE=OFF -DENABLE_LTO=OFF -DENABLE_WERROR=OFF \
    -DBUILD_TESTING=OFF -DYUZU_TESTS=OFF -DDYNARMIC_TESTS=OFF \
    -DDYNARMIC_ENABLE_CPU_FEATURE_DETECTION=ON -DDYNARMIC_ENABLE_NO_EXECUTE_SUPPORT=ON \
    -DYUZU_BUILD_PRESET=custom -DYUZU_STATIC_BUILD=ON -DCPMUTIL_FORCE_BUNDLED=ON \
    -DOPENSSL_TARGET_PLATFORM=linux-x86_64 \
    -DYUZU_USE_BUNDLED_OPENSSL=OFF -DYUZU_USE_BUNDLED_FFMPEG=OFF \
    -DYUZU_USE_EXTERNAL_FFMPEG=ON -DSDL_UNIX_CONSOLE_BUILD=ON \
    -DPS4_PAID=0x3800000000000035 "$@"
