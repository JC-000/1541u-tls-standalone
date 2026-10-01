#!/bin/bash
# docker.sh <logdir>: run_qemu.sh in espressif/idf:v6.0.3. $HOME/Documents is mounted read-only at the same path, so
# the "engine" symlink and its git worktree resolve inside the container; only this repo and <logdir> are writable.
set -u
APP=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=espressif/idf:v6.0.3@sha256:54278f2c01e6e759502e2f04c9103589c17528923426f4d63193490c7a08f555
mkdir -p "$1"
LOG=$(cd "$1" && pwd)
docker run --rm -v "$HOME/Documents:$HOME/Documents:ro" -v "$APP:$APP" -v "$LOG:$LOG" -w "$APP" "$IMAGE" \
    bash tools/run_qemu.sh "$LOG"
