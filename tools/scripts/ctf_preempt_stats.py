#!/usr/bin/env python3
"""Measure ISR -> thread latency and context-switch gaps from decoded CTF text.

A "tick wake-up" is a contiguous run of events:
  isr_enter, timer_expiry_enter, msgq_put_enter, thread_sched_ready(sampling),
  [msgq_put_exit, timer_expiry_exit, isr_exit*], thread_switched_out(X),
  thread_switched_in(sampling)
with no other thread_switched_* between isr_enter and the switch, and every
event inside one timestamp segment (no discontinuity marker in between).

usage: ctf_preempt_stats.py TRACE.txt [TRACE2.txt ...]   (EXAMPLE_X=idle to pick the printed example)
"""
import os
import re
import statistics as st
import sys
from collections import defaultdict

LINE = re.compile(r'^\[(\d+\.\d+)\] (\w+): \{ (.*) \}$')
NAME = re.compile(r'name = "([^"]*)"')
LOOKBACK = 14
MAX_SPAN_NS = 200_000  # a wake-up sequence is far shorter than 200 us


def load(path):
    segs, cur = [], []
    for line in open(path):
        line = line.rstrip("\n")
        if line.startswith("#"):
            if cur:
                segs.append(cur)
            cur = []
            continue
        m = LINE.match(line)
        if not m:
            continue
        ts = round(float(m.group(1)) * 1e9)
        nm = NAME.search(m.group(3))
        cur.append((ts, m.group(2), nm.group(1) if nm else None, m.group(3)))
    if cur:
        segs.append(cur)
    return segs


def wakeups(segs):
    for seg in segs:
        for i, (t3, ev, name, _) in enumerate(seg):
            if ev != "thread_switched_in" or name != "sampling" or i < 2:
                continue
            if seg[i - 1][1] != "thread_switched_out":
                continue
            x = seg[i - 1][2]
            t2 = seg[i - 1][0]
            j0 = None
            ready = None
            for j in range(i - 2, max(i - 2 - LOOKBACK, -1), -1):
                e = seg[j][1]
                if e in ("thread_switched_in", "thread_switched_out"):
                    break
                if e == "thread_sched_ready" and seg[j][2] == "sampling":
                    ready = seg[j][0]
                if e == "isr_enter":
                    j0 = j
                    break
            if j0 is None or ready is None or t3 - seg[j0][0] > MAX_SPAN_NS:
                continue
            isr_exit = next((seg[k][0] for k in range(j0 + 1, i - 1) if seg[k][1].startswith("isr_exit")), None)
            gpio = next((seg[k][0] for k in range(i + 1, min(i + 4, len(seg)))
                         if seg[k][1] == "gpio_port_set_bits_raw_enter" and "pins = 16 " in seg[k][3] + " "), None)
            yield {"x": x, "t0": seg[j0][0], "ready": ready, "t1": isr_exit, "t2": t2, "t3": t3, "tg": gpio,
                   "block": seg[j0:i + 2]}


def us(v):
    return v / 1000.0


def summarize(vals):
    if not vals:
        return "n=0"
    vals = sorted(vals)
    return (f"n={len(vals)} min={us(vals[0]):.2f} median={us(st.median(vals)):.2f} "
            f"p95={us(vals[int(0.95 * (len(vals) - 1))]):.2f} max={us(vals[-1]):.2f} us")


by_x = defaultdict(list)
for path in sys.argv[1:]:
    for w in wakeups(load(path)):
        by_x[w["x"]].append(w)

print("preempted thread X -> count")
for x, ws in sorted(by_x.items(), key=lambda kv: -len(kv[1])):
    print(f"\n== X = {x}  ({len(ws)} wake-ups)")
    print("  ISR->thread  (t3-t0):", summarize([w["t3"] - w["t0"] for w in ws]))
    print("  ready->in    (t3-ready):", summarize([w["t3"] - w["ready"] for w in ws]))
    print("  switch gap   (t3-t2):", summarize([w["t3"] - w["t2"] for w in ws]))
    t1s = [w["t1"] - w["t0"] for w in ws if w["t1"] is not None]
    print("  ISR duration (t1-t0):", summarize(t1s) if t1s else "no isr_exit event inside these sequences")
    tg = [w["tg"] - w["t3"] for w in ws if w["tg"] is not None]
    print("  in->GPIO4 set (tg-t3):", summarize(tg) if tg else "n=0")

EX_X = os.environ.get("EXAMPLE_X", "console")
ex = by_x.get(EX_X, [])
if ex:
    w = sorted(ex, key=lambda w: w["t3"] - w["t0"])[len(ex) // 2]
    print(f"\nmedian-latency example (X = {EX_X}):")
    base = w["t0"]
    print(f"  t0 absolute: {base / 1e9:.9f} s (CTF clock)")
    for ts, ev, nm, f in w["block"]:
        print(f"  +{us(ts - base):7.2f} us  {ev}" + (f"  [{nm}]" if nm else ""))
