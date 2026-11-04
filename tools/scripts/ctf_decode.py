#!/usr/bin/env python3
"""Minimal Zephyr CTF decoder (stand-in for babeltrace2 2.1.2, which aborts on
this metadata with a library assertion).

Reads event layouts from Zephyr's tsdl/metadata, decodes a raw channel0_0
stream (packed little-endian events: u64 timestamp [ns], u16 id, fields) and
prints one line per event. Resynchronises if bytes were lost. Summary on stderr.

usage: ctf_decode.py METADATA STREAM > trace.txt
"""
import re
import struct
import sys

META, STREAM = sys.argv[1], sys.argv[2]
SIZES = {"uint8_t": 1, "int8_t": 1, "uint16_t": 2, "uint32_t": 4, "int32_t": 4, "uint64_t": 8}
FMT = {"uint8_t": "<B", "int8_t": "<b", "uint16_t": "<H", "uint32_t": "<I", "int32_t": "<i", "uint64_t": "<Q"}
HDR = 10
MAX_GAP_NS = 2_000_000_000

text = open(META).read()
events = {}
for m in re.finditer(r"\nevent\s*\{(.*?)\n\};", text, re.S):
    body = m.group(1)
    name = re.search(r"name\s*=\s*(\w+);", body).group(1)
    eid = int(re.search(r"\bid\s*=\s*(0x[0-9a-fA-F]+|\d+);", body).group(1), 0)
    fields, size = [], 0
    fm = re.search(r"fields\s*:=\s*struct\s*\{(.*?)\n\s*\};", body, re.S)
    if fm:
        for line in fm.group(1).splitlines():
            line = line.strip()
            if not line:
                continue
            f = re.match(r"(\w+)\s+(\w+)(?:\[(\d+)\])?\s*;", line)
            if not f:
                raise SystemExit(f"cannot parse field line in {name}: {line!r}")
            ftype, fname, flen = f.group(1), f.group(2), f.group(3)
            if ftype == "ctf_bounded_string_t":
                n = int(flen)
                fields.append((fname, "str", n))
                size += n
            else:
                fields.append((fname, ftype, SIZES[ftype]))
                size += SIZES[ftype]
    events[eid] = {"name": name, "fields": fields, "size": size}

data = open(STREAM, "rb").read()


def event_at(pos):
    if pos + HDR > len(data):
        return None
    ts, eid = struct.unpack_from("<QH", data, pos)
    ev = events.get(eid)
    if ev is None or pos + HDR + ev["size"] > len(data):
        return None
    return ts, ev, pos + HDR + ev["size"]


def plausible_run(pos, n, prev):
    p = pos
    for _ in range(n):
        r = event_at(p)
        if r is None:
            return False
        ts, _, p = r
        if prev is not None and (ts < prev or ts - prev > MAX_GAP_NS):
            return False
        prev = ts
    return True


start = next((o for o in range(0, 4096) if plausible_run(o, 50, None)), None)
if start is None:
    raise SystemExit("no plausible event alignment found in the first 4096 bytes")

out, resyncs, discont = [], [], []
pos, last = start, None
while pos < len(data):
    r = event_at(pos)
    if r and last is not None and not (last <= r[0] < last + MAX_GAP_NS) and plausible_run(pos, 30, None):
        # well-formed events follow, but the timestamp jumped (clock restart/wrap)
        discont.append((pos, last, r[0]))
        out.append(f"# --- timestamp discontinuity at offset {pos}: {last / 1e9:.9f} -> {r[0] / 1e9:.9f} s ---")
        last = None
        continue
    if r and (last is None or (r[0] >= last and r[0] - last < MAX_GAP_NS)):
        ts, ev, nxt = r
        p, parts = pos + HDR, []
        for fname, ftype, flen in ev["fields"]:
            if ftype == "str":
                s = data[p:p + flen].split(b"\0", 1)[0].decode("ascii", "replace")
                parts.append(f'{fname} = "{s}"')
            else:
                parts.append(f"{fname} = {struct.unpack_from(FMT[ftype], data, p)[0]}")
            p += flen
        out.append(f"[{ts / 1e9:.9f}] {ev['name']}: {{ {', '.join(parts)} }}")
        last, pos = ts, nxt
    else:
        lost_from = pos
        pos += 1
        while pos < len(data) and not plausible_run(pos, 30, None):
            pos += 1
        resyncs.append((lost_from, pos - lost_from))
        out.append(f"# --- {pos - lost_from} bytes skipped at offset {lost_from} (stream resynchronised) ---")
        last = None

sys.stdout.write("\n".join(out) + "\n")
lost = sum(n for _, n in resyncs)
n_events = sum(1 for line in out if not line.startswith("#"))
sys.stderr.write(
    f"events: {n_events} | start offset: {start} | resyncs: {len(resyncs)} | "
    f"bytes skipped: {lost} of {len(data)} | timestamp discontinuities: {len(discont)} | "
    f"event types in metadata: {len(events)}\n"
)
for pos_, a, b in discont:
    sys.stderr.write(f"discontinuity at offset {pos_}: {a / 1e9:.6f} s -> {b / 1e9:.6f} s\n")
if resyncs:
    sys.stderr.write("resync (offset, bytes): " + ", ".join(f"({a},{b})" for a, b in resyncs[:10]) + "\n")
