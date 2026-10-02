#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# selftest_redgreen.sh: redgreen.sh's own red/green, with a fake docker.sh (no Docker, no build). A run over an older
# GREEN or RED must not inherit it: a failed build, a mutant that did not apply, or a build of another engine commit
# must come out WRONG, and a missing python3 must stop redgreen. Exits 0 only if every expectation holds.
set -u
SRC=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/tools"
cp "$SRC/tools/redgreen.sh" "$SRC/tools/mutate.py" "$W/tools/"
SHA=$(sed -n 's/^ENGINE_SHA=//p' "$SRC/engine.env")
echo "ENGINE_SHA=$SHA" > "$W/engine.env"
cat > "$W/tools/docker.sh" <<'FAKE'
#!/bin/bash
# fake docker.sh <logdir> [mutant]: behaves as $FAKE says
mkdir -p "$1"
case "$FAKE" in
    build-fails)   echo "BUILD_EXIT 2" > "$1/build.log" ;;
    not-applied)   echo "MUTANT_NOT_APPLIED" > "$1/mutate.txt" ;;
    other-engine)  printf -- "-- engine: 0000000000000000000000000000000000000000, unmodified\nBUILD_EXIT 0\n" > "$1/build.log"
                   echo "VERDICT $([ -n "${2:-}" ] && echo RED || echo GREEN)" > "$1/results.txt" ;;
    good)          printf -- "-- engine: %s, unmodified\nBUILD_EXIT 0\n" "$SHA" > "$1/build.log"
                   echo "VERDICT $([ -n "${2:-}" ] && echo RED || echo GREEN)" > "$1/results.txt" ;;
esac
FAKE
chmod +x "$W/tools/docker.sh"
fail=0
check() { # name expected_exit expected_text cmd...
    local name=$1 want_rc=$2 want=$3; shift 3
    out=$("$@" 2>&1); rc=$?
    if [ "$rc" = "$want_rc" ] && echo "$out" | grep -q -- "$want"; then r=ok; else r=WRONG; fail=1; fi
    printf "%-44s exit %s, wanted %s and \"%s\": %s\n" "$name" "$rc" "$want_rc" "$want" "$r"
}
seed() { mkdir -p "$W/out/$1"; echo "VERDICT $2" > "$W/out/$1/results.txt"; echo "BUILD_EXIT 0" > "$W/out/$1/build.log"; }
export SHA
seed clean GREEN
check "failed build over an old GREEN"          1 "clean: VERDICT NONE" env FAKE=build-fails "$W/tools/redgreen.sh" "$W/out" clean
seed m6b RED
check "mutant not applied over an old RED"      1 "m6b: VERDICT NONE"   env FAKE=not-applied "$W/tools/redgreen.sh" "$W/out" m6b
check "build of another engine commit"          1 "clean: VERDICT NONE" env FAKE=other-engine "$W/tools/redgreen.sh" "$W/out" clean
check "control: good clean run"                 0 "clean: VERDICT GREEN, expected GREEN: ok" env FAKE=good "$W/tools/redgreen.sh" "$W/out" clean
check "control: good mutant run"                0 "m6b: VERDICT RED, expected RED: ok" env FAKE=good "$W/tools/redgreen.sh" "$W/out" m6b
mkdir -p "$W/bin"
for t in bash sed grep awk tr rm mkdir tee dirname cat env; do ln -s "$(command -v $t)" "$W/bin/$t"; done
check "no python3"                              2 "python3 is required" env PATH="$W/bin" FAKE=good "$W/tools/redgreen.sh" "$W/out" clean
exit $fail
