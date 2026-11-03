#!/usr/bin/env python3
"""Lab 06 Task C — predicted loop error vs. injected actuation jitter.

Mirrors firmware/kernel/src/main.c bit for bit where it matters:
  * synthetic plant      mv += (duty*33 - mv)/64  (C integer division) + noise
  * sampling tick        every 1 ms, plant read once per tick
  * control law          every 10th tick: PI with I += e/8 (|I| <= 2000),
                         duty = clamp((e + I)/20, 0, 100)
  * artificial delay     CONFIG_LAB06_JITTER_MAX_MS: before writing the duty the
                         control thread sleeps a whole number of ms drawn from the
                         same fixed-seed xorshift32, uniform in [0, N]
  * control backlog      the release semaphore counts up to 10 pending jobs

The board's own noise term is `k_cycle_get_32() % 7 - 3`; it is modelled here as
uniform in [-3, 3] mV from a seeded generator. The result is a PREDICTION to be
compared with the `loop_err_mv` the firmware prints in `status` after
`set <mv>`; it is not a hardware measurement.

Usage:
  loop_jitter_sim.py                 # N = 0, 2, 5 (the lab's table)
  loop_jitter_sim.py --sweep         # N = 0, 2, 5, 10, 20, 30, 40, 50
  loop_jitter_sim.py --step 2000 --window-s 10
"""

import argparse
import random

SETPOINT_START_MV = 1500
PLANT_START_MV = 1200
PLANT_GAIN = 33
PLANT_LAG_DIV = 64
PLANT_NOISE = 3
CONTROL_EVERY = 10
PI_KI_DIV = 8
PI_I_LIMIT = 2000
PI_OUT_DIV = 20
PCT_MAX = 100
SEM_LIMIT = CONTROL_EVERY
JITTER_SEED = 2463534242
SETTLE_S = 8  # seconds at the initial setpoint before the step


def cdiv(a, b):
    """C integer division: truncates toward zero."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def clamp(x, lo, hi):
    return max(lo, min(hi, x))


class Xorshift32:
    def __init__(self, seed):
        self.x = seed & 0xFFFFFFFF

    def next(self):
        x = self.x
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.x = x
        return x


def run(jitter_max_ms, step_mv, window_s, noise_seed=1):
    """Return (mean |e| over the window, mean |e| over the last half, duty std)."""
    noise = random.Random(noise_seed)
    rng = Xorshift32(JITTER_SEED)
    mv = PLANT_START_MV
    duty = 0
    integral = 0
    setpoint = SETPOINT_START_MV
    pressure = mv

    pending = 0          # control releases waiting for the control thread
    busy_until = None    # ms at which the in-flight job writes its duty
    new_duty = 0
    ticks = 0

    err_sum = 0
    err_cycles = 0
    half = []
    duties = []

    total_ms = (SETTLE_S + window_s) * 1000
    step_ms = SETTLE_S * 1000
    for ms in range(total_ms):
        if ms == step_ms:
            setpoint = step_mv
            err_sum = 0
            err_cycles = 0

        # sampling thread (priority 2): plant read, then control release
        mv += cdiv(duty * PLANT_GAIN - mv, PLANT_LAG_DIV)
        mv += noise.randint(-PLANT_NOISE, PLANT_NOISE)
        pressure = mv
        ticks += 1
        if ticks >= CONTROL_EVERY:
            ticks = 0
            pending = min(pending + 1, SEM_LIMIT)

        # control thread (priority 3): runs after sampling within the same ms
        while True:
            if busy_until is not None and ms >= busy_until:
                duty = new_duty
                busy_until = None
            if busy_until is None and pending > 0:
                pending -= 1
                error = setpoint - pressure
                integral = clamp(integral + cdiv(error, PI_KI_DIV), -PI_I_LIMIT, PI_I_LIMIT)
                new_duty = clamp(cdiv(error + integral, PI_OUT_DIV), 0, PCT_MAX)
                late = rng.next() % (jitter_max_ms + 1) if jitter_max_ms > 0 else 0
                busy_until = ms + late
                if ms >= step_ms:
                    err_sum += abs(error)
                    err_cycles += 1
                    if ms >= step_ms + (total_ms - step_ms) // 2:
                        half.append(abs(error))
                    duties.append(new_duty)
                continue
            break

    mean_all = err_sum / err_cycles if err_cycles else 0.0
    mean_half = sum(half) / len(half) if half else 0.0
    mu = sum(duties) / len(duties) if duties else 0.0
    std = (sum((d - mu) ** 2 for d in duties) / len(duties)) ** 0.5 if duties else 0.0
    return mean_all, mean_half, std


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sweep", action="store_true", help="N = 0,2,5,10,20,30,40,50")
    ap.add_argument("--step", type=int, default=2000, help="setpoint step target, mV")
    ap.add_argument("--window-s", type=int, default=10, help="measurement window, s")
    args = ap.parse_args()

    levels = [0, 2, 5, 10, 20, 30, 40, 50] if args.sweep else [0, 2, 5]
    print(f"step {SETPOINT_START_MV} -> {args.step} mV, window {args.window_s} s "
          f"(settled {SETTLE_S} s before the step)")
    print(f"{'N [ms]':>7}{'mean|e| [mV]':>15}{'last-half [mV]':>16}{'duty std [%]':>14}")
    base = None
    for n in levels:
        mean_all, mean_half, std = run(n, args.step, args.window_s)
        base = mean_all if base is None else base
        print(f"{n:>7}{mean_all:>15.1f}{mean_half:>16.1f}{std:>14.2f}   x{mean_all / base:.2f} vs N=0")


if __name__ == "__main__":
    main()
