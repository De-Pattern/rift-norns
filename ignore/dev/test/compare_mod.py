"""Hold the C++ port to live.py under per-block modulation and steals (what a sequencer,
p-locks, LFOs and knob turns do): random patches, every control moving now and then, stages
switching on and off mid-note, retriggers on a sounding voice.

    PYTHONPATH=../DN2-Rift/src python3 ignore/dev/test/compare_mod.py ignore/dev/build/drive [runs] [blocks]
"""
import random
import subprocess
import sys

import numpy as np

from dnfw.fascia import live as L
from dnfw.fascia import model as M

NAMES = list(M.INTERNAL)


def run(exe, seed, nblocks):
    rnd = random.Random(seed)
    q = {k: rnd.randrange(128) for k in NAMES}
    # keep the long, slow parts of the space likely: a few stages on, some off
    for k in ("SHIFT", "RING", "FORM", "WATR", "FOLD", "CHAR", "BUZZ", "FDBK", "ATK", "CLIK", "TZFM"):
        if rnd.random() < 0.5:
            q[k] = 64 if k == "SHIFT" else 0
    vseed = rnd.randrange(1, 1000)
    v = L.Voice(seed=vseed)
    lines = [f"V {vseed}"]
    ref = []
    note = float(rnd.randrange(36, 84))
    vel = round(rnd.random(), 3)
    v.note_on(note, vel)
    lines.append(f"N {note!r} {vel!r}")
    for b in range(nblocks):
        if rnd.random() < 0.15:                       # a p-lock / knob move
            for _ in range(rnd.randrange(1, 4)):
                k = rnd.choice(NAMES)
                q[k] = rnd.randrange(128)
        if rnd.random() < 0.06:                       # a stage switched on or off
            k = rnd.choice(("SHIFT", "RING", "FORM", "WATR", "FOLD", "CHAR", "LPG"))
            q[k] = (64 if k == "SHIFT" else 0) if rnd.random() < 0.5 else rnd.randrange(1, 128)
        if rnd.random() < 0.01:                       # a retrigger (a steal if it sounds)
            note = float(rnd.randrange(36, 84))
            vel = round(rnd.random(), 3)
            v.note_on(note, vel)
            lines.append(f"N {note!r} {vel!r}")
        ctl = {**q, "_internal": True}
        ref.append(v.block(ctl))
        lines.append("B " + " ".join(str(q[k]) for k in NAMES))
    ref = np.concatenate(ref).astype(np.float32)
    got = np.frombuffer(subprocess.run([exe], input=("\n".join(lines) + "\n").encode(),
                                       capture_output=True, check=True).stdout, np.float32)
    same = np.array_equal(got.view(np.uint32), ref.view(np.uint32))
    d = float(np.max(np.abs(got.astype(np.float64) - ref))) * 32768 if len(got) == len(ref) else float("inf")
    return same, d, v.faults


def main(exe, runs=40, nblocks=900):
    bad = 0
    for s in range(runs):
        same, d, faults = run(exe, s, nblocks)
        if not same:
            bad += 1
        print(f"run {s:3d}: {'IDENTICAL' if same else 'DIFFERS'}  max {d:.4f} LSB  faults {faults}", flush=True)
    print(f"{runs - bad}/{runs} identical")


if __name__ == "__main__":
    a = sys.argv[1:]
    main(a[0], *(int(x) for x in a[1:]))
