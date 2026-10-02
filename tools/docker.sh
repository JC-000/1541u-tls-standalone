#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# docker.sh <logdir> [mutant]: run_qemu.sh in espressif/idf:v6.0.3, the image for this machine's architecture
# pinned by digest. Only the repo (as /work, with ./engine from tools/fetch_engine.sh) and <logdir> (as /out) are
# mounted.
set -u
APP=$(cd "$(dirname "$0")/.." && pwd)
case "$(uname -m)" in
    arm64|aarch64) DIGEST=sha256:da1f22c157dcb4f684f822ff37be17dc0abef82160d889bfd7fd23cc1299f0fa ;;
    x86_64|amd64)  DIGEST=sha256:01ab23e20744721378d1dd9d0fff4fe15e2edd8bd5cdaf02d24597f4ade05a1e ;;
    *) echo "no pinned image for $(uname -m)" >&2; exit 1 ;;
esac
mkdir -p "$1"
LOG=$(cd "$1" && pwd)
echo "image espressif/idf@$DIGEST (v6.0.3, $(uname -m))" > "$LOG/image.txt"
docker run --rm -e MUTANT="${2:-}" -v "$APP:/work" -v "$LOG:/out" -w /work "espressif/idf@$DIGEST" \
    bash tools/run_qemu.sh /out
