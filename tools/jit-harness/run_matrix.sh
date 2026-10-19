#!/bin/bash
HARNESS="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
cd "$HARNESS"
./build_dynarmic.sh > /dev/null 2>&1
./build_harness.sh > /dev/null 2>&1
EA=9d5f20c62bc7c207; EB=a4b0e3d46d695874
fail=0
t() { out=$(./build/harness "$@" 2>&1 | tail -1); echo "$out"; echo "$out" | grep -q " OK" || fail=1; }
for hl in "" "--hostlow"; do
 for ra in "" "--noregalloc"; do
  t --mode=ref    --input=data/a.zst --expect=$EA $hl $ra
  t --mode=packed --input=data/a.zst --expect=$EA $hl $ra
  t --mode=direct --noreuse --input=data/a.zst --expect=$EA $hl $ra
  t --mode=direct --input=data/a.zst --expect=$EA $hl $ra
  t --mode=direct --input=data/b.zst --expect=$EB --chunk=777 $hl $ra
  t --mode=direct --input=data/b.zst --expect=$EB --chunk=65536 $hl $ra
 done
done
echo "overall fail=$fail"
