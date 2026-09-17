# Week 10 — PREEMPT_RT: Linux enters the loop
> **Tech guide:** [SOP-10](sops/sop10_preempt_rt.md) · **Reading:** [READINGS.md](../READINGS.md), week 10 · **Module:** 5
> **Starting today:** larger groups (two pairs), one SBC per group. The Hub Kit is needed this week ([BOM](../BOM.md)).

**From:** Eng. Samuel Cifuentes — *"The Hub runs Linux: GUI, routing, cloud — you
don't do that on an MCU. But the Hub also controls the pump station, and there
Linux has a well-earned reputation for tardiness. That reputation is done being
argued with opinions: install the PREEMPT_RT kernel and bring me the latency
distribution — stock vs. RT, unloaded and under load. That curve decides whether
the Hub can hold hard duties."*

| Stakeholder | Their question | How this session answers it |
|---|---|---|
| **Samuel** | What's the worst-case wake-up on this SBC? | `cyclictest` histogram, max under load |
| **Gustavo** | Do we need an extra MCU in the Hub, or is RT Linux enough? | The histogram's tail answers |

## What you'll measure

| Measurement (`cyclictest`, ≥ 5 min each) | Max (µs) | p99 (µs) |
|---|---|---|
| Stock kernel, no load | ____ | ____ |
| Stock kernel, **under load** (`stress-ng` + traffic) | ____ | ____ |
| PREEMPT_RT kernel, no load | ____ | ____ |
| PREEMPT_RT kernel, under load | ____ | ____ |
| PREEMPT_RT, under load, **powersave governor** (DFS penalty) | ____ | ____ |

## Tasks

### Task A — SBC bring-up
- Image with a PREEMPT_RT kernel on the group's SBC (SOP-10: course-prepared image or the distro's RT kernel). Verify: `uname -v` must say `PREEMPT_RT`.
- **Evidence:** `uname -a` output + the group's SSH access.

### Task B — The curve that decides
- **Thermal & DFS Protocol:** Dynamic Frequency Scaling (DFS) ruins real-time guarantees. Before your official runs, lock the CPU to maximum frequency:
  `echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`
- `cyclictest -m -Sp90 -i 1000 -h 400 -D 5m` under the table's first four conditions.
  Load: `stress-ng --cpu 0 --io 2` + a `ping -f` from a laptop (real traffic).
- Plot the four histograms (the SOP ships the plotting script).
- **Evidence:** the four histograms + the filled table.

### Task C — The DFS Penalty
- Change the governor to `powersave` (or `ondemand`) and run one more 5-minute `cyclictest` under load. 
- Watch how CPU throttling destroys your tail latency. Log the max in row 5.
- **Evidence:** The row 5 numbers proving why production servers lock their governors.

### Task D — Reading the tail
- In the RET: where does PREEMPT_RT's max under load sit vs. the pump loop's 10 ms deadline? And vs. the MCU's ~µs (week 9)? Three sentences: what this SBC can and cannot promise.
- **Evidence:** the paragraph, citing the table's numbers.

## What about FreeRTOS?

Change of scale, same question. On the MCU the worst case came from the small kernel (FreeRTOS or Zephyr: ~µs); on Linux it comes from the giant kernel (~tens–hundreds of µs even with RT). MCUs rarely use complex dynamic frequency scaling (DFS)—they run at a fixed clock. The Hub's architecture — what lives in Linux and what would force a return to an MCU — is exactly that comparison, and it's the final project's central ADR.

## Deliverables (RET)

- **§3 Week-10 evidence:** table + histograms for the four conditions.
- **§1:** new task-set section: the Hub's tasks (pumps, interlock, GUI, routing)
  with their types and deadlines — `C_i` pending.

## Rubric (100 pts)

| | pts |
|---|---|
| **Execution** — RT kernel verified (10) · runs with constant protocol & performance governor (20) · DFS penalty captured (10) | 40 |
| **Evidence** — readable histograms + complete table (30) | 30 |
| **Analysis** — tail read against the Hub's deadlines and against the MCU (30) | 30 |
