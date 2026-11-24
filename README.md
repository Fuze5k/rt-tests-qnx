# rt-tests-qnx

[rt-tests](https://git.kernel.org/pub/scm/utils/rt-tests/rt-tests.git) (cyclictest and friends, the standard
real-time test suite on Linux) ported to **QNX OS 8.0**, plus **msglat**, a new QNX-native tool that measures
`MsgSend()`, pulse and timer-event latency in cyclictest's output format.

This repository holds only our work: patches, a compatibility layer, build files, msglat, scripts and results.
The upstream sources are fetched by URL at a pinned commit (`UPSTREAM_COMMIT`, rt-tests 2.11); this is not a fork.

| | |
|---|---|
| QNX version | QNX OS 8.0 (SDP 8.0, QNX Everywhere), `uname`: `QNX 8.0.0 ... QEMU_virt aarch64le` |
| Target | QEMU `virt`, aarch64le, 2 CPUs, image from `mkqnximage` (TCG: HVF does not boot QNX 8.0) |
| Toolchain | `qcc -Vgcc_ntoaarch64le` from the SDP 8.0 host tools |
| Upstream | rt-tests 2.11 at `62da2befac98f811af8e56f2b7992fb09faa33d6` (GPL-2.0) |
| Licence | GPL-2.0 for upstream code and the patches; new files GPL-2.0-or-later (see `LICENSE`) |

## What runs

Ported and run on the target (`scripts/suite.sh` runs all of them):

`cyclictest`, `signaltest`, `ptsematest`, `sigwaittest`, `pmqtest`, `pi_stress`, `pip_stress`, `hackbench`,
`oslat`, `rt-migrate-test` (all upstream), and `msglat` (new).

Not ported, because they depend on Linux-only kernel features: `ssdd` (ptrace), `cyclicdeadline`,
`deadline_test` (SCHED_DEADLINE), `queuelat`, `hwlatdetect` (Linux ftrace/hwlat), `svsematest` (System V
semaphores). `rt-numa`/libnuma calls are stubbed for a single node.

## Build

Needs the QNX SDP 8.0 host tools (`source ~/qnx800/qnxsdp-env.sh` so that `qcc` is on `PATH`).

```sh
scripts/fetch-upstream.sh      # clones rt-tests, checks out the pinned commit, applies patches/*.patch
make                           # aarch64le -> build/aarch64le/  (ARCH=x86_64 is selectable, not tested)
```

`make` does not track header dependencies: run `make clean` after editing a header.
On a Mac, the SDP host tools run in an x86-64 Linux container; any way of getting `qcc` on `PATH` works.

## Run on the target

Start a QNX 8.0 aarch64le system under QEMU (our image: `mkqnximage --type=qemu --arch=aarch64le`, ssh
forwarded to host port 2222), then:

```sh
scripts/deploy.sh              # scp the tools and scripts/suite.sh to root@127.0.0.1:/data/rt
ssh -p 2222 root@127.0.0.1
cd /data/rt && export LD_LIBRARY_PATH=/data/rt
./cyclictest -p80 -i1000 -l400
./msglat -q -l1000
./msglat -q --pi -a0 -l300          # priority inheritance on MsgSend
./msglat -q --no-pi -a0 -l300       # same, _NTO_CHF_FIXED_PRIORITY: no inheritance
sh suite.sh                         # everything, 3 repetitions (REPS=n to change)
```

`TARGET`, `PORT`, `KEY` and `DEST` change the connection (see the top of `scripts/deploy.sh`).

## msglat

```
msglat [-m msg|pulse|event] [-p prio] [-i us] [-l loops] [-a cpu] [-h us] [-q] [--pi|--no-pi]
```

| Mode | Measures | Output lines |
|---|---|---|
| `msg` | `MsgSend()` -> `MsgReceive()` -> `MsgReply()` | `RT` round trip seen by the client, `OW` one way (send to receive) |
| `pulse` | `MsgSendPulsePtr()` -> `MsgReceive()`; the sender never blocks | `OW` |
| `event` | a periodic timer whose `sigevent` is a pulse | `OW`, timer expiry to `MsgReceive()` |
| `--pi` | `msg` with a low-priority server, a medium-priority spinner and the client at high priority, one CPU | `RT`, `OW`: the server inherits the client's priority |
| `--no-pi` | the same on a channel created with `_NTO_CHF_FIXED_PRIORITY` | `RT`, `OW`: the spinner starves the server |

Columns are those of cyclictest (`Min`, `Act`, `Avg`, `Max`, microseconds), so existing scripts that parse
cyclictest summaries read msglat too. The server runs at priority P+1, the client at P.

## The port

* `patches/` - three small patches on top of rt-tests 2.11, written as a series that could go to
  linux-rt-users: guards around the Linux-only parts of `cyclictest`, `rt-get_cpu`, `rt-sched`, `rt-utils`,
  `rt-migrate-test`, `pip_stress`; `pi_stress` pins each thread itself; `ptsematest` hands the lock over with
  semaphores (a QNX mutex can only be unlocked by its owner).
* `qnx/include`, `qnx/src` - the compatibility layer, force-included into every upstream file:
  `sched_setaffinity()`/`pthread_setaffinity_np()` and the getters on top of `ThreadCtlExt()` with
  `_NTO_TCTL_RUNMASK_GET_AND_SET_INHERIT`, `gettid()`, `sched_getcpu()`, `cpu_set_t` macros, `SCHED_BATCH` and
  `SCHED_IDLE` placeholders, libnuma stubs, small `<sys/...>` headers.
* `qnx/tools/msglat.c` - the new tool.
* `scripts/` - fetch upstream, deploy to the target, the test suite.

QNX differences worth knowing, and how the port handles them:

* No `SCHED_DEADLINE`, `SCHED_BATCH`, `SCHED_IDLE`. `SCHED_OTHER` needs priority >= 1 (default 10): cyclictest maps
  priority 0 to it.
* `sched_getcpu()` does not exist in QNX: the shim returns -1, so `-a` pins threads but cannot print the CPU.
* The kernel tick of the QEMU image is 1000 us and `ClockPeriod()` cannot change it there, so cyclictest's
  average sits around 1 ms for a 1000 us interval; `clock_getres()` says 1 ms. msglat prints the period.
* `mmap()` of anonymous memory needs `fd = -1`; the shim/patch handles it.

## Results

Numbers from `scripts/suite.sh`, 3 repetitions, **QEMU TCG on an M3 Mac, emulated, 2 CPUs. These are not QNX
hardware numbers.** Compare rows inside one run, not with other systems. Full output:
`results/qemu-tcg-run1.txt`.

| Measurement (average, us) | Run 1 | Run 2 | Run 3 | What it shows |
|---|---|---|---|---|
| msglat `msg`, RT | 40 | 37 | 46 | message round trip |
| msglat `msg`, OW | 15 | 15 | 17 | one way |
| msglat `pulse`, OW | 46 | 33 | 39 | pulse delivery |
| msglat `event`, OW | 1147 | 1173 | 1103 | timer event waits for the 1 ms tick |
| msglat `--pi`, RT | 14 | 15 | 18 | priority inheritance on |
| msglat `--no-pi`, RT | 879 | 939 | 868 | inheritance off: spinner starves the server (max about 20 ms) |
| cyclictest FIFO p80, 2 busy loops | 1157 | 815 | 1245 | stays on time under load |
| cyclictest SCHED_OTHER, 2 busy loops | 3224 | 3537 | 3694 | delayed by the loops (max 10.6-12.2 ms) |

The tools run and print correct output, and the contrasts are the same in every run (FIFO against OTHER, PI on
against off). Absolute latencies under TCG say little about hardware.

Known limits: `pip_stress` sometimes reports "No inversion incurred"; `oslat` reports a minimum of 0 us under
emulation; the patches are compile-tested on QNX only, not on Linux.

## Credits

rt-tests is by Clark Williams, John Kacur, Thomas Gleixner and many others (GPL-2.0); this repository adds the
QNX port and msglat only.
