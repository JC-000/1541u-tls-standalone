#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
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
git -C engine remote add source "$URL"
# By commit; if that commit is no longer reachable from a branch, by ENGINE_TAG, which must name the same commit
# (ENGINE_FETCH_BY_TAG=1 takes the tag path directly)
TARGET=FETCH_HEAD
if [ -n "${ENGINE_FETCH_BY_TAG:-}" ] || ! git -C engine fetch -q --depth 1 --filter=blob:none source "$ENGINE_SHA"; then
    echo "fetching tag ${ENGINE_TAG:?no ENGINE_TAG in engine.env}" >&2
    git -C engine fetch -q --depth 1 --filter=blob:none source "refs/tags/$ENGINE_TAG:refs/tags/$ENGINE_TAG"
    TARGET="refs/tags/$ENGINE_TAG^{commit}"
fi
git -C engine -c advice.detachedHead=false checkout -q "$TARGET"
HEAD_SHA=$(git -C engine rev-parse HEAD)
[ "$HEAD_SHA" = "$ENGINE_SHA" ] || { echo "fetched $HEAD_SHA, not ENGINE_SHA $ENGINE_SHA" >&2; exit 1; }
echo "engine: $ENGINE_SHA"
(cd engine/software/wifi/raw_u64/main && sha256sum tls_engine.c tls_engine.h 2>/dev/null || shasum -a 256 tls_engine.c tls_engine.h)
