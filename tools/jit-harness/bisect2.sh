#!/bin/bash
HARNESS="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
cd "$HARNESS"
G=guest/out_x2/guest.bin; E=9d5f20c62bc7c207
run() { timeout 60 ./build/harness --guest=$G --mode=direct --noreuse --input=data/a.zst --expect=$E --movelimit=$1 2>&1 | grep -a "mode=" | tail -1; }
lo=0; hi=300000
while [ $((hi-lo)) -gt 1 ]; do
  mid=$(((lo+hi)/2))
  if run $mid | grep -q " OK"; then lo=$mid; else hi=$mid; fi
done
echo "first failing limit: $hi"
timeout 60 ./build/harness --guest=$G --mode=direct --noreuse --input=data/a.zst --expect=$E --movelimit=$hi > build/dump2.txt 2>&1
grep -a "LAST FREE MOVE" build/dump2.txt
objdump -D -b binary -mi386:x86-64 build/block.bin > build/block2.dis
wc -l build/dump2.txt build/block2.dis
