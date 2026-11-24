#!/bin/sh
# Copy the built tools and the test suite to a QNX 8.0 target over scp, optionally run the suite there.
#
#   scripts/deploy.sh                 # copy to root@127.0.0.1:/data/rt (QEMU with port 2222 forwarded to 22)
#   scripts/deploy.sh run             # copy, then run scripts/suite.sh on the target
#
# Environment: TARGET (root@127.0.0.1), PORT (2222), KEY (ssh identity file, optional), DEST (/data/rt),
# ARCH (aarch64le).
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")/.."
TARGET=${TARGET:-root@127.0.0.1}
PORT=${PORT:-2222}
DEST=${DEST:-/data/rt}
ARCH=${ARCH:-aarch64le}
OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5"
[ -n "${KEY:-}" ] && OPTS="$OPTS -i $KEY"
ssh $OPTS -p "$PORT" "$TARGET" "mkdir -p $DEST"
files=""
for f in build/$ARCH/*; do
    [ -x "$f" ] && [ -f "$f" ] && files="$files $f"
done
scp $OPTS -P "$PORT" $files scripts/suite.sh "$TARGET:$DEST/"
echo "copied to $TARGET:$DEST"
if [ "${1:-}" = run ]; then
    ssh $OPTS -p "$PORT" "$TARGET" "cd $DEST && LD_LIBRARY_PATH=$DEST sh suite.sh"
fi
