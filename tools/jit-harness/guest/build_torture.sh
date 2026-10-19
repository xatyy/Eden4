#!/bin/bash
HARNESS="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
set -e
G="$HARNESS/guest"; OUT=$G/out; mkdir -p $OUT
RES=$(clang-21 -print-resource-dir)
CF="--target=aarch64-none-elf -O2 -ffreestanding -fno-builtin -nostdinc -isystem $G/shim -isystem $RES/include -fno-stack-protector -fno-pic -mno-outline-atomics -w -DBASE=0x2000000000ULL"
clang-21 $CF -c $G/torture.c -o $OUT/torture.o
clang-21 --target=aarch64-none-elf -c $G/start.S -o $OUT/start_t.o
ld.lld-21 -Ttext=0x2000000000 -e _start -o $OUT/torture.elf $OUT/start_t.o $OUT/torture.o
llvm-objcopy-21 -O binary $OUT/torture.elf $OUT/torture.bin
ls -l $OUT/torture.bin
