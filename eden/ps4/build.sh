#!/usr/bin/env bash
# Builds the Eden4 package: eden/ps4/build.sh [--no-configure]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
DOLPHIN_PS4_ROOT="${DOLPHIN_PS4_ROOT:-$REPO/runtime}"
source "$DOLPHIN_PS4_ROOT/scripts/ps4-env.sh"
BUILD="${EDEN_PS4_BUILD:-$PS4_BUILD_ROOT/build/eden}"
STAGE="$BUILD/stage"
OUT="$PS4_BUILD_ROOT/out"

NAME="Eden4"
VERSION="${EDEN4_VERSION:-01.00}"   # PKG version, XX.YY
# EDEN00002 replaces the Eden PS4 test builds in place; the app keeps its data in /data/EdenPS4.
TITLE_ID="${EDEN4_TITLE_ID:-EDEN00002}"

if [ "${1:-}" != --no-configure ]; then "$HERE/configure.sh"; fi
cmake --build "$BUILD" --target eden-ps4_eboot -j"$JOBS"
python3 "$DOLPHIN_PS4_ROOT/toolchain/check-oelf.py" \
    "$BUILD/src/yuzu_ps4/eden-ps4_eboot/eden-ps4.oelf"
rm -rf "$STAGE"
mkdir -p "$STAGE/sce_sys" "$STAGE/licenses" "$OUT"
cp "$BUILD/src/yuzu_ps4/eden-ps4_eboot/eboot.bin" "$STAGE/eboot.bin"
# Development builds only: the CPU self-test program, listed on HOME as two "CPU self-test" entries.
if [ "${EDEN4_SELF_TEST:-0}" = 1 ]; then
    mkdir -p "$STAGE/homebrew"
    "$HERE/build-cpu-smoke.sh" "$STAGE/homebrew"
    rm -f "$STAGE/homebrew/cpu-smoke.o"
fi
cp "$REPO/LICENSE" "$STAGE/licenses/Eden4-GPL-3.0.txt"
cp "$DOLPHIN_PS4_ROOT/LICENSE" "$STAGE/licenses/Runtime-GPL-2.0.txt"
cp "$DOLPHIN_PS4_ROOT/fonts/Nunito-OFL.txt" "$STAGE/licenses/Nunito-OFL.txt"
cp "$REPO/CREDITS.md" "$STAGE/licenses/CREDITS.md"
python3 "$HERE/collect-licenses.py" "$HERE/.." "$PS4_BUILD_ROOT" "$STAGE/licenses"

# Icon: branding/icon0.png (any square PNG/JPEG) if present, else Eden's logo on a dark tile.
# Optional: branding/pic1.png, the background shown behind the app on the PS4 home screen.
python3 - "$REPO/branding" "$HERE/../dist/qt_themes/default/icons/256x256/eden.png" "$STAGE/sce_sys" <<'PY'
import sys
from pathlib import Path
from PIL import Image
branding, fallback, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
custom = next((p for p in (branding / 'icon0.png', branding / 'icon0.jpg') if p.exists()), None)
with Image.open(custom or fallback) as original:
    icon = original.convert('RGBA')
    side = min(icon.size)  # centre-crop to a square
    left, top = (icon.width - side) // 2, (icon.height - side) // 2
    icon = icon.crop((left, top, left + side, top + side)).resize((512, 512), Image.Resampling.LANCZOS)
    background = Image.new('RGB', icon.size, (22, 24, 30))
    background.paste(icon, mask=icon.getchannel('A'))
    background.save(out / 'icon0.png')
print('icon:', custom or 'default Eden logo')
pic = next((p for p in (branding / 'pic1.png', branding / 'pic1.jpg') if p.exists()), None)
if pic:
    with Image.open(pic) as original:
        original.convert('RGB').resize((1920, 1080), Image.Resampling.LANCZOS).save(out / 'pic1.png')
    print('background:', pic)
PY

# Base PS4 mode. PS4 Pro mode (0x00800000) hung the GPU and crashed the console: the RADV port
# describes the base GPU (2 shader engines, 18 CUs, 8 RBs). 0x00010000 = seventh CPU core.
ATTRIBUTE="${EDEN_SFO_ATTRIBUTE:-0}"
SUFFIX="${EDEN_PKG_SUFFIX:-}"
PKG="$(INCLUDE_PIGLET_MODULES=0 SFO_STYLE=plain SFO_ATTRIBUTE="$ATTRIBUTE" "$DOLPHIN_PS4_ROOT/scripts/make-pkg.sh" \
    "$STAGE" "$TITLE_ID" "$NAME" "$VERSION" EDEN4 "$OUT" | tail -1)"
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
"$OPENORBIS/bin/linux/PkgTool.Core" pkg_validate "$PKG"
FINAL="$OUT/$NAME-$VERSION$SUFFIX.pkg"
cp "$PKG" "$FINAL"
printf 'Package: %s\n' "$FINAL"
