#!/bin/bash
HARNESS="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
# Full regression: every guest variant x mode x shuffle x allocator setting, plus the memory torture.
cd "$HARNESS"
./build_dynarmic.sh > /dev/null 2>&1
./build_harness.sh > /dev/null 2>&1
EA=9d5f20c62bc7c207; EB=a4b0e3d46d695874
fail=0; total=0
for g in guest/out/guest.bin guest/out_x1/guest.bin guest/out_x2/guest.bin; do
 for m in ref packed "direct --noreuse" direct; do
  for sh in "" "--shuffle"; do
   for ra in "" "--noregalloc" "--nofreemoves" "--nor13"; do
    for inp in "data/a.zst $EA" "data/b.zst $EB"; do
     set -- $inp
     out=$(timeout 120 ./build/harness --guest=$g --mode=$m $sh $ra --input=$1 --expect=$2 2>&1 | grep -a "mode=" | tail -1)
     total=$((total+1))
     if ! echo "$out" | grep -q " OK"; then fail=$((fail+1)); echo "FAIL: $g $m $sh $ra $1 :: $out" | cut -c1-260; fi
    done
   done
  done
 done
done
echo "zstd runs: $total, failures: $fail"
ref=$(./build/harness --torture --mode=ref --noregalloc --shuffle --input=x 2>&1 | grep -a "torture mode" | sed 's/.*status=[0-9]* //;s/ cb=.*//')
tf=0
for sh in "" "--shuffle"; do for ra in "" "--noregalloc" "--nofreemoves"; do for m in ref packed "direct --noreuse" direct; do
  got=$(timeout 90 ./build/harness --torture --mode=$m --input=x $sh $ra 2>&1 | grep -a "torture mode" | sed 's/.*status=[0-9]* //;s/ cb=.*//')
  [ "$got" = "$ref" ] || { tf=$((tf+1)); echo "TORTURE MISMATCH: $m $sh $ra"; }
done; done; done
echo "torture failures: $tf"
