/* A calibrated CPU burner: the "computation" of a synthetic task. */
#ifndef SCHED_LABS_BURN_H_
#define SCHED_LABS_BURN_H_

#include <stdint.h>

/* Measure how many loop iterations fit in one millisecond. Call once at boot,
 * before any task runs. Returns 0, or -EIO if the cycle counter did not move. */
int burn_calibrate(void);

/* Consume `ms` milliseconds of CPU time. The loop is counted in iterations, not
 * in wall-clock time, so a preemption stretches its wall time instead of
 * shortening the work: that is what makes it a stand-in for C_i. */
void burn_ms(uint32_t ms);

/* Wall time, in microseconds, that burn_ms(ms) takes when nothing else runs. */
uint32_t burn_measure_us(uint32_t ms);

#endif /* SCHED_LABS_BURN_H_ */
