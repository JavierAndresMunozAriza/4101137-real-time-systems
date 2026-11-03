#include <errno.h>
#include <zephyr/kernel.h>

#include "burn.h"

#define CAL_ITERS 2000000U /* ~tens of ms: long enough to average the tick ISR out */

static uint32_t iters_per_ms;

/* Deliberate busy-wait: this loop IS the synthetic computation. It runs inside a
 * periodic task on purpose, because the schedulability labs need a task that
 * consumes a known amount of CPU. `volatile` keeps the compiler from deleting it. */
static void spin(uint32_t iters)
{
	for (volatile uint32_t i = 0; i < iters; i++) {
	}
}

int burn_calibrate(void)
{
	uint32_t start = k_cycle_get_32();
	uint32_t cycles;
	uint64_t cycles_per_ms;

	spin(CAL_ITERS);
	cycles = k_cycle_get_32() - start;
	if (cycles == 0U) {
		return -EIO;
	}
	cycles_per_ms = sys_clock_hw_cycles_per_sec() / MSEC_PER_SEC;
	iters_per_ms = (uint32_t)(((uint64_t)CAL_ITERS * cycles_per_ms) / cycles);
	return iters_per_ms > 0U ? 0 : -EIO;
}

void burn_ms(uint32_t ms)
{
	spin(ms * iters_per_ms);
}

uint32_t burn_measure_us(uint32_t ms)
{
	uint32_t start = k_cycle_get_32();

	burn_ms(ms);
	return k_cyc_to_us_floor32(k_cycle_get_32() - start);
}
