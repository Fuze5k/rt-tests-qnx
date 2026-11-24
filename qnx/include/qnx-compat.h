/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * qnx-compat.h - the Linux scheduling API that rt-tests uses, on QNX 8.0.
 *
 * Forced into every rt-tests source file with `-include` (see Makefile.qnx).
 * The affinity calls are built on QNX's run mask (ThreadCtl/ThreadCtlExt with
 * _NTO_TCTL_RUNMASK_GET_AND_SET*), the CPU number comes from the thread's
 * CPU index in the system page, gettid() is the QNX thread id.
 */
#ifndef QNX_COMPAT_H
#define QNX_COMPAT_H

#ifndef __QNX__
#error "qnx-compat.h is for QNX only"
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- cpu_set_t: glibc-compatible bit set, 1024 CPUs ---- */
#define CPU_SETSIZE 1024
#define __NCPUBITS (8 * sizeof(unsigned long))
typedef struct { unsigned long __bits[CPU_SETSIZE / (8 * sizeof(unsigned long))]; } cpu_set_t;

#define CPU_ZERO_S(sz, s)    memset((s), 0, (sz))
#define CPU_SET_S(c, sz, s)  ((void)(sz), (s)->__bits[(c) / __NCPUBITS] |= 1UL << ((c) % __NCPUBITS))
#define CPU_CLR_S(c, sz, s)  ((void)(sz), (s)->__bits[(c) / __NCPUBITS] &= ~(1UL << ((c) % __NCPUBITS)))
#define CPU_ISSET_S(c, sz, s) ((void)(sz), ((s)->__bits[(c) / __NCPUBITS] >> ((c) % __NCPUBITS)) & 1UL)
#define CPU_COUNT_S(sz, s)   qnx_cpu_count((s), (sz))
#define CPU_ZERO(s)          CPU_ZERO_S(sizeof(cpu_set_t), s)
#define CPU_SET(c, s)        CPU_SET_S(c, sizeof(cpu_set_t), s)
#define CPU_CLR(c, s)        CPU_CLR_S(c, sizeof(cpu_set_t), s)
#define CPU_ISSET(c, s)      CPU_ISSET_S(c, sizeof(cpu_set_t), s)
#define CPU_COUNT(s)         CPU_COUNT_S(sizeof(cpu_set_t), s)
#define CPU_ALLOC_SIZE(n)    sizeof(cpu_set_t)
#define CPU_ALLOC(n)         ((cpu_set_t *)calloc(1, sizeof(cpu_set_t)))
#define CPU_FREE(s)          free(s)

int qnx_cpu_count(const cpu_set_t *set, size_t size);

/* ---- affinity (run mask) ---- */
/* pid 0 = the calling thread; pid == getpid() = every thread of the process, now and
 * for threads created later (inherit mask). Any other value is treated as a thread id. */
int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask);
int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask);
int pthread_setaffinity_np(pthread_t thread, size_t cpusetsize, const cpu_set_t *mask);
int pthread_getaffinity_np(pthread_t thread, size_t cpusetsize, cpu_set_t *mask);

/* ---- Linux-only constants the sources refer to ----
 * QNX has no SCHED_BATCH/SCHED_IDLE/SCHED_DEADLINE; the values only have to stay distinct from
 * QNX's own policies (SCHED_OTHER is 3 there, so Linux's 3 and 5 cannot be reused). The kernel
 * rejects them with EINVAL, which the tools report like any unsupported policy. */
#include <sched.h>
#include <signal.h>
#ifndef SCHED_BATCH
#define SCHED_BATCH   0x101
#endif
#ifndef SCHED_IDLE
#define SCHED_IDLE    0x102
#endif
/* timer signal aimed at the thread that armed the timer: cyclictest's SIGEV_THREAD_ID */
#ifndef SIGEV_THREAD_ID
#define SIGEV_THREAD_ID SIGEV_SIGNAL_THREAD
#endif

/* ---- glibc extensions ---- */
#include <alloca.h>
#include <string.h>
#ifndef strdupa
#define strdupa(s) \
    ({ const char *__s = (s); size_t __n = strlen(__s) + 1; \
       (char *)memcpy(alloca(__n), __s, __n); })
#endif

/* ---- identity ---- */
pid_t gettid(void);               /* QNX thread id (1-based within the process) */
int sched_getcpu(void);           /* -1: QNX does not tell a thread which CPU it is on */

int qnx_num_cpus(void);           /* CPUs the kernel started with (system page) */

#ifdef __cplusplus
}
#endif
#endif /* QNX_COMPAT_H */
