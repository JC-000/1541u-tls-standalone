#!/bin/bash
# fetch_engine.sh: fetches ENGINE_SHA (engine.env) from ENGINE_URL into ./engine, a git checkout of that commit with
# only the paths this app uses. ENGINE_URL_ENV in the environment overrides engine.env's URL (a mirror or a local clone).
set -euo pipefail
cd "$(dirname "$0")/.."
. ./engine.env
URL=$ENGINE_URL
if [ -n "${ENGINE_URL_ENV:-}" ]; then URL=$ENGINE_URL_ENV; fi
case "$URL" in "["*) echo "engine.env: ENGINE_URL is still a placeholder; set ENGINE_URL_ENV=<url or clone>" >&2; exit 1;; esac
rm -rf engine
git init -q engine
git -C engine sparse-checkout set software/wifi software/io/uart software/io/wifi software/io/network tools/ca_store
git -C engine fetch -q --depth 1 "$URL" "$ENGINE_SHA"
git -C engine -c advice.detachedHead=false checkout -q FETCH_HEAD
test "$(git -C engine rev-parse HEAD)" = "$ENGINE_SHA"
echo "engine: $ENGINE_SHA"
(cd engine/software/wifi/raw_u64/main && sha256sum tls_engine.c tls_engine.h 2>/dev/null || shasum -a 256 tls_engine.c tls_engine.h)
