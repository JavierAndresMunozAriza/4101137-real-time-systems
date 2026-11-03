#!/usr/bin/env python3
"""Lab 07 — response-time analysis (RTA), Buttazzo Fig. 4.17.

For a fixed-priority task set, the worst-case response time of task i is the
fixed point of

    R_i = C_i + B_i + sum over higher-priority h of ceil(R_i / T_h) * C_h

where B_i is the longest time task i can be blocked by a LOWER-priority task
holding a shared lock (0 when there is no shared lock). The iteration starts at
R_i = C_i + B_i and stops at a fixed point (R_i is the bound) or when R_i > D_i
(the task is not schedulable).

Tasks are given in DECREASING priority order, as name:C:T[:D[:B]] (same unit for
all numbers; D defaults to T, B to 0).

Presets (pure data, no hardware involved):
  node             Lab 02 measured C_i of the node (us) + the e-stop deadline
  lab07-nolock     control / medium / slow, control never takes the lock
  lab07-pi         the same set with a PI mutex: B_control = longest critical section
  lab07-inversion  the same set with a bare semaphore: the medium task also delays
                   control, so it is modelled as extra interference (priority inversion)

Examples:
  rta.py --preset node
  rta.py --preset lab07-pi --trace
  rta.py --tasks "ctrl:1:100:5:4,med:10:100,slow:4:100" --unit ms
"""

import argparse
import math
import sys

# Lab 07 scenario (ms). Mirrors firmware/sched_labs/src/lab07.c.
CONTROL_C = 1.0    # control job, entirely inside the lock when it uses one
MEDIUM_C = 10.0    # medium job, never takes the lock
SLOW_CS = 4.0      # slow job = its critical section
EPISODE = 100.0    # every task is released once per episode
CONTROL_D = 5.0    # the e-stop deadline
CONTROL_OFFSET = 1.0  # control is released this long after the slow task took the lock

# Lab 02 measured values, L476RG superloop, logic analyzer + PulseView (us).
# The ISR period is an ASSUMPTION (not measured); the e-stop deadline is the course's.
NODE_US = [
    ("flow_isr", 4.2, 1_000.0, 1_000.0, 0.0),
    ("sampling", 6.0, 1_000.0, 1_000.0, 0.0),
    ("control", 12.5, 10_000.0, 5_000.0, 0.0),
    ("telemetry", 45.0, 1_000_000.0, 1_000_000.0, 0.0),
]

PRESETS = {
    "node": ("us", NODE_US),
    "lab07-nolock": ("ms", [
        ("control", CONTROL_C, EPISODE, CONTROL_D, 0.0),
        ("medium", MEDIUM_C, EPISODE, EPISODE, 0.0),
        ("slow", SLOW_CS, EPISODE, EPISODE, 0.0),
    ]),
    "lab07-pi": ("ms", [
        ("control", CONTROL_C, EPISODE, CONTROL_D, SLOW_CS),
        ("medium", MEDIUM_C, EPISODE, EPISODE, 0.0),
        ("slow", SLOW_CS, EPISODE, EPISODE, 0.0),
    ]),
    # Inversion: while the slow task holds the lock, the medium task runs ahead of it,
    # so control is delayed by B (slow's critical section) AND by the medium job. The
    # medium task behaves as if it had a priority above control for that interval.
    "lab07-inversion": ("ms", [
        ("control", CONTROL_C + MEDIUM_C, EPISODE, CONTROL_D, SLOW_CS),
        ("medium", MEDIUM_C, EPISODE, EPISODE, 0.0),
        ("slow", SLOW_CS, EPISODE, EPISODE, 0.0),
    ]),
}


def rta(tasks, trace=False):
    """Return a list of (name, C, B, R or None, D, ok)."""
    results = []
    for i, (name, c, t, d, b) in enumerate(tasks):
        higher = tasks[:i]
        r = c + b
        steps = [r]
        while True:
            nxt = c + b + sum(math.ceil(r / th) * ch for _, ch, th, _, _ in higher)
            steps.append(nxt)
            if nxt == r:
                break
            if nxt > d:
                r = nxt
                break
            r = nxt
        ok = r <= d
        if trace:
            print(f"  {name}: iterations R = {' -> '.join(f'{s:g}' for s in steps)}")
        results.append((name, c, b, r, d, ok))
    return results


def parse_tasks(spec):
    tasks = []
    for item in spec.split(","):
        parts = item.split(":")
        if not 3 <= len(parts) <= 5:
            sys.exit(f"bad task '{item}': expected name:C:T[:D[:B]]")
        name = parts[0]
        c, t = float(parts[1]), float(parts[2])
        d = float(parts[3]) if len(parts) > 3 else t
        b = float(parts[4]) if len(parts) > 4 else 0.0
        tasks.append((name, c, t, d, b))
    return tasks


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preset", choices=sorted(PRESETS))
    ap.add_argument("--tasks", help='"name:C:T[:D[:B]],..." in decreasing priority order')
    ap.add_argument("--unit", default="ms", help="unit label for --tasks (default ms)")
    ap.add_argument("--trace", action="store_true", help="show the fixed-point iterations")
    args = ap.parse_args()

    if args.preset:
        unit, tasks = PRESETS[args.preset]
    elif args.tasks:
        unit, tasks = args.unit, parse_tasks(args.tasks)
    else:
        sys.exit("give --preset or --tasks")

    print(f"unit: {unit}   (tasks in decreasing priority order)")
    results = rta(tasks, trace=args.trace)
    print(f"{'task':<11}{'C':>10}{'B':>10}{'R':>12}{'D':>12}  schedulable")
    for name, c, b, r, d, ok in results:
        print(f"{name:<11}{c:>10g}{b:>10g}{r:>12g}{d:>12g}  {'yes' if ok else 'NO'}")
    sys.exit(0 if all(r[5] for r in results) else 1)


if __name__ == "__main__":
    main()
