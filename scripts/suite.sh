#!/bin/sh
# Runs on the QNX target, from /data/rt. Each measurement is repeated $REPS times.
# Numbers from QEMU TCG on an M3 Mac are emulated: compare inside one run, not with hardware.
REPS=${REPS:-3}
cd /data/rt || exit 1
HOGS=""
hogs_start() { for n in 1 2; do sh -c 'while :; do :; done' & HOGS="$HOGS $!"; done; }
hogs_stop()  { kill $HOGS 2>/dev/null; HOGS=""; wait 2>/dev/null; }
trap hogs_stop EXIT
i=1
while [ $i -le $REPS ]; do
  echo "=== run $i: idle system"
  echo "--- cyclictest SCHED_FIFO p80";  ./cyclictest -q -t1 -p80 -i1000 -l500 | tail -1
  echo "--- cyclictest 2 threads, -a";   ./cyclictest -q -t2 -a -p80 -i1000 -l300 | tail -2
  echo "--- msglat msg";   ./msglat -q -l1000 | grep -E '^(RT|OW)'
  echo "--- msglat pulse"; ./msglat -q -m pulse -l1000 | grep -E '^(RT|OW)'
  echo "--- msglat event"; ./msglat -q -m event -l1000 | grep -E '^(RT|OW)'
  echo "--- msglat --pi    (server below client, spinner between, same CPU)"; ./msglat -q --pi -a0 -l300 | grep -E '^(RT|OW)'
  echo "--- msglat --no-pi (same, _NTO_CHF_FIXED_PRIORITY)"; ./msglat -q --no-pi -a0 -l300 | grep -E '^(RT|OW)'
  echo "=== run $i: 2 busy loops at priority 10"
  hogs_start
  echo "--- cyclictest SCHED_FIFO p80";  ./cyclictest -q -t1 -p80 -i1000 -l500 | tail -1
  echo "--- cyclictest SCHED_OTHER";     ./cyclictest -q -t1 -p0 --policy=other -i1000 -l500 | tail -1
  hogs_stop
  i=$((i+1))
done
echo "=== other tools"
echo "--- signaltest";    ./signaltest -q -l500 -p80 2>&1 | tail -2
echo "--- ptsematest";    ./ptsematest -q -l1000 -p80 2>&1 | tail -1
echo "--- sigwaittest";   ./sigwaittest -q -l1000 -p80 2>&1 | tail -1
echo "--- pmqtest";       ./pmqtest -q -l1000 -p80 2>&1 | tail -1
echo "--- pi_stress";     ./pi_stress -q -D5 -g1 -m 2>&1 | tail -4
echo "--- pip_stress";    ./pip_stress 2>&1 | tail -3
echo "--- hackbench";     ./hackbench -l 100 -g 2 2>&1 | tail -1
echo "--- oslat";         ./oslat -D5 -c1 -f80 2>&1 | tail -8
echo "--- rt-migrate-test"; ./rt-migrate-test -q -l20 -r10 -s20 2>&1 | tail -4
