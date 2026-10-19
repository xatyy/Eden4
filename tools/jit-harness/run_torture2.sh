#!/bin/bash
HARNESS="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
cd "$HARNESS"
ref=$(./build/harness --torture --mode=ref --noregalloc --shuffle --input=x 2>&1 | grep -a "torture mode" | sed 's/.*status=[0-9]* //;s/ cb=.*//')
echo "reference: $ref"
fail=0
for sh in "" "--shuffle"; do for ra in "" "--noregalloc"; do
  for m in "ref" "packed" "direct --noreuse" "direct"; do
    out=$(timeout 90 ./build/harness --torture --mode=$m --input=x $sh $ra 2>&1 | grep -a "torture mode")
    got=$(echo "$out" | sed 's/.*status=[0-9]* //;s/ cb=.*//')
    if [ "$got" = "$ref" ]; then r=OK; else r=MISMATCH; fail=1; fi
    echo "$r  $m $sh $ra :: $(echo "$out" | sed 's/.*cb=/cb=/')"
  done
done; done
echo "torture fail=$fail"
