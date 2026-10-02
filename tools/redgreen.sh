#!/bin/bash
# redgreen.sh <logroot>: the clean engine must give VERDICT GREEN, and each mutant (tools/mutate.py) VERDICT RED.
# Exits 0 only if all three hold.
set -u
T=$(cd "$(dirname "$0")" && pwd)
ROOT=$1
status=0
for run in clean mut1 mut2; do
    mutant=$([ "$run" = clean ] || echo "$run")
    "$T/docker.sh" "$ROOT/$run" $mutant > "$ROOT/$run.out" 2>&1
    verdict=$(grep -h "^VERDICT" "$ROOT/$run/results.txt" 2>/dev/null | awk '{print $2}')
    want=$([ "$run" = clean ] && echo GREEN || echo RED)
    ok=$([ "${verdict:-NONE}" = "$want" ] && echo ok || echo WRONG)
    [ "$ok" = ok ] || status=1
    echo "$run: VERDICT ${verdict:-NONE}, expected $want: $ok" | tee -a "$ROOT/redgreen.txt"
done
exit $status
