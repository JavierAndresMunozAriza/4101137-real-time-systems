/*
 * SoilSense Control — week 4: the full migration to the Zephyr kernel,
 * instrumented in week 5 (thread names, runtime stats, CTF tracing).
 * Every superloop task is a thread (or deferred work) with its own priority;
 * main only brings the hardware up and returns.
 *
 * The node's tasks (PROJECT_SCENARIO.md):
 *   sampling  (hard, 1 kHz)  — pressure ADC, e-stop check, valve PWM phase
 *   control   (hard, 10 ms)  — PI loop -> valve duty
 *   console   (firm)         — UART commands
 *                              (help/status/joy/set/clear/calib/threads)
 *   telemetry (soft, 1 Hz)   — status line over the console UART
 *   flow ISR  (hard)         — pulse counting on the flow input pin
 *
 * Every thread-level task raises its instrumentation GPIO on entry and drops
 * it on exit: the logic analyzer sees exactly who ran and for how long.
 * The flow ISR is the exception: it is a single atomic add, so it has no GPIO
 * edge of its own and its duration is read from the CTF isr_enter/isr_exit
 * events (week 5). `instr_flow` marks the deferred batch math on flow_wq.
 *
 * Shared state lives in atomic_t: it is written and read from several threads
 * and ISRs, and the ESP32-S3 port gives no other ordering guarantee.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#ifdef CONFIG_THREAD_ANALYZER
#include <zephyr/debug/thread_analyzer.h>
#endif
#ifdef CONFIG_CHARACTER_FRAMEBUFFER
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#endif
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Timing and sizing ---------------------------------------------------- */

#define SAMPLE_PERIOD_US 1000  /* sampling: 1 kHz */
#define CONTROL_EVERY    10    /* control runs every 10th sample -> 10 ms */
#define JOY_EVERY        20    /* joystick decimation: 1 kHz / 20 = 50 Hz */
#define TELEMETRY_MS     1000
#define PWM_STEPS        20    /* software PWM on the valve LED: 1 kHz / 20 = 50 Hz */
#define FLOW_BATCH       100   /* pulses accumulated before the batch math runs */
#define ESTOP_MV         3000  /* overpressure threshold */
#define CALIB_ROUNDS     1000  /* `calib` command: rounds of read + settle */
#define CALIB_SETTLE_US  400   /* sensor settling time between calib reads */
#define DISPLAY_MS       500   /* HMI refresh period */
#define ESTOP_HOLD       50    /* joystick-down samples (at 50 Hz) = 1 s hold */
#define TICK_Q_DEPTH     8     /* sampling ticks that may pile up unattended */
#define TICK_Q_ALIGN     4     /* alignment of the tick queue's ring buffer */
#define CMD_SET          "set " /* console: set <mv> */

/* ---- Units and scaling ---------------------------------------------------- */

#define PCT_MAX          100
#define X100             100   /* fixed-point scale for L/min */
#define SETPOINT_MAX_MV  3300  /* ADC full scale */
#define SETPOINT_STEP_MV 10    /* joystick up/down step */
#define SETPOINT_INIT_MV 1500

/* PI loop (demo tuning): I += e/8, |I| <= 2000, duty = (e + I) / 20 */
#define PI_KI_DIV        8
#define PI_I_LIMIT       2000
#define PI_OUT_DIV       20

/* Lab 06 Task C: artificial actuation delay, a whole number of ms in
 * [0, JITTER_MAX_MS] drawn from a fixed-seed xorshift32, so every run replays
 * the same delays. 0 compiles the injection out. */
#define JITTER_MAX_MS    CONFIG_LAB06_JITTER_MAX_MS
#define JITTER_SEED      2463534242U
#define XORSHIFT_A       13   /* Marsaglia xorshift32 shift triple */
#define XORSHIFT_B       17
#define XORSHIFT_C       5

/* Synthetic plant (no pot on the pressure channel): first-order lag plus noise */
#define PLANT_START_MV   1200
#define PLANT_GAIN       33
#define PLANT_LAG_DIV    64
#define PLANT_NOISE_MOD  7
#define PLANT_NOISE_OFF  3

/* Flow batch math (YF-S401) */
#define FLOW_PULSES_PER_L 5880.0f
#define FLOW_SEC_PER_MIN  60.0f
#define FLOW_SMOOTH_ITERS 50    /* deliberate soft-float load on flow_wq */
#define FLOW_SMOOTH_KEEP  0.9f
#define FLOW_SMOOTH_NEW   0.1f

/* Joystick resistor-ladder bands (mV). Calibrate with `joy`. */
#define JOY_SEL_MV       300
#define JOY_DOWN_MV      900
#define JOY_RIGHT_MV     1600
#define JOY_LEFT_MV      2300
#define JOY_UP_MV        3000

/* HMI rows */
#define HMI_ROW_0        0
#define HMI_ROW_1        16
#define HMI_ROW_FOOTER   48
#define HMI_LINE_LEN     24

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
		/* instrumentation: a failed edge is not actionable, checking would
		 * lengthen every toggle the analyzer measures */
		(void)gpio_pin_set_dt(p, v);
	}
}

/* ---- Shared state --------------------------------------------------------- */

#define LOAD(v) ((long)atomic_get(&(v)))

static atomic_t setpoint_mv = ATOMIC_INIT(SETPOINT_INIT_MV);
static atomic_t pressure_mv;
static atomic_t joystick_mv = ATOMIC_INIT(-1);
static atomic_t duty_pct;          /* valve command, 0-100 */
static atomic_t integral_mv;
static atomic_t estop;
static atomic_t flow_lpm_x100;     /* batch math result, L/min x 100 */
static atomic_t flow_batches;
static atomic_t flow_pulses;       /* counted in the ISR */
static atomic_t backlog_peak;      /* worst tick backlog seen — `status` reports it */
static atomic_t lat_peak_us;       /* worst release -> thread start */
static atomic_t ticks_dropped;     /* ticks lost because tick_q was full */
static atomic_t hmi_errors;        /* failed display calls */
static atomic_t valve_errors;      /* failed valve GPIO writes */
static atomic_t err_abs_sum;       /* sum of |setpoint - pressure| per control cycle, mV */
static atomic_t err_cycles;        /* control cycles accumulated in err_abs_sum */
static atomic_t flow_submit_failed; /* flow batches that could not be queued */

enum page { PAGE_DASH, PAGE_FLOW, PAGE_HEALTH, PAGE_COUNT };
static atomic_t page;
static atomic_t page_dirty = ATOMIC_INIT(1);

static void request_redraw(void)
{
	atomic_set(&page_dirty, 1);
}

/* Restart the loop-error window (`set` starts a new step response). Not atomic
 * as a pair: a control cycle landing between the two clears skews one sample. */
static void loop_err_reset(void)
{
	atomic_clear(&err_abs_sum);
	atomic_clear(&err_cycles);
}

/* The valve command is an actuator, not instrumentation: a failed write is
 * counted and reported by `status`. */
static void valve_set(int on)
{
	if (valve_led.port && gpio_pin_set_dt(&valve_led, on) != 0) {
		atomic_inc(&valve_errors);
	}
}

/* The only writers of the setpoint: clamped, and the step is a CAS loop so a
 * joystick step cannot overwrite a concurrent `set` from the console. */
static void setpoint_set(int mv)
{
	atomic_set(&setpoint_mv, CLAMP(mv, 0, SETPOINT_MAX_MV));
	request_redraw();
}

static void setpoint_step(int delta)
{
	atomic_val_t old;
	atomic_val_t next;

	do {
		old = atomic_get(&setpoint_mv);
		next = CLAMP(old + delta, 0, SETPOINT_MAX_MV);
	} while (!atomic_cas(&setpoint_mv, old, next));
	request_redraw();
}

/* Reset the integrator *before* releasing the e-stop: while the e-stop is set
 * control never touches the integrator, so the reset cannot be overwritten.
 * Two benign races remain, both bounded by one 10 ms control cycle:
 *  - `clear` with the e-stop already clear can lose the reset to a control
 *    update in flight (the integrator is clamped to +/-PI_I_LIMIT anyway);
 *  - sampling can raise the e-stop just after control read it clear, so the
 *    valve keeps its duty until the next control cycle. */
static void estop_clear(void)
{
	atomic_set(&integral_mv, 0);
	atomic_set(&estop, 0);
	request_redraw();
}

/* ---- ISR side ------------------------------------------------------------- */

/* Sampling tick: the ISR queues the release time and returns; the sampling
 * thread wakes on it. Ticks still pile up here if the thread falls behind. */
K_MSGQ_DEFINE_STATIC(tick_q, sizeof(uint32_t), TICK_Q_DEPTH, TICK_Q_ALIGN);
static K_SEM_DEFINE(control_sem, 0, CONTROL_EVERY);

static void tick_isr(struct k_timer *t)
{
	uint32_t released = k_cycle_get_32();

	if (k_msgq_put(&tick_q, &released, K_NO_WAIT) != 0) {
		atomic_inc(&ticks_dropped);
	}

	atomic_val_t backlog = k_msgq_num_used_get(&tick_q);

	if (backlog > atomic_get(&backlog_peak)) {
		atomic_set(&backlog_peak, backlog);
	}
}
static K_TIMER_DEFINE(tick_timer, tick_isr, NULL);

/* Flow pulses: counted in the ISR; each full batch is deferred to flow_wq. */
static struct gpio_callback flow_cb;
static struct k_work_q flow_wq;
static struct k_work flow_work;

static void flow_isr(const struct device *dev, struct gpio_callback *cb,
		     uint32_t pins)
{
	if (atomic_inc(&flow_pulses) + 1 >= FLOW_BATCH &&
	    k_work_submit_to_queue(&flow_wq, &flow_work) < 0) {
		atomic_inc(&flow_submit_failed);
	}
}

/* ---- Inputs: ADC when the board wires it, a synthetic plant when not ------ */

#if HAS_ADC
static int adc_read_mv(const struct adc_dt_spec *spec)
{
	int16_t raw;
	struct adc_sequence seq = { .buffer = &raw, .buffer_size = sizeof(raw) };
	int32_t mv;

	if (adc_sequence_init_dt(spec, &seq) < 0 || adc_read_dt(spec, &seq) < 0) {
		return -1;
	}
	mv = raw;
	if (adc_raw_to_millivolts_dt(spec, &mv) < 0) {
		return -1;
	}
	return mv;
}
#endif

/* Without a pot on the pressure channel (or on native_sim) the plant is
 * simulated: pressure drifts toward what the valve commands. Only the sampling
 * thread calls it, so `mv` needs no protection. */
static int plant_mv(void)
{
	static int mv = PLANT_START_MV;

	mv += ((int)LOAD(duty_pct) * PLANT_GAIN - mv) / PLANT_LAG_DIV;
	mv += (int)(k_cycle_get_32() % PLANT_NOISE_MOD) - PLANT_NOISE_OFF;
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

enum joy { JOY_NONE, JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_SEL };

static enum joy joy_decode(int mv)
{
	if (mv < 0) {
		return JOY_NONE;
	}
	if (mv < JOY_SEL_MV) {
		return JOY_SEL;
	}
	if (mv < JOY_DOWN_MV) {
		return JOY_DOWN;
	}
	if (mv < JOY_RIGHT_MV) {
		return JOY_RIGHT;
	}
	if (mv < JOY_LEFT_MV) {
		return JOY_LEFT;
	}
	if (mv < JOY_UP_MV) {
		return JOY_UP;
	}
	return JOY_NONE;
}

/* Joystick UX, sampled at 50 Hz: left/right turn HMI pages, up/down move the
 * setpoint on the dashboard, down *held* ~1 s is the e-stop, center clears it. */
static void joystick_update(void)
{
	static enum joy prev = JOY_NONE;
	static int down_held;
	int mv = read_joystick_mv();
	enum joy j = joy_decode(mv);

	atomic_set(&joystick_mv, mv);

	if (j == JOY_DOWN) {
		if (++down_held == ESTOP_HOLD) {
			atomic_set(&estop, 1);
			request_redraw();
		}
	} else {
		down_held = 0;
	}

	switch (j) {
	case JOY_UP: /* held = ramp */
		if (LOAD(page) == PAGE_DASH) {
			setpoint_step(SETPOINT_STEP_MV);
		}
		break;
	case JOY_DOWN: /* single step per press; keep holding for e-stop */
		if (LOAD(page) == PAGE_DASH && j != prev) {
			setpoint_step(-SETPOINT_STEP_MV);
		}
		break;
	case JOY_LEFT:
		if (j != prev) {
			atomic_set(&page, (LOAD(page) + PAGE_COUNT - 1) % PAGE_COUNT);
			request_redraw();
		}
		break;
	case JOY_RIGHT:
		if (j != prev) {
			atomic_set(&page, (LOAD(page) + 1) % PAGE_COUNT);
			request_redraw();
		}
		break;
	case JOY_SEL:
		estop_clear();
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
	int p;

	instr_set(&instr_samp, 1);

	p = read_pressure_mv();
	atomic_set(&pressure_mv, p);

	/* e-stop condition is *detected* here, at 1 kHz... */
	if (p > ESTOP_MV) {
		atomic_set(&estop, 1); /* ...but the valve only reacts when control runs */
	}

	/* joystick, decimated to 50 Hz */
	if (++joy_div >= JOY_EVERY) {
		joy_div = 0;
		joystick_update();
	}

	/* software PWM on the valve */
	pwm_phase = (pwm_phase + 1) % PWM_STEPS;
	valve_set(pwm_phase < ((int)LOAD(duty_pct) * PWM_STEPS) / PCT_MAX);

	instr_set(&instr_samp, 0);
}

#if JITTER_MAX_MS > 0
/* Only the control thread calls it. */
static unsigned int jitter_ms(void)
{
	static uint32_t x = JITTER_SEED;

	x ^= x << XORSHIFT_A;
	x ^= x >> XORSHIFT_B;
	x ^= x << XORSHIFT_C;
	return x % (JITTER_MAX_MS + 1U);
}
#endif

static void task_control(void)
{
	instr_set(&instr_ctrl, 1);

	if (atomic_get(&estop)) {
		atomic_set(&duty_pct, 0);
	} else {
		int error = (int)(LOAD(setpoint_mv) - LOAD(pressure_mv));
		int integral = CLAMP((int)LOAD(integral_mv) + error / PI_KI_DIV,
				     -PI_I_LIMIT, PI_I_LIMIT);

		atomic_add(&err_abs_sum, abs(error));
		atomic_inc(&err_cycles);
		atomic_set(&integral_mv, integral);
#if JITTER_MAX_MS > 0
		unsigned int late_ms = jitter_ms(); /* Task C: late actuation */

		if (late_ms > 0U) {
			k_msleep(late_ms);
		}
#endif
		atomic_set(&duty_pct, CLAMP((error + integral) / PI_OUT_DIV, 0, PCT_MAX));
	}

	instr_set(&instr_ctrl, 0);
}

/* Every FLOW_BATCH pulses: convert the accumulated count to L/min and smooth
 * it. Float math on flow_wq, soft-float (CONFIG_FPU is off). Only flow_wq
 * touches `smoothed`; everyone else reads the fixed-point result. */
static void task_flow_batch(void)
{
	static float smoothed;

	if (atomic_get(&flow_pulses) < FLOW_BATCH) {
		return;
	}
	instr_set(&instr_flow, 1);

	float pulses = (float)atomic_clear(&flow_pulses);
	float lpm = pulses / FLOW_PULSES_PER_L * FLOW_SEC_PER_MIN;

	for (int i = 0; i < FLOW_SMOOTH_ITERS; i++) {
		smoothed = smoothed * FLOW_SMOOTH_KEEP + lpm * FLOW_SMOOTH_NEW;
	}
	atomic_set(&flow_lpm_x100, (atomic_val_t)(smoothed * X100));
	atomic_inc(&flow_batches);

	instr_set(&instr_flow, 0);
}

/* The telemetry thread owns the period (absolute deadlines); this only prints. */
static void task_telemetry(void)
{
	char line[96];

	instr_set(&instr_tele, 1);

	snprintf(line, sizeof(line),
		 "t=%u p_mv=%ld sp_mv=%ld duty=%ld flow_x100=%ld estop=%ld backlog=%ld",
		 (unsigned int)k_uptime_get(), LOAD(pressure_mv), LOAD(setpoint_mv),
		 LOAD(duty_pct), LOAD(flow_lpm_x100), LOAD(estop), LOAD(backlog_peak));
	printk("%s\n", line); /* polled UART: ~7 ms of blocking at 115200 */

	instr_set(&instr_tele, 0);
}

/* ---- Display (soft "HMI"): SSD1306 pages, redrawn whole ------------------- */

#ifdef CONFIG_CHARACTER_FRAMEBUFFER
static void hmi_check(int rc)
{
	if (rc != 0) {
		atomic_inc(&hmi_errors);
	}
}

static void hmi_printf(int y, const char *fmt, ...) __printf_like(2, 3);

static void hmi_printf(int y, const char *fmt, ...)
{
	char line[HMI_LINE_LEN];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	hmi_check(cfb_print(disp, line, 0, y));
}

static void task_display(void)
{
	static int64_t next_ms;
	static bool ready;

	if (disp == NULL) {
		return;
	}
	if (!ready) {
		if (!device_is_ready(disp) || cfb_framebuffer_init(disp) != 0 ||
		    display_blanking_off(disp) != 0) {
			return;
		}
		ready = true;
	}
	/* test-and-clear in one step: a redraw requested while we draw is kept */
	bool dirty = atomic_clear(&page_dirty) != 0;

	if (!dirty && k_uptime_get() < next_ms) {
		return;
	}
	next_ms = k_uptime_get() + DISPLAY_MS;

	instr_set(&instr_disp, 1);
	hmi_check(cfb_framebuffer_clear(disp, false));
	switch (LOAD(page)) {
	case PAGE_DASH:
		hmi_printf(HMI_ROW_0, "P %4ld>%4ldmV", LOAD(pressure_mv), LOAD(setpoint_mv));
		hmi_printf(HMI_ROW_1, "duty %3ld%% %s", LOAD(duty_pct),
			   atomic_get(&estop) ? "ESTOP" : "");
		hmi_printf(HMI_ROW_FOOTER, "1/3 dash");
		break;
	case PAGE_FLOW:
		hmi_printf(HMI_ROW_0, "flow %ld.%02ld L/m", LOAD(flow_lpm_x100) / X100,
			   LOAD(flow_lpm_x100) % X100);
		hmi_printf(HMI_ROW_1, "batches %ld", LOAD(flow_batches));
		hmi_printf(HMI_ROW_FOOTER, "2/3 flow");
		break;
	case PAGE_HEALTH:
		hmi_printf(HMI_ROW_0, "up %us",
			   (unsigned int)(k_uptime_get() / MSEC_PER_SEC));
		hmi_printf(HMI_ROW_1, "backlog pk %ld", LOAD(backlog_peak));
		hmi_printf(HMI_ROW_FOOTER, "3/3 health");
		break;
	default:
		break;
	}
	/* the whole 1 KB frame goes over I2C here, char by char: blocking */
	hmi_check(cfb_framebuffer_finalize(disp));
	instr_set(&instr_disp, 0);
}
#else
static void task_display(void)
{
}
#endif

/* ---- Console (firm): poll chars, run a full command synchronously --------- */

/* `calib` is a deliberate CPU hog: it is the load the Lab 04 A/B test throws at
 * the node to prove the sampling thread is isolated from the console. It runs
 * once per explicit command (never periodically) on the lowest-priority user
 * thread, so the busy-wait is the point, not an accident. */
static void cmd_calib(void)
{
	printk("calibrating zero-flow offset (%d rounds)...\n", CALIB_ROUNDS);
	for (int i = 0; i < CALIB_ROUNDS; i++) {
#if HAS_ADC
		(void)adc_read_mv(&adc_pressure);
#endif
		k_busy_wait(CALIB_SETTLE_US);
	}
	printk("calibration done\n");
}

/* Mean |error| over the current window, mV (0 before the first control cycle). */
static long err_cycles_avg(void)
{
	long n = LOAD(err_cycles);

	return n > 0 ? LOAD(err_abs_sum) / n : 0;
}

static void console_handle(char *line)
{
	if (strcmp(line, "help") == 0) {
		printk("commands: help status joy set <mv> clear calib threads\n");
	} else if (strcmp(line, "status") == 0) {
		printk("p=%ld mV sp=%ld mV duty=%ld%% flow_x100=%ld estop=%ld "
		       "batches=%ld backlog_peak=%ld lat_peak_us=%ld "
		       "ticks_dropped=%ld hmi_errors=%ld valve_errors=%ld flow_submit_failed=%ld "
		       "loop_err_mv=%ld jitter_max_ms=%d\n",
		       LOAD(pressure_mv), LOAD(setpoint_mv), LOAD(duty_pct),
		       LOAD(flow_lpm_x100), LOAD(estop), LOAD(flow_batches),
		       LOAD(backlog_peak), LOAD(lat_peak_us), LOAD(ticks_dropped),
		       LOAD(hmi_errors), LOAD(valve_errors), LOAD(flow_submit_failed),
		       err_cycles_avg(), JITTER_MAX_MS);
	} else if (strcmp(line, "joy") == 0) {
		printk("joystick raw: %ld mV\n", LOAD(joystick_mv));
	} else if (strncmp(line, CMD_SET, sizeof(CMD_SET) - 1) == 0) {
		setpoint_set(atoi(line + sizeof(CMD_SET) - 1));
		loop_err_reset();
		printk("setpoint = %ld mV\n", LOAD(setpoint_mv));
	} else if (strcmp(line, "clear") == 0) {
		estop_clear();
		printk("e-stop cleared\n");
	} else if (strcmp(line, "calib") == 0) {
		cmd_calib();
#ifdef CONFIG_THREAD_ANALYZER
	} else if (strcmp(line, "threads") == 0) {
		thread_analyzer_print(0);
#endif
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

/* ---- Threads ------------------------------------------------------------- */

/* Rate-monotonic order: the shorter the period, the more important. `main`
 * runs at CONFIG_MAIN_THREAD_PRIORITY (10), below all of them, so they are
 * created held and main() releases them after the bring-up. */
#define SAMPLING_PRIO  2
#define CONTROL_PRIO   3
#define FLOW_WQ_PRIO   5
#define DISPLAY_PRIO   7
#define TELEMETRY_PRIO 8
#define CONSOLE_PRIO   9

#define SAMPLING_STACK  1536
#define CONTROL_STACK   1024
#define DISPLAY_STACK   2048
#define TELEMETRY_STACK 2048
#define CONSOLE_STACK   2048
#define FLOW_WQ_STACK   1024

/* Threads are created held (SYS_FOREVER_MS) and released by main() once the
 * hardware is up, so none of them touches a pin or device before init_hw(). */
#define BOOT_HOLD       SYS_FOREVER_MS

#define CONSOLE_POLL_MS 5  /* the S3's 128-byte RX FIFO holds ~11 ms at 115200 */
#define DISPLAY_POLL_MS 50

static void sampling_thread(void *p1, void *p2, void *p3)
{
	uint32_t released;
	int control_div = 0;

	while (1) {
		if (k_msgq_get(&tick_q, &released, K_FOREVER) != 0) {
			continue;
		}

		atomic_val_t lat = k_cyc_to_us_floor32(k_cycle_get_32() - released);

		if (lat > atomic_get(&lat_peak_us)) {
			atomic_set(&lat_peak_us, lat);
		}

		task_sampling();
		if (++control_div >= CONTROL_EVERY) {
			control_div = 0;
			k_sem_give(&control_sem);
		}
	}
}
K_THREAD_DEFINE(sampling_tid, SAMPLING_STACK, sampling_thread, NULL, NULL, NULL,
		SAMPLING_PRIO, 0, BOOT_HOLD);

static void control_thread(void *p1, void *p2, void *p3)
{
	while (1) {
		if (k_sem_take(&control_sem, K_FOREVER) == 0) {
			task_control();
		}
	}
}
K_THREAD_DEFINE(control_tid, CONTROL_STACK, control_thread, NULL, NULL, NULL,
		CONTROL_PRIO, 0, BOOT_HOLD);

static void display_thread(void *p1, void *p2, void *p3)
{
	while (1) {
		task_display();
		k_msleep(DISPLAY_POLL_MS); /* soft task: drift is acceptable */
	}
}
K_THREAD_DEFINE(display_tid, DISPLAY_STACK, display_thread, NULL, NULL, NULL,
		DISPLAY_PRIO, 0, BOOT_HOLD);

static void telemetry_thread(void *p1, void *p2, void *p3)
{
	int64_t next_ms = k_uptime_get();

	while (1) {
		task_telemetry();
		next_ms += TELEMETRY_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next_ms));
	}
}
K_THREAD_DEFINE(telemetry_tid, TELEMETRY_STACK, telemetry_thread, NULL, NULL, NULL,
		TELEMETRY_PRIO, 0, BOOT_HOLD);

/* Polls on absolute deadlines (the RX FIFO budget is tight), then sleeps: a
 * console that spun here would starve idle. After a long command such as
 * `calib` the missed polls are skipped, not replayed. */
static void console_thread(void *p1, void *p2, void *p3)
{
	int64_t next_ms = k_uptime_get();

	while (1) {
		task_console();
		next_ms += CONSOLE_POLL_MS;
		if (next_ms < k_uptime_get()) {
			next_ms = k_uptime_get();
		}
		k_sleep(K_TIMEOUT_ABS_MS(next_ms));
	}
}
K_THREAD_DEFINE(console_tid, CONSOLE_STACK, console_thread, NULL, NULL, NULL,
		CONSOLE_PRIO, 0, BOOT_HOLD);

/* Its own queue, not the system one: that runs at priority -1 (cooperative)
 * and would hold sampling off for the whole batch. */
K_THREAD_STACK_DEFINE(flow_wq_stack, FLOW_WQ_STACK);

static void flow_work_fn(struct k_work *work)
{
	task_flow_batch();
}

/* ---- Bring-up ------------------------------------------------------------ */

static int gpio_out_init(const struct gpio_dt_spec *p)
{
	if (!p->port) {
		return 0; /* optional node absent: degrade to the synthetic rig */
	}
	if (!gpio_is_ready_dt(p)) {
		return -ENODEV;
	}
	return gpio_pin_configure_dt(p, GPIO_OUTPUT_INACTIVE);
}

static int flow_input_init(void)
{
	int rc;

	if (!flow_pulse.port) {
		return 0;
	}
	if (!gpio_is_ready_dt(&flow_pulse)) {
		return -ENODEV;
	}
	rc = gpio_pin_configure_dt(&flow_pulse, GPIO_INPUT);
	if (rc == 0) {
		rc = gpio_pin_interrupt_configure_dt(&flow_pulse, GPIO_INT_EDGE_TO_ACTIVE);
	}
	if (rc == 0) {
		gpio_init_callback(&flow_cb, flow_isr, BIT(flow_pulse.pin));
		rc = gpio_add_callback(flow_pulse.port, &flow_cb);
	}
	return rc;
}

static int init_hw(void)
{
	const struct gpio_dt_spec *outs[] = { &instr_samp, &instr_ctrl,
					      &instr_cons, &instr_tele,
					      &instr_flow, &instr_disp,
					      &valve_led };
	int rc;

	for (size_t i = 0; i < ARRAY_SIZE(outs); i++) {
		rc = gpio_out_init(outs[i]);
		if (rc != 0) {
			return rc;
		}
	}
#if HAS_ADC
	if (!adc_is_ready_dt(&adc_joy) || !adc_is_ready_dt(&adc_pressure)) {
		return -ENODEV;
	}
	rc = adc_channel_setup_dt(&adc_joy);
	if (rc != 0) {
		return rc;
	}
	rc = adc_channel_setup_dt(&adc_pressure);
	if (rc != 0) {
		return rc;
	}
#endif
#if DT_HAS_CHOSEN(zephyr_console)
	if (!device_is_ready(console_uart)) {
		return -ENODEV;
	}
#endif
	return flow_input_init();
}

/* Name each thread (trace viewer, `threads`: CONFIG_THREAD_NAME) and release it. */
static void start_threads(void)
{
	const struct {
		k_tid_t tid;
		const char *name;
	} names[] = {
		{ sampling_tid, "sampling" },
		{ control_tid, "control" },
		{ display_tid, "display" },
		{ telemetry_tid, "telemetry" },
		{ console_tid, "console" },
	};

	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		if (k_thread_name_set(names[i].tid, names[i].name) != 0) {
			printk("thread name '%s' not set\n", names[i].name);
		}
		k_thread_start(names[i].tid);
	}
}

int main(void)
{
	const struct k_work_queue_config flow_wq_cfg = { .name = "flow_wq" };
	int rc;

	k_work_queue_start(&flow_wq, flow_wq_stack,
			   K_THREAD_STACK_SIZEOF(flow_wq_stack), FLOW_WQ_PRIO,
			   &flow_wq_cfg);
	k_work_init(&flow_work, flow_work_fn);

	rc = init_hw();
	if (rc != 0) {
		printk("hardware init failed (%d): threads and sampling tick not started\n", rc);
		return rc;
	}
	printk("SoilSense Control — kernel build (%s)\n", CONFIG_BOARD);
	printk("type 'help'\n");

	start_threads();

	k_timer_start(&tick_timer, K_USEC(SAMPLE_PERIOD_US),
		      K_USEC(SAMPLE_PERIOD_US));
	return 0;
}
