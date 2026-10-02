#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# run_qemu.sh <logdir>: inside espressif/idf:v6.0.3 (see docker.sh), in the repo root: write sdkconfig from the
# engine's, build, start the local servers, run the image in QEMU (esp32, open_eth, user-mode networking) until
# STANDALONE_DONE, stop everything. QEMU reaches this container as 10.0.2.2, so the servers listen here.
# MUTANT=<name> in the environment builds a mutated COPY of the engine instead (tools/mutate.py). Every output of an
# earlier run in <logdir> is removed first, so a failed build or run leaves no results.txt (and no VERDICT) behind.
set -u
cd "$(dirname "$0")/.."
LOG=$1
mkdir -p "$LOG"
rm -rf "$LOG"/* 2>/dev/null
git config --global --add safe.directory '*'
. $IDF_PATH/export.sh > /dev/null 2>&1

if [ -n "${MUTANT:-}" ]; then
    cp -a engine "/tmp/engine-$MUTANT"
    python3 tools/mutate.py "$MUTANT" "/tmp/engine-$MUTANT/software/wifi/raw_u64/main/tls_engine.c" \
        > "$LOG/mutate.txt" 2>&1 || { cat "$LOG/mutate.txt"; echo "MUTANT_NOT_APPLIED"; exit 1; }
    git -C "/tmp/engine-$MUTANT" diff > "$LOG/mutant.diff"
    export ENGINE_DIR="/tmp/engine-$MUTANT" ENGINE_MUTANT="$MUTANT"
fi
{ idf.py --version; openssl version; qemu-system-xtensa --version | head -1; uname -m;
  echo "engine $(git -C engine rev-parse HEAD)"; } > "$LOG/versions.txt" 2>&1
python3 tools/make_sdkconfig.py > "$LOG/make_sdkconfig.txt"
# GCC takes __DATE__/__TIME__ from it: the engine's clock floor (tls_build_time()) is then the commit's date
export SOURCE_DATE_EPOCH=$(git -C engine log -1 --format=%ct)
echo "SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH" >> "$LOG/versions.txt"
rm -rf build
idf.py build > "$LOG/build.log" 2>&1
echo "BUILD_EXIT $?" | tee -a "$LOG/build.log"
grep -q "Project build complete" "$LOG/build.log" || exit 1
idf.py size > "$LOG/size.txt" 2>&1
tools/diff_sdkconfig.sh > "$LOG/sdkconfig.diff" 2>&1
sha256sum build/tls_standalone.bin build/bootloader/bootloader.bin build/partition_table/partition-table.bin \
    "${ENGINE_DIR:-engine}/software/wifi/raw_u64/main/tls_engine.c" engine/software/wifi/raw_u64/main/tls_engine.h \
    > "$LOG/image.sha256"

tools/servers.sh start "$LOG"
idf.py qemu > "$LOG/qemu-raw.log" 2>&1 < /dev/null &
QPID=$!
echo "$QPID idf.py qemu" >> "$LOG/pids.txt"
for i in $(seq 1 1800); do
    grep -q "STANDALONE_DONE\|NET failed\|Guru Meditation\|abort()" "$LOG/qemu-raw.log" && break
    sleep 1
done
sleep 2
pkill -f qemu-system-xtensa; kill $QPID 2>/dev/null
tools/servers.sh stop "$LOG"
pkill -f "openssl s_server"; pkill -f noems_server.py; pkill -f sntp_server.py; pkill -f app_server.py
sleep 1
pgrep -fa "qemu-system|s_server|noems|sntp_server|app_server" > "$LOG/leftover-processes.txt" || echo "none" > "$LOG/leftover-processes.txt"
tr -d '\r' < "$LOG/qemu-raw.log" > "$LOG/qemu.log"
grep -E "^(RESULT|CASE|SUMMARY|VERDICT|CLOCK|HEAP|NET|1541u-tls|\*\*\* MUTANT)" "$LOG/qemu.log" > "$LOG/results.txt"
python3 tools/results.py "$LOG/results.txt" > "$LOG/results.md"
grep -E "^(SUMMARY|VERDICT)" "$LOG/results.txt" || echo "VERDICT NONE (the run did not finish)"
echo "RUN_DONE"
