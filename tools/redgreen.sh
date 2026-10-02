#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# redgreen.sh <logroot> [run...]: the clean engine must give VERDICT GREEN, and every mutant in tools/mutate.py's
# REQUIRED_RED VERDICT RED. Naming runs (clean, or mutants) runs only those. A run counts only if its build log says
# BUILD_EXIT 0 for this engine.env's ENGINE_SHA and it printed a VERDICT; anything else (a failed build, a mutant
# whose anchor no longer applies, a run that did not finish) is WRONG. Exits 0 only if every run is as required.
set -u
T=$(cd "$(dirname "$0")" && pwd)
ROOT=$1
mkdir -p "$ROOT"
command -v python3 > /dev/null || { echo "redgreen: python3 is required (it reads tools/mutate.py)" >&2; exit 2; }
SHA=$(sed -n 's/^ENGINE_SHA=//p' "$T/../engine.env")
MUTANTS=$(sed -n 's/^REQUIRED_RED = \[\(.*\)\]$/\1/p' "$T/mutate.py" | tr -d '",')
[ -n "$SHA" ] && [ -n "$MUTANTS" ] || { echo "redgreen: no ENGINE_SHA or no REQUIRED_RED found" >&2; exit 2; }
status=0
RUNS=${*:2}
for run in ${RUNS:-clean $MUTANTS}; do
    mutant=$([ "$run" = clean ] || echo "$run")
    rm -rf "$ROOT/$run"
    "$T/docker.sh" "$ROOT/$run" $mutant > "$ROOT/$run.out" 2>&1
    verdict=$(grep -h "^VERDICT" "$ROOT/$run/results.txt" 2>/dev/null | awk '{print $2}')
    built=$(grep -c "^BUILD_EXIT 0$" "$ROOT/$run/build.log" 2>/dev/null)
    engine=$(grep -c "engine: .*$SHA" "$ROOT/$run/build.log" 2>/dev/null)
    if [ "${built:-0}" = 0 ] || [ "${engine:-0}" = 0 ]; then
        verdict="NONE(build failed or not of $SHA)"
    fi
    want=$([ "$run" = clean ] && echo GREEN || echo RED)
    ok=$([ "${verdict:-NONE}" = "$want" ] && echo ok || echo WRONG)
    [ "$ok" = ok ] || status=1
    echo "$run: VERDICT ${verdict:-NONE}, expected $want: $ok" | tee -a "$ROOT/redgreen.txt"
done
exit $status
