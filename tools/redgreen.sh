#!/bin/bash
# redgreen.sh <logroot> [run...]: the clean engine must give VERDICT GREEN, and every mutant in tools/mutate.py's
# REQUIRED_RED VERDICT RED. Exits 0 only if all of them hold. Naming runs (clean, or mutants) runs only those.
set -u
T=$(cd "$(dirname "$0")" && pwd)
ROOT=$1
mkdir -p "$ROOT"
status=0
MUTANTS=$(python3 -c "import re; print(' '.join(eval(re.search(r'REQUIRED_RED = (\[.*?\])', open('$T/mutate.py').read()).group(1))))")
RUNS=${*:2}
for run in ${RUNS:-clean $MUTANTS}; do
    mutant=$([ "$run" = clean ] || echo "$run")
    "$T/docker.sh" "$ROOT/$run" $mutant > "$ROOT/$run.out" 2>&1
    verdict=$(grep -h "^VERDICT" "$ROOT/$run/results.txt" 2>/dev/null | awk '{print $2}')
    want=$([ "$run" = clean ] && echo GREEN || echo RED)
    ok=$([ "${verdict:-NONE}" = "$want" ] && echo ok || echo WRONG)
    [ "$ok" = ok ] || status=1
    echo "$run: VERDICT ${verdict:-NONE}, expected $want: $ok" | tee -a "$ROOT/redgreen.txt"
done
exit $status
