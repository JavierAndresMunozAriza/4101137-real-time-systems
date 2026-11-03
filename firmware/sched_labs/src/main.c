/*
 * Scheduling labs (weeks 6 and 7): synthetic task sets on the ESP32-S3, measured
 * with the logic analyzer. The scenario is chosen at build time (see Kconfig).
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "burn.h"
#include "instr.h"
#include "scenario.h"

#define BURN_CHECK_MS 10 /* calibration self-check: burn this long, report wall time */

int main(void)
{
	int rc = instr_init();

	if (rc != 0) {
		printk("instrumentation pins failed (%d)\n", rc);
		return rc;
	}
	rc = burn_calibrate();
	if (rc != 0) {
		printk("burn calibration failed (%d)\n", rc);
		return rc;
	}

	printk("sched_labs: %s\n", scenario_name());
	printk("burn_ms(%d) took %u us with nothing else running\n", BURN_CHECK_MS,
	       burn_measure_us(BURN_CHECK_MS));

	rc = scenario_run();
	printk("scenario finished (%d)\n", rc);
	return rc;
}
