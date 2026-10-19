#!/bin/bash
HARNESS="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
# Native (arm64 LLVM) container: builds the aarch64 guest.
set -e
Z=$REPO/eden/.cache/cpm/zstd/b8d6101fba/lib
G="$HARNESS/guest"
OUT=$G/out${VARIANT}; mkdir -p $OUT
RES=$(clang-21 -print-resource-dir)
SRC="$Z/common/debug.c $Z/common/entropy_common.c $Z/common/error_private.c $Z/common/fse_decompress.c $Z/common/xxhash.c $Z/common/zstd_common.c \
 $Z/decompress/huf_decompress.c $Z/decompress/zstd_ddict.c $Z/decompress/zstd_decompress.c $Z/decompress/zstd_decompress_block.c"
CF="--target=aarch64-none-elf -O2 -ffreestanding -fno-builtin -nostdinc -isystem $G/shim -isystem $RES/include -fno-stack-protector -fno-pic -mno-outline-atomics \
 -DZSTD_DISABLE_ASM=1 $EXTRA_DEFS -DDEBUGLEVEL=0 -DZSTD_LEGACY_SUPPORT=0 -DBASE=0x2000000000ULL -I$Z -I$Z/common -I$Z/decompress -w"
for f in $SRC $G/main.c $G/crt.c; do clang-21 $CF -c $f -o $OUT/$(basename $f).o; done
clang-21 --target=aarch64-none-elf -c $G/start.S -o $OUT/start.o
ld.lld-21 -Ttext=0x2000000000 -e _start -o $OUT/guest.elf $OUT/start.o $OUT/*.c.o
llvm-objcopy-21 -O binary $OUT/guest.elf $OUT/guest.bin
ls -l $OUT/guest.bin
