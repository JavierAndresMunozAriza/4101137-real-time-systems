/*
 * SoilSense Control — the provided superloop.
 * One main loop + ISRs. No scheduler. You run, read, and measure this in week 2.
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
#include <stdlib.h>
#include <string.h>

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
/* ---- ISR side ------------------------------------------------------------- */

/* Sampling tick: the ISR only raises a flag; the loop discovers it whenever it
 * next comes around. If the loop was busy, ticks pile up here. */
static atomic_t ticks_pending;
static atomic_t backlog_peak; /* worst backlog seen — `status` reports it */

static void tick_isr(struct k_timer *t)
{
atomic_val_t backlog = atomic_inc(&ticks_pending) + 1;

if (backlog > atomic_get(&backlog_peak)) {
    atomic_set(&backlog_peak, backlog);
}
}
K_TIMER_DEFINE(tick_timer, tick_isr, NULL);

/* Flow pulses: counted in the ISR, processed in the loop in batches. */
static atomic_t flow_pulses;
static struct gpio_callback flow_cb;

static void flow_isr(const struct device *dev, struct gpio_callback *cb,
		     uint32_t pins)
{
	atomic_inc(&flow_pulses);
}

/* ---- State ---------------------------------------------------------------- */

static int setpoint_mv = 1500;
static int pressure_mv;
static int joystick_mv = -1;
static int duty_pct;          /* valve command, 0-100 */
static int integral_mv;
static bool estop;
static float flow_lpm;        /* batch math result (float on purpose: no FPU) */
static uint32_t flow_batches;

enum page { PAGE_DASH, PAGE_FLOW, PAGE_HEALTH, PAGE_COUNT };
static int page;
static bool page_dirty = true;

/* ---- Inputs: ADC when the board wires it, a synthetic plant when not ------ */

#if HAS_ADC
static int adc_read_mv(const struct adc_dt_spec *spec)
{
	int16_t raw;
	struct adc_sequence seq = { .buffer = &raw, .buffer_size = sizeof(raw) };
	int32_t mv;

	adc_sequence_init_dt(spec, &seq);
	if (adc_read_dt(spec, &seq) < 0) {
		return -1;
	}
	mv = raw;
	adc_raw_to_millivolts_dt(spec, &mv);
	return mv;
}
#endif

/* Without a pot on the pressure channel (or on native_sim) the plant is
 * simulated: pressure drifts toward what the valve commands. */
static int plant_mv(void)
{
	static int mv = 1200;

	mv += (duty_pct * 33 - mv) / 64;
	mv += (int)(k_cycle_get_32() % 7) - 3;
	return mv;
}

static int read_pressure_mv(void)
{
#if HAS_ADC
	int mv = adc_read_mv(&adc_pressure);

	if (mv >= 0) {
		return mv;
	}
#endif
	return plant_mv();
}

static int read_joystick_mv(void)
{
#if HAS_ADC
	return adc_read_mv(&adc_joy);
#else
	return -1;
#endif
}

/* ---- Tasks ---------------------------------------------------------------- */

/* Joystick bands, single-ADC resistor ladder. Calibrate with `joy`. */
enum joy { JOY_NONE, JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_SEL };

static enum joy joy_decode(int mv)
{
	if (mv < 0) return JOY_NONE;
	if (mv < 300) return JOY_SEL;
	if (mv < 900) return JOY_DOWN;
	if (mv < 1600) return JOY_RIGHT;
	if (mv < 2300) return JOY_LEFT;
	if (mv < 3000) return JOY_UP;
	return JOY_NONE;
}

/* Joystick UX, sampled at 50 Hz: left/right turn HMI pages, up/down move the
 * setpoint on the dashboard, down *held* ~1 s is the e-stop, center clears it. */
static void joystick_update(void)
{
	static enum joy prev = JOY_NONE;
	static int down_held;

	joystick_mv = read_joystick_mv();
	enum joy j = joy_decode(joystick_mv);

	if (j == JOY_DOWN) {
		if (++down_held == ESTOP_HOLD) {
			estop = true;
			page_dirty = true;
		}
	} else {
		down_held = 0;
	}

	switch (j) {
	case JOY_UP: /* held = ramp */
		if (page == PAGE_DASH) {
			setpoint_mv = CLAMP(setpoint_mv + 10, 0, 3300);
			page_dirty = true;
		}
		break;
	case JOY_DOWN: /* single step per press; keep holding for e-stop */
		if (page == PAGE_DASH && j != prev) {
			setpoint_mv = CLAMP(setpoint_mv - 10, 0, 3300);
			page_dirty = true;
		}
		break;
	case JOY_LEFT:
		if (j != prev) {
			page = (page + PAGE_COUNT - 1) % PAGE_COUNT;
			page_dirty = true;
		}
		break;
	case JOY_RIGHT:
		if (j != prev) {
			page = (page + 1) % PAGE_COUNT;
			page_dirty = true;
		}
		break;
	case JOY_SEL:
		estop = false;
		integral_mv = 0;
		page_dirty = true;
		break;
	default:
		break;
	}
	prev = j;
}

static void task_sampling(void)
{
	static int pwm_phase;
	static int joy_div;

	instr_set(&instr_samp, 1);

	pressure_mv = read_pressure_mv();

	/* e-stop condition is *detected* here, at 1 kHz... */
	if (pressure_mv > ESTOP_MV) {
		estop = true; /* ...but the valve only reacts when control runs */
	}

	/* joystick, decimated to 50 Hz */
	if (++joy_div >= 20) {
		joy_div = 0;
		joystick_update();
	}

	/* software PWM on the valve */
	pwm_phase = (pwm_phase + 1) % PWM_STEPS;
	if (valve_led.port) {
		gpio_pin_set_dt(&valve_led,
				pwm_phase < (duty_pct * PWM_STEPS) / 100);
	}

	instr_set(&instr_samp, 0);
}

static void task_control(void)
{
	instr_set(&instr_ctrl, 1);

	if (estop) {
		duty_pct = 0;
	} else {
		int error = setpoint_mv - pressure_mv;

		integral_mv += error / 8;
		integral_mv = CLAMP(integral_mv, -2000, 2000);
		duty_pct = CLAMP((error + integral_mv) / 20, 0, 100);
	}

	instr_set(&instr_ctrl, 0);
}

/* Every FLOW_BATCH pulses: convert the accumulated count to L/min and smooth
 * it. Float math, in the middle of the loop, on a core without an FPU. */
static void task_flow_batch(void)
{
	if (atomic_get(&flow_pulses) < FLOW_BATCH) {
		return;
	}
	instr_set(&instr_flow, 1);

	float pulses = (float)atomic_clear(&flow_pulses);
	float lpm = pulses / 5880.0f * 60.0f; /* YF-S401: 5880 pulses/L */

	for (int i = 0; i < 50; i++) {
		flow_lpm = flow_lpm * 0.9f + lpm * 0.1f;
	}
	flow_batches++;

	instr_set(&instr_flow, 0);
}

static void task_telemetry(void)
{
	static int64_t next_ms;
	char line[96];

	if (k_uptime_get() < next_ms) {
		return;
	}
	next_ms += TELEMETRY_MS;
	instr_set(&instr_tele, 1);

	snprintf(line, sizeof(line),
		 "t=%u p_mv=%d sp_mv=%d duty=%d flow_x100=%d estop=%d backlog=%ld",
		 (unsigned int)k_uptime_get(), pressure_mv, setpoint_mv, duty_pct,
		 (int)(flow_lpm * 100.0f), (int)estop,
		 (long)atomic_get(&backlog_peak));
	printk("%s\n", line); /* polled UART: ~7 ms of blocking at 115200 */

	instr_set(&instr_tele, 0);
}

/* ---- Display (soft "HMI"): SSD1306 pages, redrawn whole ------------------- */

#ifdef CONFIG_CHARACTER_FRAMEBUFFER
static void task_display(void)
{
	static int64_t next_ms;
	static bool ready;
	char line[24];

	if (disp == NULL) {
		return;
	}
	if (!ready) {
		if (!device_is_ready(disp) || cfb_framebuffer_init(disp) != 0) {
			return;
		}
		display_blanking_off(disp);
		ready = true;
	}
	if (!page_dirty && k_uptime_get() < next_ms) {
		return;
	}
	next_ms = k_uptime_get() + DISPLAY_MS;
	page_dirty = false;

	instr_set(&instr_disp, 1);
	cfb_framebuffer_clear(disp, false);
	switch (page) {
	case PAGE_DASH:
		snprintf(line, sizeof(line), "P %4d>%4dmV", pressure_mv,
			 setpoint_mv);
		cfb_print(disp, line, 0, 0);
		snprintf(line, sizeof(line), "duty %3d%% %s", duty_pct,
			 estop ? "ESTOP" : "");
		cfb_print(disp, line, 0, 16);
		cfb_print(disp, "1/3 dash", 0, 48);
		break;
	case PAGE_FLOW:
		snprintf(line, sizeof(line), "flow %d.%02d L/m",
			 (int)flow_lpm, (int)(flow_lpm * 100.0f) % 100);
		cfb_print(disp, line, 0, 0);
		snprintf(line, sizeof(line), "batches %u", flow_batches);
		cfb_print(disp, line, 0, 16);
		cfb_print(disp, "2/3 flow", 0, 48);
		break;
	case PAGE_HEALTH:
		snprintf(line, sizeof(line), "up %us",
			 (unsigned int)(k_uptime_get() / 1000));
		cfb_print(disp, line, 0, 0);
		snprintf(line, sizeof(line), "backlog pk %ld",
			 (long)atomic_get(&backlog_peak));
		cfb_print(disp, line, 0, 16);
		cfb_print(disp, "3/3 health", 0, 48);
		break;
	}
	/* the whole 1 KB frame goes over I2C here, char by char: blocking */
	cfb_framebuffer_finalize(disp);
	instr_set(&instr_disp, 0);
}
#else
static void task_display(void)
{
}
#endif

/* ---- Console (firm): poll chars, run a full command synchronously --------- */

static void cmd_calib(void)
{
	printk("calibrating zero-flow offset (%d rounds)...\n", CALIB_ROUNDS);
	for (int i = 0; i < CALIB_ROUNDS; i++) {
		(void)read_pressure_mv();
		k_busy_wait(400); /* sensor settling time between reads */
	}
	printk("calibration done\n");
}

static void console_handle(char *line)
{
	if (strcmp(line, "help") == 0) {
		printk("commands: help status joy set <mv> clear calib\n");
	} else if (strcmp(line, "status") == 0) {
		printk("p=%d mV sp=%d mV duty=%d%% flow_x100=%d estop=%d "
		       "batches=%u backlog_peak=%ld\n",
		       pressure_mv, setpoint_mv, duty_pct,
		       (int)(flow_lpm * 100.0f), (int)estop, flow_batches,
		       (long)atomic_get(&backlog_peak));
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
		cmd_calib();
	} else if (line[0] != '\0') {
		printk("? (try: help)\n");
	}
}

static void task_console(void)
{
	static char buf[32];
	static size_t len;
	unsigned char c;

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
}

/* ---- Bring-up + the loop -------------------------------------------------- */

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

int main(void)
{
	init_hw();
	printk("SoilSense Control — superloop build (%s)\n", CONFIG_BOARD);
	printk("type 'help'\n");

	k_timer_start(&tick_timer, K_USEC(SAMPLE_PERIOD_US),
		      K_USEC(SAMPLE_PERIOD_US));

	int control_div = 0;

while (1) {
    task_console();
    task_display();
    task_telemetry();

    if (atomic_get(&ticks_pending) > 0) {
        atomic_dec(&ticks_pending);
        task_sampling();
        if (++control_div >= CONTROL_EVERY) {
            control_div = 0;
            task_control();
        }
    }

    task_flow_batch();
}
	return 0;
}
