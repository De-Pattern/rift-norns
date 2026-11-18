"""Hold the C++ port to live.py on the 30 audition patches.

    PYTHONPATH=../DN2-Rift/src:../DN2-Rift/scripts python3 ignore/dev/test/compare.py ignore/dev/build/render [names...]

Prints, per patch, the largest sample difference in LSB of 16-bit and whether the two are
bit-identical. live.py is slow (per-sample Python): the full set takes a while.
"""
import subprocess
import sys
import time

import numpy as np

import fascia_audition as A
from dnfw.fascia import live as L
from dnfw.fascia import model as M


def spec(p, evs, secs):
    q = L.engine(p)
    lines = ["P " + " ".join(str(q[k]) for k in M.INTERNAL), f"S {secs!r}"]
    lines += [f"E {t!r} {n!r} {v!r}" for t, n, v in evs]
    return "\n".join(lines) + "\n"


def main(exe, names):
    worst = 0
    for name, (p, evs, secs, _trem) in A.PATCHES.items():
        if names and not any(s in name for s in names):
            continue
        t = time.time()
        ref = L.render_notes(p, evs, secs)
        tr = time.time() - t
        t = time.time()
        raw = subprocess.run([exe], input=spec(p, evs, secs).encode(), capture_output=True, check=True).stdout
        tc = time.time() - t
        got = np.frombuffer(raw, np.float32)
        assert len(got) == len(ref), (len(got), len(ref))
        d = np.abs(got.astype(np.float64) - ref.astype(np.float64))
        lsb = float(d.max()) * 32768
        same = bool(np.array_equal(got.view(np.uint32), ref.view(np.uint32)))
        worst = max(worst, lsb)
        first = int(np.argmax(d > 0)) if not same else -1
        print(f"{name:28s} {'IDENTICAL' if same else 'differs'}  max {lsb:9.4f} LSB  first diff @ {first}"
              f"  (py {tr:5.1f}s, c++ {tc*1000:6.1f} ms)", flush=True)
    print(f"worst: {worst:.4f} LSB")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
