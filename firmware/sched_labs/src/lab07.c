/*
 * Lab 07 Task B/C — priority inversion, reproduced and closed.
 *
 * Three tasks, released once per episode (EPISODE_MS), by timers whose offsets set the
 * classic sequence:
 *
 *   t = 0    slow    (low priority)    takes the lock and starts its critical section
 *   t = 1    control (high priority)   asks for the lock -> blocks
 *   t = 2    medium  (medium priority) does not use the lock; becomes ready
 *
 * Without priority inheritance (CONFIG_SCHED_LAB_INVERSION: a k_sem of 1 as the lock)
 * medium preempts slow, so slow cannot finish its critical section and control waits for
 * slow AND medium. With a k_mutex (CONFIG_SCHED_LAB_PI) slow inherits control's priority
 * while it holds the lock, medium cannot preempt it, and control waits only for the rest
 * of the critical section. CONFIG_SCHED_LAB_NOLOCK is the baseline: nobody takes the lock.
 *
 * Instrumentation: D0 control busy, D1 medium busy, D2 slow holding the lock, D3 control
 * blocked waiting for the lock. The numbers predicted for this exact set are produced by
 * tools/scripts/rta.py (presets lab07-nolock / lab07-inversion / lab07-pi).
 */
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "burn.h"
#include "instr.h"
#include "scenario.h"

#define CONTROL_C_MS      1    /* control job, entirely inside the lock when it uses one */
#define MEDIUM_C_MS       10   /* medium job, never takes the lock */
#define SLOW_CS_MS        4    /* slow job = its critical section */
#define EPISODE_MS        100
#define CONTROL_OFFSET_MS 1    /* control is released this long after slow */
#define MEDIUM_OFFSET_MS  2
#define CONTROL_DEADLINE_US 5000 /* the e-stop deadline */

#define CONTROL_PRIO 2
#define MEDIUM_PRIO  4
#define SLOW_PRIO    6
#define STACK_SIZE   1024
#define DRAIN_MS     200 /* let the last episode finish before the summary */

enum task_id { TASK_CONTROL, TASK_MEDIUM, TASK_SLOW, TASK_COUNT };

#if defined(CONFIG_SCHED_LAB_INVERSION)
/* A bare binary semaphore used as a lock: nothing raises the owner's priority. */
static K_SEM_DEFINE(lock, 1, 1);

static int lock_take(void)
{
	return k_sem_take(&lock, K_FOREVER);
}

static int lock_give(void)
{
	k_sem_give(&lock);
	return 0;
}
#elif defined(CONFIG_SCHED_LAB_PI)
/* Zephyr's k_mutex inherits the priority of its highest-priority waiter. */
static K_MUTEX_DEFINE(lock);

static int lock_take(void)
{
	return k_mutex_lock(&lock, K_FOREVER);
}

static int lock_give(void)
{
	return k_mutex_unlock(&lock);
}
#else
/* Baseline: no shared lock at all. */
static int lock_take(void)
{
	return 0;
}

static int lock_give(void)
{
	return 0;
}
#endif

static struct k_timer timers[TASK_COUNT];
/* Statically defined: the task threads start at boot, before main runs, and block on
 * these at once, so they must be valid objects from the first instruction. */
static K_SEM_DEFINE(release_sem_control, 0, 1);
static K_SEM_DEFINE(release_sem_medium, 0, 1);
static K_SEM_DEFINE(release_sem_slow, 0, 1);
static struct k_sem *const release_sem[TASK_COUNT] = {
	[TASK_CONTROL] = &release_sem_control,
	[TASK_MEDIUM] = &release_sem_medium,
	[TASK_SLOW] = &release_sem_slow,
};
static atomic_t release_cyc[TASK_COUNT]; /* cycle stamp of the latest release */

static atomic_t control_jobs;
static atomic_t control_misses;      /* response above CONTROL_DEADLINE_US */
static atomic_t control_worst_us;    /* worst release -> completion: the R of the lab */
static atomic_t control_block_us;    /* worst time spent waiting for the lock */
static atomic_t medium_jobs;
static atomic_t slow_jobs;
static atomic_t slow_cs_worst_us;    /* worst wall time slow held the lock: B_i */
static atomic_t lock_errors;         /* failed lock calls */

const char *scenario_name(void)
{
#if defined(CONFIG_SCHED_LAB_INVERSION)
	return "Lab 07 Task B — bare semaphore as lock (priority inversion)";
#elif defined(CONFIG_SCHED_LAB_PI)
	return "Lab 07 Task C — k_mutex with priority inheritance";
#else
	return "Lab 07 baseline — no lock";
#endif
}

static void note_max(atomic_t *peak, uint32_t cycles)
{
	atomic_val_t us = k_cyc_to_us_floor32(cycles);

	if (us > atomic_get(peak)) {
		atomic_set(peak, us); /* single writer: the task that owns the statistic */
	}
}

/* Timer expiry (ISR): stamp the release and wake the task. */
static void release_isr(struct k_timer *timer)
{
	const uint32_t i = POINTER_TO_UINT(k_timer_user_data_get(timer));

	atomic_set(&release_cyc[i], (atomic_val_t)k_cycle_get_32());
	k_sem_give(release_sem[i]);
}

static void control_thread(void *p1, void *p2, void *p3)
{
	while (1) {
		if (k_sem_take(release_sem[TASK_CONTROL], K_FOREVER) != 0) {
			continue;
		}
		const uint32_t released = (uint32_t)atomic_get(&release_cyc[TASK_CONTROL]);

		instr_set(LAB_PIN_T1, 1);
		instr_set(LAB_PIN_AUX, 1); /* blocked until it holds the lock */
		const uint32_t asked = k_cycle_get_32();

		if (lock_take() != 0) {
			atomic_inc(&lock_errors);
			instr_set(LAB_PIN_AUX, 0);
			instr_set(LAB_PIN_T1, 0);
			continue;
		}
		const uint32_t got = k_cycle_get_32();

		instr_set(LAB_PIN_AUX, 0);
		burn_ms(CONTROL_C_MS);
		if (lock_give() != 0) {
			atomic_inc(&lock_errors);
		}
		const uint32_t done = k_cycle_get_32();

		instr_set(LAB_PIN_T1, 0);
		note_max(&control_block_us, got - asked);
		note_max(&control_worst_us, done - released);
		if (k_cyc_to_us_floor32(done - released) > CONTROL_DEADLINE_US) {
			atomic_inc(&control_misses);
		}
		atomic_inc(&control_jobs);
	}
}

static void medium_thread(void *p1, void *p2, void *p3)
{
	while (1) {
		if (k_sem_take(release_sem[TASK_MEDIUM], K_FOREVER) != 0) {
			continue;
		}
		instr_set(LAB_PIN_T2, 1);
		burn_ms(MEDIUM_C_MS);
		instr_set(LAB_PIN_T2, 0);
		atomic_inc(&medium_jobs);
	}
}

static void slow_thread(void *p1, void *p2, void *p3)
{
	while (1) {
		if (k_sem_take(release_sem[TASK_SLOW], K_FOREVER) != 0) {
			continue;
		}
		if (lock_take() != 0) {
			atomic_inc(&lock_errors);
			continue;
		}
		const uint32_t start = k_cycle_get_32();

		instr_set(LAB_PIN_T3, 1);
		burn_ms(SLOW_CS_MS);
		const uint32_t end = k_cycle_get_32();

		instr_set(LAB_PIN_T3, 0);
		if (lock_give() != 0) {
			atomic_inc(&lock_errors);
		}
		note_max(&slow_cs_worst_us, end - start);
		atomic_inc(&slow_jobs);
	}
}

K_THREAD_DEFINE(control_tid, STACK_SIZE, control_thread, NULL, NULL, NULL, CONTROL_PRIO, 0, 0);
K_THREAD_DEFINE(medium_tid, STACK_SIZE, medium_thread, NULL, NULL, NULL, MEDIUM_PRIO, 0, 0);
K_THREAD_DEFINE(slow_tid, STACK_SIZE, slow_thread, NULL, NULL, NULL, SLOW_PRIO, 0, 0);

static void print_summary(void)
{
	const unsigned int worst = (unsigned int)atomic_get(&control_worst_us);
	const unsigned int block = (unsigned int)atomic_get(&control_block_us);
	const unsigned int cs = (unsigned int)atomic_get(&slow_cs_worst_us);

	printk("\ncontrol jobs     : %ld\n", (long)atomic_get(&control_jobs));
	printk("medium jobs      : %ld\n", (long)atomic_get(&medium_jobs));
	printk("slow jobs        : %ld\n", (long)atomic_get(&slow_jobs));
	printk("max R control    : %u.%03u ms   (deadline %u ms, misses: %ld)\n",
	       worst / USEC_PER_MSEC, worst % USEC_PER_MSEC,
	       CONTROL_DEADLINE_US / USEC_PER_MSEC, (long)atomic_get(&control_misses));
	printk("max lock wait    : %u.%03u ms   (control, request -> acquired)\n",
	       block / USEC_PER_MSEC, block % USEC_PER_MSEC);
	printk("max slow CS wall : %u.%03u ms   (B_i, lock held; includes any preemption)\n",
	       cs / USEC_PER_MSEC, cs % USEC_PER_MSEC);
	printk("lock errors      : %ld\n", (long)atomic_get(&lock_errors));
}

int scenario_run(void)
{
	static const uint32_t offset_ms[TASK_COUNT] = {
		[TASK_CONTROL] = CONTROL_OFFSET_MS,
		[TASK_MEDIUM] = MEDIUM_OFFSET_MS,
		[TASK_SLOW] = 0,
	};
	unsigned int key;

	for (size_t i = 0; i < TASK_COUNT; i++) {
		k_timer_init(&timers[i], release_isr, NULL);
		k_timer_user_data_set(&timers[i], UINT_TO_POINTER(i));
	}
	instr_set(LAB_PIN_AUX, 0);
	printk("episode %d ms: slow at +0, control at +%d, medium at +%d (ms)\n", EPISODE_MS,
	       CONTROL_OFFSET_MS, MEDIUM_OFFSET_MS);

	/* One-off, from main, a few microseconds: all three timers inside the same tick. */
	key = irq_lock();
	for (size_t i = 0; i < TASK_COUNT; i++) {
		k_timer_start(&timers[i], K_MSEC(offset_ms[i]), K_MSEC(EPISODE_MS));
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
