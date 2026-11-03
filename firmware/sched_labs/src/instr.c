#include <errno.h>
#include <zephyr/drivers/gpio.h>

#include "instr.h"

#define LAB_PIN_SPEC(node) GPIO_DT_SPEC_GET(DT_NODELABEL(node), gpios)

static const struct gpio_dt_spec pins[LAB_PIN_COUNT] = {
	[LAB_PIN_T1] = LAB_PIN_SPEC(lab_t1),
	[LAB_PIN_T2] = LAB_PIN_SPEC(lab_t2),
	[LAB_PIN_T3] = LAB_PIN_SPEC(lab_t3),
	[LAB_PIN_AUX] = LAB_PIN_SPEC(lab_aux),
};

int instr_init(void)
{
	for (size_t i = 0; i < LAB_PIN_COUNT; i++) {
		int rc;

		if (!gpio_is_ready_dt(&pins[i])) {
			return -ENODEV;
		}
		rc = gpio_pin_configure_dt(&pins[i], GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			return rc;
		}
	}
	return 0;
}

void instr_set(enum lab_pin pin, int level)
{
	/* Instrumentation: checking the result would lengthen every edge the
	 * analyzer measures, and there is nothing to do about a failed edge. */
	(void)gpio_pin_set_dt(&pins[pin], level);
}
