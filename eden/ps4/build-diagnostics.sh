#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DOLPHIN_PS4_ROOT="${DOLPHIN_PS4_ROOT:-$(cd "$HERE/../../runtime" && pwd)}"
source "$DOLPHIN_PS4_ROOT/scripts/ps4-env.sh"
BUILD="$PS4_BUILD_ROOT/build/eden-diagnostics"
STAGE="$BUILD/stage"
OUT="$PS4_BUILD_ROOT/out"
ps4_cmake -S "$HERE/diagnostics" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DOLPHIN_PS4_ROOT/toolchain/ps4-love-modern.cmake" \
    -DDOLPHIN_PS4_ROOT="$DOLPHIN_PS4_ROOT" -DPS4_PAID=0x3800000000000035
cmake --build "$BUILD" -j"$JOBS"
mkdir -p "$STAGE/sce_sys" "$OUT"
cp "$BUILD/eden-ps4-diagnostics_eboot/eboot.bin" "$STAGE/eboot.bin"
python3 - "$HERE/../dist/qt_themes/default/icons/256x256/eden.png" "$STAGE/sce_sys/icon0.png" <<'PY'
import sys
from PIL import Image
with Image.open(sys.argv[1]) as source:
    icon = source.convert('RGBA').resize((512, 512), Image.Resampling.LANCZOS)
    background = Image.new('RGB', icon.size, (22, 24, 30))
    background.paste(icon, mask=icon.getchannel('A'))
    background.save(sys.argv[2])
PY
PKG="$(INCLUDE_PIGLET_MODULES=0 SFO_STYLE=plain "$DOLPHIN_PS4_ROOT/scripts/make-pkg.sh" \
    "$STAGE" EDEN00001 'Eden4 Diagnostics' 00.01 EDENDIAGNOSTICS "$OUT" | tail -1)"
cp "$PKG" "$OUT/EdenPS4-Diagnostics-00.01.pkg"
printf 'Diagnostic package (no Switch emulation): %s\n' "$OUT/EdenPS4-Diagnostics-00.01.pkg"
