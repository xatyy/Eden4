#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="${1:?usage: build-cpu-smoke.sh output-directory}"
mkdir -p "$DEST"
clang-21 --target=aarch64-none-elf -c "$HERE/cpu-smoke.S" -o "$DEST/cpu-smoke.o"
ld.lld-21 --image-base=0 -Ttext=0 --oformat=binary "$DEST/cpu-smoke.o" -o "$DEST/cpu-smoke.nro"
python3 - "$DEST/cpu-smoke.nro" <<'PY'
import struct
import sys
from pathlib import Path
blob = Path(sys.argv[1]).read_bytes()
assert len(blob) == 0x3000
assert blob[0x10:0x14] == b'NRO0'
assert blob[8:16] == b'HOMEBREW'
assert struct.unpack_from('<I', blob, 0x18)[0] == len(blob)
assert struct.unpack_from('<6I', blob, 0x20) == (0, 0x1000, 0x1000, 0x1000, 0x2000, 0x1000)
assert b'EDEN_PS4_CPU_SMOKE_PASS' in blob
assert struct.unpack_from('<3Q', blob, 0x2000) == (6, 7, 0)
print('Validated unencrypted CPU smoke-test NRO:', sys.argv[1])
PY
