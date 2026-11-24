#!/bin/sh
# Thin wrapper: scripts/qemu.sh ssh <cmd>  (VM dir from $QNX_VM, run-qemu.sh from $RUN_QEMU)
H=$(cd "$(dirname "$0")/.." && pwd)
exec "${RUN_QEMU:-$H/../../qnx/qtrace/env/run-qemu.sh}" "${QNX_VM:-$H/../../qnx/vm/aarch64}" "$@"
