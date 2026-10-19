#!/bin/bash
HARNESS="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
set -e
W="$REPO"; H="$HARNESS"; Z=$W/eden/.cache/cpm/zstd/b8d6101fba/lib
FLAGS=$(cat $H/build/flags.txt)
cd $H
g++ $FLAGS -O1 harness.cpp build/libdynarmic_host.a -lpthread -o build/harness
if [ ! -x build/ztool ]; then
  gcc -O2 -w -DZSTD_MULTITHREAD=0 -I$Z -I$Z/common -I$Z/compress -I$Z/decompress ztool_dummy 2>/dev/null || true
  gcc -O2 -w -DZSTD_DISABLE_ASM=1 -I$Z -I$Z/common -I$Z/compress -I$Z/decompress guest/ztool.c $Z/common/*.c $Z/compress/*.c $Z/decompress/*.c -o build/ztool
fi
