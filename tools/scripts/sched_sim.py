#!/usr/bin/env python3
"""Lab 06 — schedulability tools.

Two sub-commands, both pure Python 3 (standard library only):

  sim    Task A. Preemptive simulation of a periodic task set under RM and EDF
         (implicit deadlines D = T, synchronous release at t = 0). Reports the
         first deadline miss, per-task worst response time and a timeline.

  tests  Task B. Utilization U, Liu & Layland bound, hyperbolic bound and the
         EDF bound for a task set, plus the room left for extra tasks.

Examples:
  sched_sim.py sim
  sched_sim.py sim --tasks "1:5,3:6,4:15" --policy edf --timeline 30
  sched_sim.py tests --preset node
  sched_sim.py tests --preset node --extra "sensor_a:200:50000,sensor_b:350:100000"

Time units are whatever the task set uses (the firmware maps 1 unit -> 10 ms).
Nothing here is a hardware measurement: these are predictions to compare with
the analyzer captures.
"""

import argparse
import sys
from math import gcd

# Synthetic stand-in for the book's Fig. 4.13 set (the book's parameters are not
# in this repository): 3 tasks, U = 0.967, RM misses and EDF meets. Verified by
# `sim`; replace it with the book's numbers if they differ.
FIG413_STAND_IN = [(1, 5), (3, 6), (4, 15)]  # (C, T), 1 unit = 10 ms on the board

# Measured C_i in this repo: Lab 02, L476RG superloop, logic analyzer + PulseView.
# The ESP32-S3 values are not recorded yet; the ISR period is an ASSUMPTION.
NODE_PRESET_US = [
    ("sampling", 6.0, 1_000.0),
    ("control", 12.5, 10_000.0),
    ("telemetry", 45.0, 1_000_000.0),
    ("flow_isr", 4.2, 1_000.0),  # T = assumed minimum inter-arrival, not measured
]


def lcm(a, b):
    return a * b // gcd(a, b)


# ---------------------------------------------------------------- tests (Task B)

def utilization(tasks):
    return sum(c / t for _, c, t in tasks)


def liu_layland_bound(n):
    return n * (2 ** (1.0 / n) - 1)


def hyperbolic_product(tasks):
    prod = 1.0
    for _, c, t in tasks:
        prod *= c / t + 1.0
    return prod


def run_tests(tasks):
    n = len(tasks)
    u = utilization(tasks)
    bound = liu_layland_bound(n)
    hyp = hyperbolic_product(tasks)

    print(f"n = {n} tasks")
    print(f"{'task':<12}{'C':>12}{'T':>14}{'U_i':>12}")
    for name, c, t in tasks:
        print(f"{name:<12}{c:>12.3f}{t:>14.1f}{c / t:>12.6f}")
    print(f"\nU                    = {u:.6f}")
    print(f"Liu & Layland bound  = n(2^(1/n)-1) = {bound:.6f}   -> U <= bound: {u <= bound}")
    print(f"Hyperbolic product   = prod(U_i+1)  = {hyp:.6f}   -> <= 2:        {hyp <= 2.0}")
    print(f"EDF bound            = 1.000000                  -> U <= 1:      {u <= 1.0}")

    if u <= bound:
        verdict = "RM schedulable (Liu & Layland is sufficient and holds)"
    elif hyp <= 2.0:
        verdict = "RM schedulable (hyperbolic holds; Liu & Layland is too pessimistic here)"
    elif u <= 1.0:
        verdict = "inconclusive for RM (both sufficient tests fail) -> run RTA; EDF is schedulable"
    else:
        verdict = "NOT schedulable (U > 1)"
    print(f"Verdict: {verdict}")
    return u


def headroom(base, extra_count):
    """Largest extra utilization that keeps U under the L&L bound for n+k tasks."""
    n = len(base) + extra_count
    room = liu_layland_bound(n) - utilization(base)
    return n, liu_layland_bound(n), room


def parse_named(spec):
    tasks = []
    for item in spec.split(","):
        name, c, t = item.split(":")
        tasks.append((name, float(c), float(t)))
    return tasks


def cmd_tests(args):
    tasks = list(NODE_PRESET_US) if args.preset == "node" else []
    if args.tasks:
        tasks += parse_named(args.tasks)
    if not tasks:
        sys.exit("tests: give --preset node and/or --tasks name:C:T,...")

    print("== Base set ==")
    base_u = run_tests(tasks)

    if args.extra:
        extra = parse_named(args.extra)
        print("\n== Base set + extra tasks ==")
        run_tests(tasks + extra)
    else:
        extra_n = args.room
        n, bound, room = headroom(tasks, extra_n)
        print(f"\n== Room for {extra_n} more tasks (Liu & Layland, n = {n}) ==")
        print(f"bound(n={n}) = {bound:.6f}; U now = {base_u:.6f}; "
              f"extra utilization that still fits = {room:.6f}")


# ------------------------------------------------------------- simulation (Task A)

def simulate(tasks, policy, horizon):
    """Preemptive, 1 time unit per step. Stops at the first deadline miss.

    Returns (segments, first_miss, worst_response); segments are (start, end, idx).
    """
    n = len(tasks)
    remaining = [0] * n
    deadline = [0] * n
    release = [0] * n
    job_no = [0] * n
    worst = [0] * n
    segments = []
    miss = None

    for now in range(horizon):
        for i, (c, t) in enumerate(tasks):
            if now % t == 0:
                if remaining[i] > 0 and miss is None:
                    miss = (i, job_no[i], release[i], deadline[i], now)
                    return segments, miss, worst
                remaining[i] = c
                release[i] = now
                deadline[i] = now + t
                job_no[i] += 1

        ready = [i for i in range(n) if remaining[i] > 0]
        if not ready:
            continue
        if policy == "rm":
            run = min(ready, key=lambda i: (tasks[i][1], i))
        else:
            run = min(ready, key=lambda i: (deadline[i], i))

        remaining[run] -= 1
        if segments and segments[-1][2] == run and segments[-1][1] == now:
            segments[-1] = (segments[-1][0], now + 1, run)
        else:
            segments.append((now, now + 1, run))
        if remaining[run] == 0:
            worst[run] = max(worst[run], now + 1 - release[run])
    return segments, miss, worst


def print_timeline(tasks, segments, length):
    print("      " + "".join(str(t % 10) if t % 5 == 0 else " " for t in range(length)))
    for i in range(len(tasks)):
        row = ["."] * length
        for start, end, idx in segments:
            if idx == i:
                for t in range(start, min(end, length)):
                    row[t] = "#"
        print(f"tau{i + 1}  " + "".join(row))


def cmd_sim(args):
    if args.tasks:
        tasks = [tuple(int(x) for x in item.split(":")) for item in args.tasks.split(",")]
    else:
        tasks = FIG413_STAND_IN
    periods_lcm = 1
    for _, t in tasks:
        periods_lcm = lcm(periods_lcm, t)
    horizon = args.horizon or periods_lcm

    u = sum(c / t for c, t in tasks)
    print("task set (C, T):", tasks, f" U = {u:.4f}  hyperperiod = {periods_lcm}")
    for policy in (["rm", "edf"] if args.policy == "both" else [args.policy]):
        segments, miss, worst = simulate(tasks, policy, horizon)
        print(f"\n--- {policy.upper()} ---")
        if miss:
            i, job, rel, dl, now = miss
            print(f"DEADLINE MISS: tau{i + 1} job {job} released at t={rel}, "
                  f"deadline t={dl}, still has work left at t={now}")
        else:
            print(f"no miss over {horizon} units")
        print("worst response (completed jobs):",
              {f"tau{i + 1}": w for i, w in enumerate(worst)})
        if args.timeline:
            print_timeline(tasks, segments, min(args.timeline, horizon))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("sim", help="RM vs EDF simulation (Task A)")
    sp.add_argument("--tasks", help='"C:T,C:T,..." integer units (default: Fig. 4.13 stand-in)')
    sp.add_argument("--policy", choices=["rm", "edf", "both"], default="both")
    sp.add_argument("--horizon", type=int, help="units to simulate (default: hyperperiod)")
    sp.add_argument("--timeline", type=int, default=0, help="print a timeline of N units")
    sp.set_defaults(func=cmd_sim)

    tp = sub.add_parser("tests", help="utilization tests (Task B)")
    tp.add_argument("--preset", choices=["node"], help="Lab 02 measured C_i of the node")
    tp.add_argument("--tasks", help='extra base tasks "name:C:T,..." in the same unit')
    tp.add_argument("--extra", help='additional tasks to add and re-test "name:C:T,..."')
    tp.add_argument("--room", type=int, default=2, help="how many extra tasks to size room for")
    tp.set_defaults(func=cmd_tests)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
