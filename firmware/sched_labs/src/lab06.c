/*
 * Lab 06 Task A — a task set that rate-monotonic scheduling misses and EDF meets.
 *
 * Three periodic tasks with implicit deadlines (D = T), released together at t = 0
 * (the critical instant). A task's "computation" is a calibrated busy-wait
 * (burn_ms), so a preemption stretches its wall time exactly as it would a real
 * C_i.
 *
 *   RM  (CONFIG_SCHED_LAB_RM):  fixed priorities, shorter period = higher priority.
 *   EDF (CONFIG_SCHED_LAB_EDF): one priority for all; each job's absolute deadline is
 *                               set with k_thread_deadline_set and the kernel runs
 *                               the earliest one (CONFIG_SCHED_DEADLINE).
 *
 * The set is a synthetic stand-in for the book's Fig. 4.13 (those numbers are not in
 * the repository): U = 0.967, one time unit = 10 ms. tools/scripts/sched_sim.py
 * predicts the outcome of both policies for exactly this table.
 *
 * Instrumentation: D0..D2 are high while a job of tau1..tau3 is running or preempted
 * (first dispatch -> completion); D3 latches high at the first missed deadline.
 */
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "burn.h"
#include "instr.h"
#include "scenario.h"

#define TASK_COUNT 3

#define TAU1_C_MS 10
#define TAU1_T_MS 50
#define TAU2_C_MS 30
#define TAU2_T_MS 60
#define TAU3_C_MS 40
#define TAU3_T_MS 150

#define BASE_PRIO   3    /* main runs at 10, below every task */
#define STACK_SIZE  1024
#define QUEUE_DEPTH 4    /* releases that may queue behind a late job */
#define QUEUE_ALIGN 4
#define DRAIN_MS    1000 /* let late jobs finish before the summary */
#define CYCLES_PER_MS (sys_clock_hw_cycles_per_sec() / MSEC_PER_SEC)

struct task_def {
	const char *name;
	uint32_t c_ms;
	uint32_t t_ms; /* period = relative deadline */
	enum lab_pin pin;
};

static const struct task_def defs[TASK_COUNT] = {
	{ "tau1", TAU1_C_MS, TAU1_T_MS, LAB_PIN_T1 },
	{ "tau2", TAU2_C_MS, TAU2_T_MS, LAB_PIN_T2 },
	{ "tau3", TAU3_C_MS, TAU3_T_MS, LAB_PIN_T3 },
};

struct task_stats {
	atomic_t pending;  /* released and not yet completed */
	atomic_t jobs;     /* completed */
	atomic_t misses;   /* completed with response time above the deadline */
	atomic_t dropped;  /* releases lost because the release queue was full */
	atomic_t worst_us; /* worst release -> completion */
};

static struct task_stats stats[TASK_COUNT];
static struct k_timer timers[TASK_COUNT];

K_MSGQ_DEFINE_STATIC(release_q_0, sizeof(uint32_t), QUEUE_DEPTH, QUEUE_ALIGN);
K_MSGQ_DEFINE_STATIC(release_q_1, sizeof(uint32_t), QUEUE_DEPTH, QUEUE_ALIGN);
K_MSGQ_DEFINE_STATIC(release_q_2, sizeof(uint32_t), QUEUE_DEPTH, QUEUE_ALIGN);
static struct k_msgq *const release_q[TASK_COUNT] = {
	&release_q_0, &release_q_1, &release_q_2,
};

#ifdef CONFIG_SCHED_LAB_EDF
#define TASK_PRIO(i) BASE_PRIO       /* one priority: the deadline decides */
#else
#define TASK_PRIO(i) (BASE_PRIO + (i)) /* shorter period -> lower number */
#endif

const char *scenario_name(void)
{
#ifdef CONFIG_SCHED_LAB_EDF
	return "Lab 06 Task A — EDF (SCHED_DEADLINE)";
#else
	return "Lab 06 Task A — rate-monotonic";
#endif
}

/* The timer expires in ISR context: stamp the release, queue it, return. */
static void release_isr(struct k_timer *timer)
{
	const uint32_t i = POINTER_TO_UINT(k_timer_user_data_get(timer));
	uint32_t stamp = k_cycle_get_32();

	/* D = T: if the previous job is still unfinished, its deadline just passed. */
	if (atomic_get(&stats[i].pending) > 0) {
		instr_set(LAB_PIN_AUX, 1);
	}
	atomic_inc(&stats[i].pending);
	if (k_msgq_put(release_q[i], &stamp, K_NO_WAIT) != 0) {
		atomic_inc(&stats[i].dropped);
		atomic_dec(&stats[i].pending);
	}
}

static void finish_job(uint32_t i, uint32_t released, uint32_t period_cyc)
{
	uint32_t now = k_cycle_get_32();
	uint32_t response = now - released;
	atomic_val_t us = k_cyc_to_us_floor32(response);

	if (response > period_cyc) {
		atomic_inc(&stats[i].misses);
	}
	if (us > atomic_get(&stats[i].worst_us)) {
		atomic_set(&stats[i].worst_us, us); /* single writer: this task */
	}
	atomic_inc(&stats[i].jobs);

#ifdef CONFIG_SCHED_LAB_EDF
	/* The next job's absolute deadline is its release (released + T) plus T. Set it
	 * now, so it is already in place when that release makes this thread ready. */
	int32_t next = (int32_t)(released + 2U * period_cyc - now);

	k_thread_deadline_set(k_current_get(), MAX(next, 0));
#endif
	atomic_dec(&stats[i].pending);
}

static void periodic_thread(void *p1, void *p2, void *p3)
{
	const uint32_t i = POINTER_TO_UINT(p1);
	const struct task_def *d = &defs[i];
	const uint32_t period_cyc = d->t_ms * CYCLES_PER_MS;
	uint32_t released;

	while (1) {
		if (k_msgq_get(release_q[i], &released, K_FOREVER) != 0) {
			continue;
		}
		instr_set(d->pin, 1);
		burn_ms(d->c_ms);
		instr_set(d->pin, 0);
		finish_job(i, released, period_cyc);
	}
}

K_THREAD_DEFINE(tau1_tid, STACK_SIZE, periodic_thread, UINT_TO_POINTER(0), NULL, NULL,
		TASK_PRIO(0), 0, 0);
K_THREAD_DEFINE(tau2_tid, STACK_SIZE, periodic_thread, UINT_TO_POINTER(1), NULL, NULL,
		TASK_PRIO(1), 0, 0);
K_THREAD_DEFINE(tau3_tid, STACK_SIZE, periodic_thread, UINT_TO_POINTER(2), NULL, NULL,
		TASK_PRIO(2), 0, 0);

static void print_summary(void)
{
	bool any_miss = false;

	printk("\n%-5s %6s %6s %6s %7s %8s %11s\n", "task", "C[ms]", "T[ms]", "jobs",
	       "misses", "dropped", "worst[ms]");
	for (size_t i = 0; i < TASK_COUNT; i++) {
		unsigned int worst = (unsigned int)atomic_get(&stats[i].worst_us);

		printk("%-5s %6u %6u %6ld %7ld %8ld %7u.%03u\n", defs[i].name, defs[i].c_ms,
		       defs[i].t_ms, (long)atomic_get(&stats[i].jobs),
		       (long)atomic_get(&stats[i].misses), (long)atomic_get(&stats[i].dropped),
		       worst / USEC_PER_MSEC, worst % USEC_PER_MSEC);
		any_miss = any_miss || atomic_get(&stats[i].misses) > 0;
	}
	printk("deadline missed: %s\n", any_miss ? "YES" : "no");
}

int scenario_run(void)
{
#ifdef CONFIG_SCHED_LAB_EDF
	const k_tid_t tids[TASK_COUNT] = { tau1_tid, tau2_tid, tau3_tid };
#endif
	unsigned int key;

	for (size_t i = 0; i < TASK_COUNT; i++) {
		printk("%s: C = %u ms, T = D = %u ms\n", defs[i].name, defs[i].c_ms,
		       defs[i].t_ms);
		k_timer_init(&timers[i], release_isr, NULL);
		k_timer_user_data_set(&timers[i], UINT_TO_POINTER(i));
#ifdef CONFIG_SCHED_LAB_EDF
		/* First job: absolute deadline = now + T (the release is about to happen). */
		k_thread_deadline_set(tids[i], (int)(defs[i].t_ms * CYCLES_PER_MS));
#endif
	}
	instr_set(LAB_PIN_AUX, 0);

	/* All three timers inside one tick: a synchronous release is the critical instant.
	 * One-off, from main, a few microseconds: the lock cannot hurt a periodic task. */
	key = irq_lock();
	for (size_t i = 0; i < TASK_COUNT; i++) {
		k_timer_start(&timers[i], K_NO_WAIT, K_MSEC(defs[i].t_ms));
	}
	irq_unlock(key);

	k_msleep(CONFIG_SCHED_LAB_RUN_S * MSEC_PER_SEC);
	for (size_t i = 0; i < TASK_COUNT; i++) {
		k_timer_stop(&timers[i]);
	}
	k_msleep(DRAIN_MS);
	print_summary();
	return 0;
}
