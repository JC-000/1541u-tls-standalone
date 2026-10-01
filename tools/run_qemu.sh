#!/bin/bash
# run_qemu.sh <logdir>: inside espressif/idf:v6.0.3 (see docker.sh): build, start the local OpenSSL servers, run the
# image in QEMU (esp32, open_eth, user-mode networking) until STANDALONE_DONE, stop everything.
# QEMU reaches this container as 10.0.2.2, so the servers (tools/servers.sh) listen here.
set -u
cd "$(dirname "$0")/.."
APP=$PWD
LOG=${1:-$APP/logs}
mkdir -p "$LOG"
git config --global --add safe.directory '*'
. $IDF_PATH/export.sh > /dev/null 2>&1

{ idf.py --version; openssl version; qemu-system-xtensa --version | head -1; } > "$LOG/versions.txt" 2>&1
rm -rf build
idf.py build > "$LOG/build.log" 2>&1
echo "BUILD_EXIT $?" | tee -a "$LOG/build.log"
grep -q "Project build complete" "$LOG/build.log" || exit 1
idf.py size > "$LOG/size.txt" 2>&1
tools/diff_sdkconfig.sh > "$LOG/sdkconfig.diff" 2>&1
sha256sum build/tls_standalone.bin build/bootloader/bootloader.bin build/partition_table/partition-table.bin \
    engine/software/wifi/raw_u64/main/tls_engine.c engine/software/wifi/raw_u64/main/tls_engine.h > "$LOG/image.sha256"

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
tools/servers.sh stop "$LOG"; rm -rf "$LOG/www"
pkill -f "openssl s_server"; pkill -f noems_server.py
sleep 1
pgrep -fa "qemu-system|s_server|noems" > "$LOG/leftover-processes.txt" || echo "none" > "$LOG/leftover-processes.txt"
tr -d '\r' < "$LOG/qemu-raw.log" > "$LOG/qemu.log"
grep -E "^(RESULT|CASE|SUMMARY|CLOCK|HEAP|NET|1541u-tls)" "$LOG/qemu.log" > "$LOG/results.txt"
python3 tools/results.py "$LOG/results.txt" > "$LOG/results.md"
tail -3 "$LOG/results.txt"
echo "RUN_DONE"
