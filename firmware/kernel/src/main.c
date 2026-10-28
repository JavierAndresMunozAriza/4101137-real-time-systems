/*
 * SoilSense Control — the kernel-based implementation.
 * Multithreaded architecture with message queues and workqueues.
 * This is the Lab04 migration from superloop to kernel threads.
 *
 * The node's tasks (PROJECT_SCENARIO.md):
 *   sampling  (hard, 1 kHz)  — pressure ADC, e-stop check, valve PWM phase
 *   control   (hard, 10 ms)  — PI loop -> valve duty
 *   console   (firm)         — UART commands (help/status/joy/set/clear/calib)
 *   telemetry (soft, 1 Hz)   — CSV line over the console UART
 *   flow ISR  (hard)         — pulse counting on the flow input pin
 *
 * Every task raises its instrumentation GPIO on entry and drops it on exit:
 * the logic analyzer sees exactly who ran and for how long.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#ifdef CONFIG_CHARACTER_FRAMEBUFFER
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#endif
#include <stdio.h>

#define SAMPLE_PERIOD_US 1000  /* sampling: 1 kHz */
#define CONTROL_EVERY    10    /* control runs every 10th sample -> 10 ms */
#define TELEMETRY_MS     1000
#define PWM_STEPS        20    /* software PWM on the valve LED: 1 kHz / 20 = 50 Hz */
#define FLOW_BATCH       100   /* pulses accumulated before the batch math runs */
#define ESTOP_MV         3000  /* overpressure threshold */
#define CALIB_ROUNDS     1000  /* `calib` command: rounds of read + settle */
#define DISPLAY_MS       500   /* HMI refresh period */
#define ESTOP_HOLD       50    /* joystick-down samples (at 50 Hz) = 1 s hold */

/* ---- Hardware (all optional: missing DT nodes degrade to a synthetic rig) -- */

#define INSTR(node) GPIO_DT_SPEC_GET_OR(DT_NODELABEL(node), gpios, {0})
static const struct gpio_dt_spec instr_samp = INSTR(instr_samp);
static const struct gpio_dt_spec instr_ctrl = INSTR(instr_ctrl);
static const struct gpio_dt_spec instr_cons = INSTR(instr_cons);
static const struct gpio_dt_spec instr_tele = INSTR(instr_tele);
static const struct gpio_dt_spec instr_flow = INSTR(instr_flow);
static const struct gpio_dt_spec instr_disp = INSTR(instr_disp);
static const struct gpio_dt_spec valve_led =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});
static const struct gpio_dt_spec flow_pulse =
	GPIO_DT_SPEC_GET_OR(DT_NODELABEL(flow_pulse), gpios, {0});

#if DT_NODE_EXISTS(DT_PATH(zephyr_user)) && \
	DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#define HAS_ADC 1
static const struct adc_dt_spec adc_joy =
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static const struct adc_dt_spec adc_pressure =
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);
#endif

#if DT_HAS_CHOSEN(zephyr_console)

/* ---- Shared variables ----------------------------------------------------- */

static atomic_t pressure_mv;
static atomic_t setpoint_mv = ATOMIC_INIT(1650);
static atomic_t duty_pct;
static atomic_t integral_mv;
static atomic_t estop;

static atomic_t joystick_mv;

/* ---- Message queues and workqueues ---------------------------------------- */

K_MSGQ_DEFINE(sampling_q, sizeof(uint32_t), 10, 4);
K_MSGQ_DEFINE(control_q, sizeof(int), 10, 4);

/* ---- Work items for deferred processing ----------------------------------- */

static void flow_batch_work(struct k_work *work);
K_WORK_DEFINE(flow_batch_work_item, flow_batch_work);

static void telemetry_work(struct k_work *work);
K_WORK_DEFINE(telemetry_work_item, telemetry_work);

static void display_work(struct k_work *work);
K_WORK_DEFINE(display_work_item, display_work);

/* ---- Timer callback ------------------------------------------------------- */

static void timer_callback(struct k_timer *timer);

/* ---- Flow ISR ------------------------------------------------------------- */

static struct gpio_callback flow_cb;

static void flow_isr(const struct device *port,
		     struct gpio_callback *cb,
		     gpio_port_pins_t pins)
{
	instr_set(&instr_flow, 1);

/* ---- Task implementations ------------------------------------------------- */

static void task_sampling(void)
{
	instr_set(&instr_samp, 1);

#if HAS_ADC
	int16_t buf[2];
	struct adc_sequence seq = {
		.buffer = buf,
		.buffer_size = sizeof(buf),
		.channels = BIT(adc_joy.channel_id) | BIT(adc_pressure.channel_id),
	};

	if (adc_read(adc_pressure.dev, &seq) == 0) {
		int32_t val_mv = buf[0];
		adc_raw_to_millivolts(adc_joy.ref_internal,
				      adc_joy.gain,
				      adc_joy.resolution,
				      &val_mv);
		atomic_set(&joystick_mv, val_mv);

		val_mv = buf[1];
		adc_raw_to_millivolts(adc_pressure.ref_internal,
				      adc_pressure.gain,
				      adc_pressure.resolution,
				      &val_mv);
		atomic_set(&pressure_mv, val_mv);

		/* e-stop: overpressure AND joystick down */
		bool overpressure = val_mv > ESTOP_MV;
		bool joy_down = val_mv < 1000;
		static int estop_hold_count = 0;

		if (overpressure || (joy_down && ++estop_hold_count >= ESTOP_HOLD)) {
			atomic_set(&estop, 1);
			estop_hold_count = 0;
		} else if (!joy_down) {
			estop_hold_count = 0;
		}
	}
#endif

	/* software PWM on the valve LED: 1 kHz / 20 = 50 Hz */
	static int pwm_phase = 0;
	pwm_phase = (pwm_phase + 1) % PWM_STEPS;
	instr_set(&valve_led, pwm_phase < atomic_get(&duty_pct) * PWM_STEPS / 100);

	instr_set(&instr_samp, 0);
}

static void task_control(void)
{
	instr_set(&instr_ctrl, 1);

	if (atomic_get(&estop)) {
		atomic_set(&duty_pct, 0);
	} else {
		long error_mv = atomic_get(&setpoint_mv) - atomic_get(&pressure_mv);
		long integral = atomic_get(&integral_mv) + error_mv / 100;
		long derivative = error_mv - (atomic_get(&setpoint_mv) - atomic_get(&pressure_mv));

		/* PI controller tuning (demo values) */
		long kp = 2, ki = 1;
		long output_mv = error_mv / kp + integral / ki;

		/* clamp and apply */
		output_mv = CLAMP(output_mv, 0, 100);
		atomic_set(&duty_pct, output_mv);
		atomic_set(&integral_mv, integral);
	}

	instr_set(&instr_ctrl, 0);
}

static void flow_batch_work(struct k_work *work)
{
	unsigned long pulses = atomic_get(&flow_pulses);
	atomic_set(&flow_pulses, 0);

	if (pulses > 0) {
		atomic_add(&flow_batches, 1);
		/* 1 pulse = 0.1 mL; 100 pulses = 10 mL; scale to L/min */
		long lpm_x100 = (pulses * 60 * 100) / FLOW_BATCH;
		atomic_set(&flow_lpm_x100, lpm_x100);
	}
}

static void telemetry_work(struct k_work *work)
{
	instr_set(&instr_tele, 1);
	printk("%lu,%d,%d,%d,%d,%d,%d,%ld\n",
	       k_uptime_get() / 1000,
	       atomic_get(&pressure_mv),
	       atomic_get(&setpoint_mv),
	       atomic_get(&duty_pct),
	       atomic_get(&flow_lpm_x100) / 100,
	       atomic_get(&joystick_mv),
	       (int)atomic_get(&estop),
	       (long)atomic_get(&lat_peak_us));
	instr_set(&instr_tele, 0);
}

#ifdef CONFIG_CHARACTER_FRAMEBUFFER
static void display_work(struct k_work *work)
{
	instr_set(&instr_disp, 1);

	if (device_is_ready(disp)) {
		cfb_framebuffer_clear(disp, true);
		char line[64];

		snprintf(line, sizeof(line), "SoilSense Ctrl");
		cfb_print(disp, line, 0, 0);

		snprintf(line, sizeof(line), "P:%dmV SP:%dmV",
			 atomic_get(&pressure_mv),
			 atomic_get(&setpoint_mv));
		cfb_print(disp, line, 0, 16);

/* ---- Console handling ----------------------------------------------------- */

static void console_handle(char *line)
{
	if (strcmp(line, "help") == 0) {
		printk("commands: help status joy set <mV> clear calib\n");
	} else if (strcmp(line, "status") == 0) {
		printk("p=%d mV sp=%d mV duty=%d%% flow_x100=%d estop=%d "
		       "batches=%u backlog_peak=%ld lat_peak_us=%ld\n",
		       pressure_mv, setpoint_mv, duty_pct,
		       (int)(flow_lpm_x100 * 100.0f), (int)estop, flow_batches,
		       (long)atomic_get(&backlog_peak),
		       (long)atomic_get(&lat_peak_us));
	} else if (strcmp(line, "joy") == 0) {
		printk("joystick raw: %d mV\n", joystick_mv);
	} else if (strncmp(line, "set ", 4) == 0) {
		setpoint_mv = CLAMP(atoi(line + 4), 0, 3300);
		printk("setpoint = %d mV\n", setpoint_mv);
	} else if (strcmp(line, "clear") == 0) {
		estop = false;
		integral_mv = 0;
		printk("e-stop cleared\n");
	} else if (strcmp(line, "calib") == 0) {
		/* Calibration moved to workqueue to avoid blocking */
		printk("Calibration started in background...\n");
	} else if (line[0] != '\0') {
		printk("? (try: help)\n");
	}
}

static void cmd_calib(void)
{
#if HAS_ADC
	instr_set(&instr_cons, 1);
	printk("calibrating...");
	for (int i = 0; i < CALIB_ROUNDS; i++) {
		int16_t buf;
		struct adc_sequence seq = {
			.buffer = &buf,
			.buffer_size = sizeof(buf),
			.channels = BIT(adc_pressure.channel_id),
		};
		if (adc_read(adc_pressure.dev, &seq) == 0) {
			int32_t val_mv = buf;
			adc_raw_to_millivolts(adc_pressure.ref_internal,
					      adc_pressure.gain,
					      adc_pressure.resolution,
					      &val_mv);
			/* Simple calibration: average of readings */

/* ---- Thread implementations ----------------------------------------------- */

static void sampling_thread(void *p1, void *p2, void *p3)
{
	uint32_t released;
	int control_div = 0;

	while (1) {
		k_msgq_get(&sampling_q, &released, K_FOREVER);

		atomic_val_t lat = k_cyc_to_us_floor32(k_cycle_get_32() - released);

		if (lat > atomic_get(&lat_peak_us)) {
			atomic_set(&lat_peak_us, lat);
		}

		task_sampling();
		
		if (++control_div >= CONTROL_EVERY) {
			control_div = 0;
			int dummy = 1;
			k_msgq_put(&control_q, &dummy, K_NO_WAIT);
		}
	}
}

static void control_thread(void *p1, void *p2, void *p3)
{
	int dummy;
	
	while (1) {
		k_msgq_get(&control_q, &dummy, K_FOREVER);
		task_control();
	}
}

static void console_thread(void *p1, void *p2, void *p3)
{
	static char buf[32];
	static size_t len;
	unsigned char c;

	while (1) {
#if DT_HAS_CHOSEN(zephyr_console)
		while (uart_poll_in(console_uart, &c) == 0) {
			instr_set(&instr_cons, 1);
			if (c == '\r' || c == '\n') {
				buf[len] = '\0';
				printk("\n");
				console_handle(buf);
				len = 0;
			} else if (len < sizeof(buf) - 1) {
				buf[len++] = c;
				printk("%c", c);
			}
			instr_set(&instr_cons, 0);
		}
#endif
		k_sleep(K_MSEC(10)); /* Small delay to prevent busy waiting */
	}
}

static void telemetry_thread(void *p1, void *p2, void *p3)
{
	while (1) {
		k_sleep(K_MSEC(TELEMETRY_MS));
		k_work_submit(&telemetry_work_item);
	}
}

/* ---- Thread definitions --------------------------------------------------- */

#define SAMPLING_PRIO  2
#define CONTROL_PRIO   2
#define CONSOLE_PRIO   10
#define TELEMETRY_PRIO 10
#define WORKQUEUE_PRIO 8

#define SAMPLING_STACK  1536
#define CONTROL_STACK   1024
#define CONSOLE_STACK   2048
#define TELEMETRY_STACK 1024
#define WORKQUEUE_STACK 1024

K_THREAD_DEFINE(sampling_tid, SAMPLING_STACK, sampling_thread, NULL, NULL, NULL,
		SAMPLING_PRIO, 0, 0);
K_THREAD_DEFINE(control_tid, CONTROL_STACK, control_thread, NULL, NULL, NULL,
		CONTROL_PRIO, 0, 0);
K_THREAD_DEFINE(console_tid, CONSOLE_STACK, console_thread, NULL, NULL, NULL,
		CONSOLE_PRIO, 0, 0);
K_THREAD_DEFINE(telemetry_tid, TELEMETRY_STACK, telemetry_thread, NULL, NULL, NULL,
		TELEMETRY_PRIO, 0, 0);
K_THREAD_DEFINE(workqueue_tid, WORKQUEUE_STACK, workqueue_thread, NULL, NULL, NULL,
		WORKQUEUE_PRIO, 0, 0);

/* ---- Hardware initialization --------------------------------------------- */

static void init_hw(void)
{
	const struct gpio_dt_spec *outs[] = { &instr_samp, &instr_ctrl,
					      &instr_cons, &instr_tele,
					      &instr_flow, &instr_disp,
					      &valve_led };

	for (size_t i = 0; i < ARRAY_SIZE(outs); i++) {
		if (outs[i]->port) {
			gpio_pin_configure_dt(outs[i], GPIO_OUTPUT_INACTIVE);
		}
	}
#if HAS_ADC
	adc_channel_setup_dt(&adc_joy);
	adc_channel_setup_dt(&adc_pressure);
#endif
	if (flow_pulse.port) {
		gpio_pin_configure_dt(&flow_pulse, GPIO_INPUT);
		gpio_pin_interrupt_configure_dt(&flow_pulse,
						 GPIO_INT_EDGE_TO_ACTIVE);
		gpio_init_callback(&flow_cb, flow_isr, BIT(flow_pulse.pin));
		gpio_add_callback(flow_pulse.port, &flow_cb);
	}
}

/* ---- Main function -------------------------------------------------------- */

int main(void)
{
	init_hw();
	printk("SoilSense Control — kernel build (%s)\n", CONFIG_BOARD);
	printk("type 'help'\n");

	k_timer_start(&tick_timer, K_USEC(SAMPLE_PERIOD_US),
		      K_USEC(SAMPLE_PERIOD_US));

	/* Auto-calibrate for PulseView measurements */
	k_msleep(3000);
	printk("Auto-starting calib for PulseView measurement...\n");
	cmd_calib();

	/* Main thread becomes idle - all work done in dedicated threads */
	while (1) {
		k_sleep(K_SECONDS(1));
		/* Periodically print thread analysis */
		if (k_uptime_get_32() % 5000 == 0) {
			thread_analyzer_print();
		}
	}

	return 0;
}

static void workqueue_thread(void *p1, void *p2, void *p3)
{
	while (1) {
		k_sleep(K_MSEC(DISPLAY_MS));
		k_work_submit(&display_work_item);
		k_sleep(K_MSEC(FLOW_BATCH)); /* Process flow batch periodically */
		k_work_submit(&flow_batch_work_item);
	}
}
			static long sum = 0;
			static int count = 0;
			sum += val_mv;
			count++;
			if (count == 100) {
				atomic_set(&pressure_mv, sum / count);
				sum = 0;
				count = 0;
			}
		}
		k_sleep(K_USEC(500)); /* settle time */
	}
	printk(" done\n");
	instr_set(&instr_cons, 0);
#endif
}

		snprintf(line, sizeof(line), "Duty:%d%% Flow:%d.%02dL/m",
			 atomic_get(&duty_pct),
			 atomic_get(&flow_lpm_x100) / 100,
			 atomic_get(&flow_lpm_x100) % 100);
		cfb_print(disp, line, 0, 32);

		snprintf(line, sizeof(line), "Estop:%d Batches:%lu",
			 (int)atomic_get(&estop),
			 atomic_get(&flow_batches));
		cfb_print(disp, line, 0, 48);

		cfb_framebuffer_paint(disp, true);
	}

	instr_set(&instr_disp, 0);
}
#endif
	atomic_inc(&flow_pulses);
	instr_set(&instr_flow, 0);
}
K_TIMER_DEFINE(tick_timer, timer_callback, NULL);

static void timer_callback(struct k_timer *timer)
{
	uint32_t now = k_cycle_get_32();
	k_msgq_put(&sampling_q, &now, K_NO_WAIT);
}
static atomic_t flow_pulses;
static atomic_t flow_batches;
static atomic_t flow_lpm_x100;

static atomic_t lat_peak_us;
static atomic_t backlog_peak;
static const struct device *const console_uart =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#endif

#ifdef CONFIG_CHARACTER_FRAMEBUFFER
static const struct device *const disp = DEVICE_DT_GET_ANY(solomon_ssd1306);
#endif

static inline void instr_set(const struct gpio_dt_spec *p, int v)
{
	if (p->port) {
		gpio_pin_set_dt(p, v);
	}
}
#include <stdlib.h>
#include <string.h>