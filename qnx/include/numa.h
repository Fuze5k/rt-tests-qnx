/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * numa.h - the subset of libnuma that rt-tests uses, for QNX 8.0.
 * QNX has no NUMA API: the machine is one node, numa_available() says -1 and
 * the bit-mask helpers work on the CPUs the kernel started with. The CPU
 * affinity calls go through the QNX run mask (see qnx-compat.h).
 */
#ifndef QNX_NUMA_H
#define QNX_NUMA_H
#include <qnx-compat.h>

struct bitmask {
	unsigned long size;      /* in bits */
	unsigned long *maskp;
};

int numa_available(void);
struct bitmask *numa_allocate_cpumask(void);
struct bitmask *numa_parse_cpustring_all(const char *s);
void numa_bitmask_free(struct bitmask *m);
unsigned int numa_bitmask_weight(const struct bitmask *m);
int numa_bitmask_isbitset(const struct bitmask *m, unsigned int n);
struct bitmask *numa_bitmask_clearbit(struct bitmask *m, unsigned int n);
int numa_sched_getaffinity(pid_t pid, struct bitmask *m);
int numa_sched_setaffinity(pid_t pid, struct bitmask *m);
int numa_node_of_cpu(int cpu);
int numa_run_on_node(int node);
void *numa_alloc_onnode(size_t size, int node);
void numa_free(void *p, size_t size);
#endif
