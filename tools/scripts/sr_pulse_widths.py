#!/usr/bin/env python3
"""Pulse widths per analyzer channel from a PulseView session (.sr): the measured C_i.

On the ESP32-S3 node, D0 = sampling, D1 = control, D2 = console, D3 = telemetry.

usage: sr_pulse_widths.py SESSION.sr
"""
import statistics
import sys
import zipfile

NAMES = ("sampling D0", "control  D1", "console  D2", "telemetry D3")


def load(path):
    z = zipfile.ZipFile(path)
    names = sorted((n for n in z.namelist() if n.startswith("logic-1-")), key=lambda n: int(n.split("-")[-1]))
    rate = 0
    for line in z.read("metadata").decode().splitlines():
        if line.startswith("samplerate="):
            value, unit = line.split("=")[1].split()
            rate = int(float(value) * {"Hz": 1, "kHz": 1e3, "MHz": 1e6}[unit])
    return b"".join(z.read(n) for n in names), rate


def widths_us(data, bit, rate):
    levels = data.translate(bytes((b >> bit) & 1 for b in range(256)))
    out, i = [], levels.find(b"\x00\x01")
    while i != -1:
        rise = i + 1
        fall = levels.find(b"\x01\x00", rise)
        if fall == -1:
            break
        out.append((fall + 1 - rise) / rate * 1e6)
        i = levels.find(b"\x00\x01", fall)
    return out


def main(path):
    data, rate = load(path)
    print(f"resolution {1e6 / rate:.3f} us per sample")
    for bit, name in enumerate(NAMES):
        w = widths_us(data, bit, rate)
        if not w:
            print(f"{name}: no pulses")
            continue
        p95 = sorted(w)[int(0.95 * (len(w) - 1))]
        print(f"{name}: n={len(w):6d} median={statistics.median(w):9.1f} us  p95={p95:9.1f}  "
              f"max={max(w):9.1f}  min={min(w):8.1f}")


if __name__ == "__main__":
    main(sys.argv[1])
