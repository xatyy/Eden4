#!/bin/bash
HARNESS="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
cd "$HARNESS"
./build_harness.sh > /dev/null 2>&1
G=guest/out_x2/guest.bin
./build/harness --guest=$G --mode=direct --noreuse --input=data/a.zst --state --ticks=339438 --save=build/snap.bin | cut -c1-120
START=$((0x20000052cc))
run() { timeout 60 ./build/harness --guest=$G --mode=direct --noreuse --input=data/a.zst --state --load=build/snap.bin --patch=$(printf '%x' $2) $1 2>&1 | grep -a "^STATE" | sed 's/ticks=[0-9]* //'; }
for k in $(seq 1 150); do
  addr=$((START + 4*k))
  a=$(run "--nofreemoves" $addr); b=$(run "" $addr)
  if [ "$a" != "$b" ]; then echo "first mismatch at instruction $k (patched svc at $(printf '%x' $addr))"; echo "A: $a" | cut -c1-400; echo "B: $b" | cut -c1-400; break; fi
done
echo scan-done
