// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * msglat - QNX message-passing latency, reported like cyclictest
 *
 * Linux has no equivalent of the QNX microkernel IPC, so the rt-tests suite has
 * nothing that measures it. msglat times the three primitives everything else on
 * QNX is built from, between a client thread and a server thread in one process:
 *
 *   msg    MsgSend() -> MsgReceive() -> MsgReply() round trip
 *   pulse  MsgSendPulse() -> MsgReceive() delivery (no reply, never blocks the sender)
 *   event  a timer fires a SIGEV_PULSE at the server's channel (timer -> pulse delivery)
 *
 * For every cycle it records the one-way latency (client timestamp taken just before
 * the send, server timestamp taken right after MsgReceive returns) and, for msg mode,
 * the round trip as seen by the client. Both are printed in microseconds.
 *
 * The server runs at priority P+1, the client at P. Both threads can be pinned with -a.
 * With --pi the server gets a lower priority than the client and a third thread at a
 * medium priority spins: the server must inherit the client's priority to reply in
 * time, which is what the QNX kernel does for MsgSend (no _NTO_CHF_FIXED_PRIORITY).
 * --no-pi is the same set-up on a channel created with _NTO_CHF_FIXED_PRIORITY, so the
 * server keeps its low priority and the spinner starves it: the contrast. Use -a0.
 */
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/iomsg.h>
#include <sys/neutrino.h>
#include <sys/siginfo.h>

#define NSEC_PER_SEC 1000000000LL
#define MSG_CODE (_IO_MAX + 1)
#define PULSE_CODE (_PULSE_CODE_MINAVAIL + 1)
#define HIST_MAX_DEFAULT 400

enum mode { MODE_MSG, MODE_PULSE, MODE_EVENT };

struct stats {
	int64_t min, max, sum;
	uint64_t cycles;
	int64_t act;
	uint64_t *hist;		/* one bucket per microsecond, last = overflow */
};

struct msg {
	uint16_t type;
	uint16_t pad;
	int64_t sent_ns;
};

static enum mode mode = MODE_MSG;
static int prio = 80;
static long interval_us = 1000;
static long loops;		/* 0 = until interrupted */
static int cpu = -1;
static int hist_max;
static int quiet;
static int pi_test;
static int chid;
static volatile int stop;
static pthread_barrier_t ready;
static volatile int64_t event_due_ns;	/* event mode: when the timer was armed to fire */

static struct stats one_way;	/* seen by the server */
static struct stats round_trip;	/* seen by the client (msg mode) */

static int64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
}

static void stats_init(struct stats *s)
{
	s->min = INT64_MAX;
	s->max = 0;
	if (hist_max)
		s->hist = calloc(hist_max + 1, sizeof(uint64_t));
}

static void stats_add(struct stats *s, int64_t ns)
{
	if (ns < 0)
		ns = 0;
	if (ns < s->min)
		s->min = ns;
	if (ns > s->max)
		s->max = ns;
	s->sum += ns;
	s->act = ns;
	s->cycles++;
	if (s->hist) {
		int64_t us = ns / 1000;

		s->hist[us > hist_max ? hist_max : us]++;
	}
}

static void pin_and_prioritise(int p)
{
	struct sched_param sp = { .sched_priority = p };
	int err;

	if (cpu >= 0) {
		cpu_set_t set;

		CPU_ZERO(&set);
		CPU_SET(cpu, &set);
		if (sched_setaffinity(0, sizeof(set), &set))
			fprintf(stderr, "msglat: cannot pin to cpu %d: %s\n", cpu, strerror(errno));
	}
	err = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
	if (err)
		fprintf(stderr, "msglat: cannot set SCHED_FIFO prio %d: %s\n", p, strerror(err));
}

static void line(FILE *f, const char *tag, int id, const struct stats *s, int pr)
{
	fprintf(f, "%s%d (%5d) P:%2d I:%ld C:%7" PRIu64 " Min:%7" PRId64 " Act:%7" PRId64
		" Avg:%7" PRId64 " Max:%8" PRId64 "\n", tag, id, (int)gettid(), pr, interval_us,
		s->cycles, s->cycles ? s->min / 1000 : 0, s->act / 1000,
		s->cycles ? s->sum / (int64_t)s->cycles / 1000 : 0, s->max / 1000);
}

static void *server(void *arg)
{
	union {
		struct msg m;
		struct _pulse p;
	} buf;
	int rcvid;

	(void)arg;
	pin_and_prioritise(pi_test ? prio - 20 : prio + 1);
	pthread_barrier_wait(&ready);

	while (!stop) {
		rcvid = MsgReceive(chid, &buf, sizeof(buf), NULL);
		int64_t t = now_ns();

		if (rcvid < 0) {
			if (errno == EINTR)
				continue;
			perror("MsgReceive");
			break;
		}
		if (rcvid == 0) {
			if (buf.p.code != PULSE_CODE)
				continue;
			if (mode == MODE_EVENT)
				stats_add(&one_way, t - event_due_ns);
			else if (buf.p.value.sival_ptr)	/* NULL = the client's stop pulse */
				stats_add(&one_way, t - (int64_t)(intptr_t)buf.p.value.sival_ptr);
			continue;
		}
		stats_add(&one_way, t - buf.m.sent_ns);
		MsgReply(rcvid, EOK, NULL, 0);
	}
	return NULL;
}

static void *spinner(void *arg)
{
	(void)arg;
	pin_and_prioritise(prio - 10);
	/* bursts, so that an unbounded inversion shows as a delay and not as a hang */
	while (!stop) {
		int64_t until = now_ns() + 20000000LL;

		while (!stop && now_ns() < until)
			;
		usleep(2000);
	}
	return NULL;
}

static void print_hist(const char *name, const struct stats *s)
{
	int i;

	if (!s->hist)
		return;
	printf("# Histogram %s (us: count)\n", name);
	for (i = 0; i <= hist_max; i++)
		if (s->hist[i])
			printf("%s%d: %" PRIu64 "\n", i == hist_max ? ">=" : "", i, s->hist[i]);
}

static void on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

static void usage(void)
{
	printf("msglat V 0.1 - QNX message passing latency (rt-tests style)\n"
	       "Usage: msglat [options]\n"
	       "  -m, --mode=msg|pulse|event  what to measure (default msg)\n"
	       "  -p, --prio=N        client priority, server runs at N+1 (default 80)\n"
	       "  -i, --interval=US   cycle time in microseconds (default 1000)\n"
	       "  -l, --loops=N       stop after N cycles (default: until Ctrl-C)\n"
	       "  -a, --affinity=CPU  pin both threads to CPU\n"
	       "  -h, --histogram=US  print a histogram with this many 1 us buckets\n"
	       "  -q, --quiet         print only the summary\n"
	       "      --pi            server below the client plus a spinning medium-priority\n"
	       "                      thread: shows priority inheritance on MsgSend\n"
	       "      --no-pi         same, with _NTO_CHF_FIXED_PRIORITY (no inheritance); use -a0\n"
	       "  -?, --help\n");
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "mode", required_argument, NULL, 'm' },
		{ "prio", required_argument, NULL, 'p' },
		{ "interval", required_argument, NULL, 'i' },
		{ "loops", required_argument, NULL, 'l' },
		{ "affinity", required_argument, NULL, 'a' },
		{ "histogram", required_argument, NULL, 'h' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "pi", no_argument, NULL, 'P' },
		{ "no-pi", no_argument, NULL, 'N' },
		{ "help", no_argument, NULL, '?' },
		{ NULL, 0, NULL, 0 }
	};
	pthread_t srv, spin;
	struct timespec next;
	struct sigevent ev;
	timer_t timer = 0;
	int c, coid, spinning = 0;
	unsigned chan_flags = 0;
	int64_t period;
	struct msg m;

	while ((c = getopt_long(argc, argv, "m:p:i:l:a:h:q?", opts, NULL)) != -1) {
		switch (c) {
		case 'm':
			if (!strcmp(optarg, "msg"))
				mode = MODE_MSG;
			else if (!strcmp(optarg, "pulse"))
				mode = MODE_PULSE;
			else if (!strcmp(optarg, "event"))
				mode = MODE_EVENT;
			else {
				fprintf(stderr, "msglat: unknown mode '%s'\n", optarg);
				return 2;
			}
			break;
		case 'p': prio = atoi(optarg); break;
		case 'i': interval_us = atol(optarg); break;
		case 'l': loops = atol(optarg); break;
		case 'a': cpu = atoi(optarg); break;
		case 'h': hist_max = atoi(optarg) > 0 ? atoi(optarg) : HIST_MAX_DEFAULT; break;
		case 'q': quiet = 1; break;
		case 'P': pi_test = 1; break;
		case 'N': pi_test = 1; chan_flags = _NTO_CHF_FIXED_PRIORITY; break;
		default: usage(); return c == '?' ? 0 : 2;
		}
	}
	if (interval_us < 50 || prio < 2 || prio > 253) {
		fprintf(stderr, "msglat: interval must be >= 50 us and prio 2..253\n");
		return 2;
	}
	if (pi_test && mode != MODE_MSG) {
		fprintf(stderr, "msglat: --pi/--no-pi need --mode=msg\n");
		return 2;
	}

	stats_init(&one_way);
	stats_init(&round_trip);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	chid = ChannelCreate(chan_flags);
	if (chid == -1) {
		perror("ChannelCreate");
		return 1;
	}
	coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
	if (coid == -1) {
		perror("ConnectAttach");
		return 1;
	}

	pthread_barrier_init(&ready, NULL, 2);
	if (pthread_create(&srv, NULL, server, NULL)) {
		perror("pthread_create");
		return 1;
	}
	if (pi_test) {
		if (!pthread_create(&spin, NULL, spinner, NULL))
			spinning = 1;
	}
	pin_and_prioritise(prio);
	pthread_barrier_wait(&ready);

	period = interval_us * 1000LL;
	if (mode == MODE_EVENT) {
		SIGEV_PULSE_PTR_INIT(&ev, coid, prio + 1, PULSE_CODE, NULL);
		if (timer_create(CLOCK_MONOTONIC, &ev, &timer)) {
			perror("timer_create");
			return 1;
		}
	}

	printf("# QNX clock period: %ld us\n", qnx_clock_period_ns() / 1000);
	printf("# msglat V 0.1: mode=%s, client prio %d, server prio %d, interval %ld us\n",
	       mode == MODE_MSG ? "msg" : mode == MODE_PULSE ? "pulse" : "event", prio,
	       pi_test ? prio - 20 : prio + 1, interval_us);
	if (mode == MODE_EVENT)
		printf("# event mode: a one-shot timer fires 100 us after each cycle starts; latency = pulse delivery - due time\n");

	clock_gettime(CLOCK_MONOTONIC, &next);
	while (!stop && (!loops || (long)one_way.cycles < loops)) {
		int64_t t0, t1;

		next.tv_nsec += period;
		while (next.tv_nsec >= NSEC_PER_SEC) {
			next.tv_nsec -= NSEC_PER_SEC;
			next.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

		if (mode == MODE_EVENT) {
			struct itimerspec its = { { 0, 0 }, { 0, 0 } };
			int64_t due = now_ns() + 100 * 1000;

			event_due_ns = due;
			its.it_value.tv_sec = due / NSEC_PER_SEC;
			its.it_value.tv_nsec = due % NSEC_PER_SEC;
			if (timer_settime(timer, TIMER_ABSTIME, &its, NULL)) {
				perror("timer_settime");
				break;
			}
		} else if (mode == MODE_PULSE) {
			if (MsgSendPulsePtr(coid, prio, PULSE_CODE, (void *)(intptr_t)now_ns()) == -1) {
				perror("MsgSendPulsePtr");
				break;
			}
		} else {
			m.type = MSG_CODE;
			m.sent_ns = now_ns();
			t0 = m.sent_ns;
			if (MsgSend(coid, &m, sizeof(m), NULL, 0) == -1) {
				perror("MsgSend");
				break;
			}
			t1 = now_ns();
			stats_add(&round_trip, t1 - t0);
		}
		if (!quiet && (round_trip.cycles % 20 == 0 || one_way.cycles % 20 == 0)) {
			if (mode == MODE_MSG)
				line(stdout, "RT:", 0, &round_trip, prio);
			line(stdout, "OW:", 0, &one_way, prio);
			fflush(stdout);
		}
	}

	stop = 1;
	MsgSendPulsePtr(coid, prio, PULSE_CODE, NULL);
	pthread_join(srv, NULL);
	if (spinning)
		pthread_join(spin, NULL);

	printf("# Summary\n");
	if (mode == MODE_MSG)
		line(stdout, "RT:", 0, &round_trip, prio);
	line(stdout, "OW:", 0, &one_way, prio);
	if (mode == MODE_MSG)
		print_hist("round trip", &round_trip);
	print_hist("one way", &one_way);
	return 0;
}
