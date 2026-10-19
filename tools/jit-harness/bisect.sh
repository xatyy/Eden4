#!/bin/bash
HARNESS="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
cd "$HARNESS"
./build_dynarmic.sh > /dev/null 2>&1
./build_harness.sh > /dev/null 2>&1
E=9d5f20c62bc7c207
run() { ./build/harness --mode=ref --noreuse --input=data/a.zst --expect=$E --movelimit=$1 2>&1 | tail -2; }
total=$(./build/harness --mode=ref --noreuse --input=data/a.zst --expect=$E 2>&1 | tail -1 | grep -c OK)
echo "unlimited ok=$total"
lo=0; hi=200000
# find the smallest limit that fails
while [ $((hi-lo)) -gt 1 ]; do
  mid=$(((lo+hi)/2))
  if run $mid | tail -1 | grep -q OK; then lo=$mid; else hi=$mid; fi
done
echo "first failing limit: $hi"
run $hi
