#!/bin/bash
HARNESS="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
cd "$HARNESS"
./build_harness.sh > /dev/null 2>&1
ref=$(./build/harness --torture --mode=ref --noregalloc --input=x 2>&1 | tail -1 | sed 's/.*status=[0-9]* //;s/ cb=.*//')
echo "reference: $ref"
fail=0
for hl in "" "--hostlow"; do for ra in "" "--noregalloc"; do
  for m in "ref" "packed" "direct --noreuse" "direct"; do
    out=$(./build/harness --torture --mode=$m --input=x $hl $ra 2>&1 | tail -1)
    got=$(echo "$out" | sed 's/.*status=[0-9]* //;s/ cb=.*//')
    if [ "$got" = "$ref" ]; then r=OK; else r=MISMATCH; fail=1; fi
    echo "$r  $m $hl $ra :: $(echo "$out" | sed 's/.*cb=/cb=/')"
  done
done; done
echo "torture fail=$fail"
