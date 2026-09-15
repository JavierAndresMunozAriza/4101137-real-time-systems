# Week 2 — The provided superloop: run, read, measure
> **Reading:** [READINGS.md](../READINGS.md), week 2 · **Module:** 1
> **Firmware:** [firmware/superloop/](../firmware/superloop/) (provided — you complete three marked pieces; nothing is invented today)

**From:** Eng. Samuel Cifuentes — *"Before you pitch me a kernel, measure what we
already have. I'm handing you the Control node as a superloop — the architecture
this company knows how to maintain. I want the baseline:
latency and jitter for every task, with numbers, not adjectives. If the superloop
is enough, we keep it."*

The firmware already ships with **GPIO instrumentation** (a toggle on entry/exit of
each task): your instrument is the logic analyzer.

| Stakeholder | Their question | How this session answers it |
|---|---|---|
| **Samuel** | Does the superloop meet the scenario's task set? | Per-task jitter table vs. its deadlines |
| **Gustavo** | Why would we pay for a kernel's complexity? | The blocking command: the degradation, measured |

## What you'll measure

Every pair measures the same board, so today's numbers are comparable across the
room — and week 3 re-measures all of them on the ESP32-S3.

| Measurement | Your value | Reference |
|---|---|---|
| Actual sampling period (nominal 1 kHz): average | ____ µs | 1000 µs |
| Sampling jitter: max over ≥ 30 s | ____ µs | write it down: it's *the course baseline* |
| ISR → loop-service latency (flow pulse) | ____ µs | |
| Sampling jitter, max, **with the flash cache off** | ____ µs | the gap against row 2 is the ART accelerator |
| Sampling jitter **with the blocking command active** | ____ µs | compare against row 2 |
| `backlog_peak`, idle → during the blocking command | ____ → ____ ticks | the same story, counted by the firmware itself |

## Tasks

### Task A — Finish the superloop and run it

The node arrives almost complete: the five tasks, the PI controller, the console
and your board's pin map are all there. Three pieces are missing, and they are
precisely the three that make this a *superloop* instead of a pile of functions.
Each sits behind a `TASK` comment in `firmware/superloop/src/main.c`.

**TASK 1 — the instrument.** Every task raises a GPIO on entry and drops it on
exit, but the helper that does it is empty, so the analyzer would show six flat
lines. Replace the `TASK 1` comment with:

```c
	if (p->port) {
		gpio_pin_set_dt(p, v);
	}
```

**TASK 2 — the hand-off.** The 1 kHz timer ISR does *not* sample. It raises a
flag and returns; `backlog_peak` remembers the worst pile-up, which is the number
you read in Task B. Replace the `TASK 2` comment with:

```c
	atomic_val_t backlog = atomic_inc(&ticks_pending) + 1;

	if (backlog > atomic_get(&backlog_peak)) {
		atomic_set(&backlog_peak, backlog);
	}
```

**TASK 3 — the loop.** Build the project before you do this one and read the
warnings: `task_console`, `task_display`, `task_telemetry`, `task_sampling`,
`task_control` and `task_flow_batch` are all *defined but not used*. Nothing
calls them, because the thing whose whole job is to call them is missing. That
warning list is the superloop's job description.

No scheduler: one `while (1)` visiting every task in a fixed order, draining one
pending tick per pass. Replace the `TASK 3` comment with:

```c
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
```

Then build and flash:

```bash
west build -p -b nucleo_l476rg firmware/superloop && west flash
```

The console is the ST-LINK virtual COM port, 115200 8N1. `help` should answer and
`status` should report a pressure that moves. If the banner prints but `help`
never answers, TASK 3 is missing: `main()` ran off the end and there is no loop.

- Read the rest of `main.c` with your partner: identify the ISR flags, the polled
  work, and where each instrumentation GPIO toggles. Draw the flow in ≤ 10 boxes.
- **Evidence:** the diagram (a photo of paper is fine) with the instrumentation GPIOs marked.

### Task B — The baseline

![Lab 02 hardware block diagram](img/lab02_hw.svg)

- Hook the logic analyzer to the instrumentation GPIOs: Arduino **D3–D8**, in the
  task order of the table in
  [firmware/superloop/README.md](../firmware/superloop/README.md), with the flow
  input on D9. Capture ≥ 30 s.
- Fill in the first three rows of the table. Use the pulse statistics in the
  analyzer software (period/width min–max).
- Rebuild with the flash accelerator disabled and capture once more:

  ```bash
  west build -p -b nucleo_l476rg firmware/superloop -- -DEXTRA_CONF_FILE=nocache.conf
  ```

  Same source, same pins, same clock — the only change is that the L4 stops
  caching and prefetching instructions out of flash. Fill the cache-off row. The
  gap against row 2 is work the silicon was doing for you for free, and it is the
  first two links of the latency chain made visible on their own.
- Then type `status` and record `backlog_peak`: the number of 1 kHz ticks that
  piled up because the loop was busy elsewhere. It is the same delay the analyzer
  just showed you, counted in ticks instead of microseconds — the two readings
  have to tell the same story.
- **Evidence:** analyzer screenshots + filled table in the RET.

### Task C — Break it on purpose
- Trigger the console's **blocking command** (`calib`) while capturing.
- Fill in rows 4 and 5: the analyzer gives you the microseconds, `status` gives
  you the ticks. Which task suffers, and why? Explain in 2–3 sentences pointing
  at the responsible line of code.
- **Evidence:** before/after capture + the explanation in the RET.

### Task D — The requirements, in writing
- From the task table in the [scenario](../PROJECT_SCENARIO.md), extract **5–8
  timing requirements** and write each as one **EARS-style** sentence
  (*when/while `<condition>`, the system shall `<response>` within `<deadline>`*), with an ID:

| ID | Requirement |
|---|---|
| REQ-CTRL-01 | While the system is irrigating, the control loop shall run every 10 ms (deadline = period). |
| REQ-CTRL-02 | When pressure exceeds the threshold, the system shall close the valve within 5 ms. |

- They go in **RET §1**, and from today on every piece of evidence cites the REQ it
  verifies (e.g. "row 2 of the table verifies REQ-CTRL-03"). In week 8 you'll learn
  what industry calls this discipline.
- **Evidence:** the requirements table in RET §1.

## What about FreeRTOS?

Same as in Zephyr: many FreeRTOS products *also* start as superloop + ISRs, and
migrate to the kernel when exactly what you saw in Task C shows up — one task
blocking the rest. Next week's question is the same in both worlds: what does a
preemptive scheduler buy you?

## Deliverables (RET)

- **§1 Task set:** the requirements table (Task D) + first column of measured
  `C_i` (each task's pulse width).
- **§3 Week-2 evidence:** the table above, filled in, with a one-sentence reading.
- **§2 (not yet):** the superloop-vs-kernel decision is made in week 4, with the A/B.

## Rubric (100 pts)

| | pts |
|---|---|
| **Execution** — firmware running (10) · correct flow diagram (15) · blocking command reproduced (15) | 40 |
| **Evidence** — complete table with captures (20) · measurement conditions noted (duration, load) (10) | 30 |
| **Analysis** — root-cause explanation of the degradation, with the line of code (15) · baseline reading in one sentence (5) · well-formed EARS requirements, with verifiable condition and deadline (10) | 30 |
