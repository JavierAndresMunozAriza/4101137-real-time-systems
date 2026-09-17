# Week 4 — The full migration and the A/B
> **Reading:** [READINGS.md](../READINGS.md), week 4 · **Module:** 2

**From:** Eng. Samuel Cifuentes — *"Finish the migration and bring me the A/B: superloop vs. kernel, same tasks, same board, same conditions. That table decides the product architecture — and I'm the one defending it to Gustavo, so I want to be able to cite it without embarrassment. And one more thing: threads bring context switches, but they also bring stack overflows. Measure your high-water marks before we ship this."*

Today the talk's mapping is completed: every piece of the superloop finds its
kernel counterpart.

| Superloop piece | Kernel counterpart |
|---|---|
| ISR flag + check in the loop | Short ISR + `k_msgq` / `k_sem` |
| Heavy polled work | Thread with its own priority |
| Improvised "bottom half" in the loop | `k_work` on the workqueue |
| Console that blocks everything | Console thread at low priority |

| Stakeholder | Their question | How this session answers it |
|---|---|---|
| **Samuel** | Does the kernel improve the worst case, not just the average? | A/B with maxima, not averages |
| **Gustavo** | What did we buy with the new complexity? | The blocking command no longer breaks anything — measured |

## What you'll measure

| Measurement (S3) | Superloop (wk 3) | Kernel (today) |
|---|---|---|
| Max sampling jitter | (copy) | ____ µs |
| Max sampling jitter with blocking command | (copy) | ____ µs |
| ISR → thread latency (via msgq) | — | ____ µs |
| Overhead: visible context-switch width | — | ____ µs |
| Control loop thread stack high-water mark | — | ____ bytes / ____ allocated |


## Tasks

### Task A — Complete the migration
- Control → high-priority thread; console → low-priority thread; the flow ISR's
  deferred work → `k_work`. `main` ends up nearly empty.
- Keep the same instrumentation GPIOs (the comparison demands symmetry).
- **Evidence:** the full node running, thread tree (`kernel threads` in the shell or listed in code).

### Task B — The A/B
- Repeat the week-2 measurement protocol exactly (same duration, same conditions)
  on the kernel version. Fill in the table.
- **Evidence:** complete A/B table + captures of both conditions.

### Task C — The cost
- The kernel isn't free: measure the context switch (two consecutive toggles from
  different threads) and estimate the overhead per second at your current load.
- **Evidence:** the number + the one-line calculation.

### Task D — Memory Safety (Stack High-Water Mark)
- Add `CONFIG_THREAD_ANALYZER=y` and `CONFIG_INIT_STACKS=y` to your `prj.conf`. 
- Run the node under load. Use the shell (`thread analyze`) or call `thread_analyzer_print()` to view how much of each thread's allocated stack was actually used.
- **Evidence:** Terminal output showing the thread stack high-water marks, proving you aren't nearing an overflow.

## What about FreeRTOS?

The mapping is 1:1: `k_msgq`→`xQueue`, `k_work`→timer daemon or a dedicated task, same preemptive priorities. To measure stacks, FreeRTOS provides `uxTaskGetStackHighWaterMark()`. The practical difference is in the defaults: FreeRTOS boots with fewer services (smaller footprint — its strength on small chips); Zephyr ships console/log/shell ready (its strength in larger products).

## Deliverables (RET)

- **§2 ADR-001 — Node architecture: multithreaded kernel.** Context (baseline +
  blocking command), decision, justification **citing the A/B table**, status.
- **§3 Week-4 evidence:** A/B table + measured overhead + stack high-water marks.

## Rubric (100 pts)

| | pts |
|---|---|
| **Execution** — migration complete per the mapping (20) · symmetric instrumentation (10) · thread analyzer running (10) | 40 |
| **Evidence** — A/B with a protocol identical to week 2's (20) · overhead & stack measured (10) | 30 |
| **Analysis** — ADR-001 citing numbers, with the cost acknowledged (not just the benefit) (30) | 30 |
