#!/bin/sh
# Fetch rt-tests at the pinned commit into upstream/ and apply patches/*.patch.
# Idempotent: re-running resets upstream/ to the pinned commit and re-applies the patches.
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")/.."
URL=https://git.kernel.org/pub/scm/utils/rt-tests/rt-tests.git
PIN=$(cat UPSTREAM_COMMIT)
if [ ! -d upstream/.git ]; then
    git clone -q "$URL" upstream
fi
cd upstream
git fetch -q origin "$PIN" 2>/dev/null || git fetch -q origin
git checkout -q -f --detach "$PIN"
git clean -qfdx
n=0
for p in ../patches/*.patch; do
    [ -e "$p" ] || continue
    git -c user.name=qnx-port -c user.email=qnx-port@localhost am -q "$p"
    n=$((n + 1))
done
echo "upstream/ = rt-tests $(git describe --tags --always) + $n patch(es)"
