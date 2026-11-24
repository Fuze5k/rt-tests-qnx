// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * qnx-compat.c - Linux affinity and thread-id calls on top of QNX's run mask.
 *
 * The run mask is set with ThreadCtl()/ThreadCtlExt(): a struct _thread_runmask
 * followed by `size` words of run mask and, for the _INHERIT command, `size`
 * words of inherit mask (what threads created later start with).
 */
#include <errno.h>
#include <unistd.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <qnx-compat.h>

#define WORD_BITS (8 * (int)sizeof(unsigned))
#define MAX_WORDS ((CPU_SETSIZE + WORD_BITS - 1) / WORD_BITS)

int qnx_num_cpus(void)
{
	return _syspage_ptr->num_cpu;
}

int qnx_cpu_count(const cpu_set_t *set, size_t size)
{
	int n = 0;
	for (size_t i = 0; i < size / sizeof(unsigned long); i++)
		n += __builtin_popcountl(set->__bits[i]);
	return n;
}

pid_t gettid(void)
{
	return (pid_t)pthread_self();
}

int sched_getcpu(void)
{
	return -1;
}

/* One ThreadCtlExt() call. buf layout: [size][runmask * size][inherit * size]. */
static int runmask_ctl(int tid, int cmd, const cpu_set_t *set, int inherit, unsigned *old_out)
{
	int ncpu = qnx_num_cpus();
	int words = RMSK_SIZE(ncpu);
	unsigned buf[1 + 2 * MAX_WORDS];
	int rc;

	if (words > (int)MAX_WORDS)
		return EINVAL;
	memset(buf, 0, sizeof(buf));
	buf[0] = words;
	if (set) {
		for (int cpu = 0; cpu < ncpu; cpu++)
			if (CPU_ISSET(cpu, set)) {
				RMSK_SET(cpu, &buf[1]);
				if (inherit)
					RMSK_SET(cpu, &buf[1 + words]);
			}
		/* a set that names no CPU the system has cannot be applied */
		for (int i = 0; i < words; i++)
			if (buf[1 + i])
				goto have_cpu;
		return EINVAL;
	}
have_cpu:
	rc = ThreadCtlExt(0, tid, cmd, buf);
	if (rc == -1)
		return errno;
	if (old_out)
		memcpy(old_out, &buf[1], words * sizeof(unsigned));
	return 0;
}

static int set_affinity(int tid, int process_wide, size_t size, const cpu_set_t *mask)
{
	(void)size;
	return runmask_ctl(tid, process_wide ? _NTO_TCTL_RUNMASK_GET_AND_SET_INHERIT
					     : _NTO_TCTL_RUNMASK_GET_AND_SET,
			   mask, process_wide, NULL);
}

static int get_affinity(int tid, size_t size, cpu_set_t *mask)
{
	unsigned cur[MAX_WORDS];
	int ncpu = qnx_num_cpus(), rc;

	/* a run mask of zero means "do not change it, just report it" */
	rc = runmask_ctl(tid, _NTO_TCTL_RUNMASK_GET_AND_SET, NULL, 0, cur);
	if (rc)
		return rc;
	memset(mask, 0, size < sizeof(*mask) ? size : sizeof(*mask));
	for (int cpu = 0; cpu < ncpu; cpu++)
		if (RMSK_ISSET(cpu, cur))
			CPU_SET(cpu, mask);
	return 0;
}

static int tid_of(pid_t pid)
{
	return pid == 0 || pid == getpid() ? pthread_self() : pid;
}

int sched_setaffinity(pid_t pid, size_t size, const cpu_set_t *mask)
{
	int rc = set_affinity(tid_of(pid), pid != 0 && pid == getpid(), size, mask);
	if (rc) {
		errno = rc;
		return -1;
	}
	return 0;
}

int sched_getaffinity(pid_t pid, size_t size, cpu_set_t *mask)
{
	int rc = get_affinity(tid_of(pid), size, mask);
	if (rc) {
		errno = rc;
		return -1;
	}
	return 0;
}

int pthread_setaffinity_np(pthread_t thread, size_t size, const cpu_set_t *mask)
{
	return set_affinity((int)thread, 0, size, mask);
}

int pthread_getaffinity_np(pthread_t thread, size_t size, cpu_set_t *mask)
{
	return get_affinity((int)thread, size, mask);
}
