#!/usr/bin/env python3
"""Loudness pass: reads the test renderer's results.csv and writes gains.json (preset -> master volume dB)."""
import csv, json, math, sys, os
TARGET = {"Bass": -13.0, "Lead": -14.0, "Pad": -16.0, "Pluck": -16.0, "Keys": -16.0, "Chord": -15.0, "Arp": -15.0, "FX": -17.0, "Init": -16.0}
CEIL = 0.88          # just under the output soft-clip knee (0.9)
DEFAULT_VOL = -6.0
res, out = sys.argv[1], sys.argv[2]
prev = json.load(open(out)) if os.path.exists(out) else {}
gains = {}
for r in csv.DictReader(open(res)):
    cur = prev.get(r["name"], DEFAULT_VOL)
    lufs, peak = float(r["lufs"]), max(1e-4, float(r["peak"]))
    delta = TARGET[r["category"]] - lufs
    delta = min(delta, 20 * math.log10(CEIL / peak))
    gains[r["name"]] = round(max(-30.0, min(12.0, cur + delta)), 2)
json.dump(gains, open(out, "w"), indent=0)
print("wrote", len(gains), "gains")
