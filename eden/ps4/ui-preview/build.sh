#!/usr/bin/env bash
# Builds the HOME menu preview on a PC and renders every screen to PNG: ps4/ui-preview/build.sh <out-dir>
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
EDEN="$HERE/../.."
OUT="${1:-ui-preview-out}"
mkdir -p "$OUT/gen"
python3 -I "$EDEN/ps4/generate-ui-assets.py" "$EDEN/../runtime/fonts/Nunito-Regular.ttf" "$OUT/gen/ps4_ui_assets.h"
c++ -std=c++20 -O2 -I "$EDEN/src" -I "$EDEN/externals/stb" -I "$OUT/gen" "$HERE/preview.cpp" \
    "$EDEN/src/yuzu_ps4/ui_launcher.cpp" "$EDEN/src/yuzu_ps4/ui_canvas.cpp" "$EDEN/src/common/stb.cpp" -o "$OUT/preview"
(cd "$OUT" && ./preview . && python3 -I -c "
import glob
from PIL import Image
for f in sorted(glob.glob('*.rgba')):
    Image.frombytes('RGBA', (1920, 1080), open(f, 'rb').read()).convert('RGB').save(f[:-5] + '.png')
")
rm -f "$OUT"/*.rgba
echo "Screens in $OUT"
