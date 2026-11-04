#!/usr/bin/env python3
"""Lab 06, Task C: e-stop detection -> valve low, from a PulseView session (.sr).

D5 is the detection marker (instr_estop, high while the e-stop is latched) and D4 is the
valve. For every rising edge of D5 the script reports the next falling edge of D4.

usage: estop_latency.py SESSION.sr
"""
import bisect
import sys
import zipfile

DETECT_BIT, VALVE_BIT = 5, 4


def load(path):
    z = zipfile.ZipFile(path)
    names = sorted((n for n in z.namelist() if n.startswith("logic-1-")), key=lambda n: int(n.split("-")[-1]))
    rate = 0
    for line in z.read("metadata").decode().splitlines():
        if line.startswith("samplerate="):
            value, unit = line.split("=")[1].split()
            rate = int(float(value) * {"Hz": 1, "kHz": 1e3, "MHz": 1e6}[unit])
    return b"".join(z.read(n) for n in names), rate


def edges(bits, pattern):
    out, i = [], bits.find(pattern)
    while i != -1:
        out.append(i + 1)
        i = bits.find(pattern, i + 1)
    return out


def main(path):
    data, rate = load(path)
    detect = data.translate(bytes((b >> DETECT_BIT) & 1 for b in range(256)))
    valve = data.translate(bytes((b >> VALVE_BIT) & 1 for b in range(256)))
    rises = edges(detect, b"\x00\x01")
    valve_falls = edges(valve, b"\x01\x00")
    print(f"{len(data)} samples at {rate / 1e6:.0f} MHz ({1e6 / rate:.3f} us per sample)")
    deltas = []
    for r in rises:
        k = bisect.bisect_right(valve_falls, r)
        if k == len(valve_falls):
            print(f"trip at {r / rate * 1e3:.3f} ms: no valve falling edge afterwards")
            continue
        delta_us = (valve_falls[k] - r) / rate * 1e6
        deltas.append(delta_us)
        print(f"trip at {r / rate * 1e3:10.3f} ms: valve high at detection = {valve[r - 1] == 1}; "
              f"detection -> valve low = {delta_us:10.1f} us")
    if deltas:
        print(f"max = {max(deltas):.1f} us, min = {min(deltas):.1f} us, n = {len(deltas)}")


if __name__ == "__main__":
    main(sys.argv[1])
