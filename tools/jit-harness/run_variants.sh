#!/bin/bash
HARNESS="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
cd "$HARNESS"
./build_harness.sh > /dev/null 2>&1
EA=9d5f20c62bc7c207; EB=a4b0e3d46d695874
for g in guest/out/guest.bin guest/out_x1/guest.bin guest/out_x2/guest.bin; do
 for m in ref packed "direct --noreuse" direct; do
  for sh in "" "--shuffle"; do
   timeout 120 ./build/harness --guest=$g --mode=$m $sh --input=data/a.zst --expect=$EA 2>&1 | grep -a "mode=" | sed "s|^|$g |" | cut -c1-230
  done
 done
done
