// SPDX-License-Identifier: GPL-2.0-or-later
/* qnx-numa.c - the libnuma subset of numa.h on a single-node QNX system. */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <numa.h>

#define LONG_BITS (8 * sizeof(unsigned long))

int numa_available(void)
{
	return 0;        /* libnuma "works": one node, every CPU on it */
}

static struct bitmask *bitmask_alloc(unsigned int nbits)
{
	struct bitmask *m = malloc(sizeof(*m));
	if (!m)
		return NULL;
	m->size = nbits;
	m->maskp = calloc((nbits + LONG_BITS - 1) / LONG_BITS, sizeof(unsigned long));
	if (!m->maskp) {
		free(m);
		return NULL;
	}
	return m;
}

struct bitmask *numa_allocate_cpumask(void)
{
	return bitmask_alloc(qnx_num_cpus());
}

void numa_bitmask_free(struct bitmask *m)
{
	if (m) {
		free(m->maskp);
		free(m);
	}
}

unsigned int numa_bitmask_weight(const struct bitmask *m)
{
	unsigned int n = 0;
	for (unsigned long i = 0; i < (m->size + LONG_BITS - 1) / LONG_BITS; i++)
		n += __builtin_popcountl(m->maskp[i]);
	return n;
}

int numa_bitmask_isbitset(const struct bitmask *m, unsigned int n)
{
	if (n >= m->size)
		return 0;
	return (m->maskp[n / LONG_BITS] >> (n % LONG_BITS)) & 1;
}

static void setbit(struct bitmask *m, unsigned int n)
{
	if (n < m->size)
		m->maskp[n / LONG_BITS] |= 1UL << (n % LONG_BITS);
}

struct bitmask *numa_bitmask_clearbit(struct bitmask *m, unsigned int n)
{
	if (n < m->size)
		m->maskp[n / LONG_BITS] &= ~(1UL << (n % LONG_BITS));
	return m;
}

/* "all", "3", "0-2,5" (libnuma also knows "+N" and "!N"; not needed here) */
struct bitmask *numa_parse_cpustring_all(const char *s)
{
	struct bitmask *m = numa_allocate_cpumask();
	if (!m)
		return NULL;
	if (!strcmp(s, "all")) {
		for (unsigned int i = 0; i < m->size; i++)
			setbit(m, i);
		return m;
	}
	while (*s) {
		char *end;
		long a, b;
		if (!isdigit((unsigned char)*s))
			goto bad;
		a = b = strtol(s, &end, 10);
		if (*end == '-') {
			s = end + 1;
			if (!isdigit((unsigned char)*s))
				goto bad;
			b = strtol(s, &end, 10);
		}
		if (b < a || b >= (long)m->size)
			goto bad;
		for (long i = a; i <= b; i++)
			setbit(m, i);
		if (*end == ',')
			end++;
		else if (*end)
			goto bad;
		s = end;
	}
	return m;
bad:
	numa_bitmask_free(m);
	return NULL;
}

int numa_sched_getaffinity(pid_t pid, struct bitmask *m)
{
	cpu_set_t set;
	if (sched_getaffinity(pid, sizeof(set), &set))
		return -1;
	memset(m->maskp, 0, (m->size + 7) / 8);
	for (unsigned int i = 0; i < m->size; i++)
		if (CPU_ISSET(i, &set))
			setbit(m, i);
	return 0;
}

int numa_sched_setaffinity(pid_t pid, struct bitmask *m)
{
	cpu_set_t set;
	CPU_ZERO(&set);
	for (unsigned int i = 0; i < m->size; i++)
		if (numa_bitmask_isbitset(m, i))
			CPU_SET(i, &set);
	return sched_setaffinity(pid, sizeof(set), &set);
}

int numa_node_of_cpu(int cpu)
{
	if (cpu < 0 || cpu >= qnx_num_cpus()) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

int numa_run_on_node(int node)
{
	return node == 0 ? 0 : (errno = EINVAL, -1);
}

void *numa_alloc_onnode(size_t size, int node)
{
	(void)node;
	return malloc(size);
}

void numa_free(void *p, size_t size)
{
	(void)size;
	free(p);
}
