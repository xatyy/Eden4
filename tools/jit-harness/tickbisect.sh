#!/bin/bash
HARNESS="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
# Finds the smallest tick budget where two allocator settings disagree about the guest state.
cd "$HARNESS"
G=guest/out_x2/guest.bin
state() { timeout 60 ./build/harness --guest=$G --mode=direct --noreuse --input=data/a.zst --state --ticks=$1 $2 2>&1 | grep -a "^STATE"; }
lo=0; hi=${1:-40000000}
same() { [ "$(state $1 "--nofreemoves")" = "$(state $1 "")" ]; }
if same $hi; then echo "no difference at $hi ticks"; exit 0; fi
while [ $((hi-lo)) -gt 1 ]; do
  mid=$(((lo+hi)/2))
  if same $mid; then lo=$mid; else hi=$mid; fi
done
echo "first differing tick budget: $hi"
echo "A (no free moves): $(state $hi --nofreemoves)"
echo "B (free moves):    $(state $hi '')"
echo "previous budget $lo:"
echo "A: $(state $lo --nofreemoves)"
echo "B: $(state $lo '')"
