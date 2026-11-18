"""Hold each machine's C++ port to its DN2-Rift live.py, block by block: random patches,
controls moving every so often (p-locks, LFOs, knob turns), notes, retriggers and note-offs.

    PYTHONPATH=../DN2-Rift/src python3 ignore/dev/test/compare_machines.py ignore/dev/build/drive_all MACHINE [runs] [blocks]
"""
import random
import subprocess
import sys

import numpy as np

F = np.float32


class Ref:
    """the python voice behind one interface"""

    def __init__(self, machine, seed):
        self.m = machine
        self.trig = None
        self.off = False
        if machine == "organ":
            from dnfw.organ import live as O
            self.v = O.Voice(O.tables(), seed)
            self.n = 32
        elif machine == "fascia":
            from dnfw.fascia import live as L
            self.v = L.Voice(seed=seed)
            self.n = 32
        elif machine == "reson":
            from dnfw.reson import live as R
            self.v = R.Voice(seed)
            self.n = 8
        elif machine == "silt":
            from dnfw.silt import live as S
            self.S = S
            self.v = S.Voice((0x2545F491 + seed) | 1)
            self.n = 32
        elif machine == "ligature":
            from dnfw.ligature import live as G
            self.v = G.Voice(G.slot_seed(seed))
            self.n = 32

    def note(self, note, vel_int):
        self.trig = (note, vel_int)

    def note_off(self):
        self.off = True

    def block(self, knobs):
        trig, self.trig = self.trig, None
        off, self.off = self.off, False
        m = self.m
        if m == "organ":
            return self.v.block(knobs, trig is not None, trig[0] if trig else 0.0, trig[1] if trig else 127)
        if m == "fascia":
            if trig:
                self.v.note_on(trig[0], F(trig[1]) / F(127.0))
            return self.v.block({**dict(zip(KN[m], knobs)), "_internal": True})
        if m == "reson":
            return self.v.block(knobs, trig is not None, trig[0] if trig else 0.0, trig[1] if trig else 127)
        if m == "silt":
            P = self.S.eff_of(knobs)
            if trig:
                self.v.note(P, trig[0], F(min(trig[1], 127)) * self.S.INV127)
            return self.v.block(P, off=off)
        if m == "ligature":
            from dnfw.ligature import live as G
            if trig:
                self.v.note(trig[0], F(min(trig[1], 127)) * G.INV127)
            if off:
                self.v.off()
            return self.v.block(knobs)


def names(m):
    if m == "fascia":
        from dnfw.fascia import model as M
        return list(M.INTERNAL)
    if m == "organ":
        from dnfw.organ import machines as OM
        return list(OM.CONTROLS)
    if m == "reson":
        from dnfw.reson import live as R
        return list(R.CONTROLS)
    if m == "silt":
        from dnfw.silt import live as S
        return list(S.CONTROLS)
    from dnfw.ligature import live as G
    return list(G.CONTROLS)


KN = {}


def defaults(m):
    if m == "organ":
        from dnfw.organ import machines as OM
        return [OM.DEFAULTS[c] for c in OM.CONTROLS]
    if m == "fascia":
        from dnfw.fascia import model as M
        return [M.DEFAULTS[c] for c in M.INTERNAL]
    if m == "reson":
        from dnfw.reson import live as R
        return [R.DEFAULTS[c] for c in R.CONTROLS]
    if m == "silt":
        from dnfw.silt import live as S, machines as SM
        return [SM.KNOB_DEFAULTS[c] if c in SM.KNOB_DEFAULTS else S.option_knob(SM.OPT_DEFAULTS[c], len(SM.CHOICES[c]))
                for c in S.CONTROLS]
    from dnfw.ligature import live as G
    return [G.DEFAULTS[c] for c in G.CONTROLS]


def run(exe, m, s, nblocks, start=None, moves=True):
    rnd = random.Random(s * 7919 + hash(m) % 1000)
    seed = rnd.randrange(0, 64)
    ref = Ref(m, seed)
    n = ref.n
    knobs = list(start) if start is not None else [rnd.randrange(128) for _ in range(n)]
    lines = [f"M {m} {seed}"]
    out = []
    note = float(rnd.randrange(36, 84))
    vel = rnd.randrange(1, 128)
    ref.note(note, vel)
    lines.append(f"N {note!r} {vel / 127.0!r}")
    for b in range(nblocks):
        if moves and rnd.random() < 0.12:
            for _ in range(rnd.randrange(1, 4)):
                knobs[rnd.randrange(n)] = rnd.randrange(128)
        if rnd.random() < 0.012:
            note = float(rnd.randrange(36, 84))
            vel = rnd.randrange(1, 128)
            ref.note(note, vel)
            lines.append(f"N {note!r} {vel / 127.0!r}")
        if rnd.random() < 0.01:
            ref.note_off()
            lines.append("O")
        out.append(np.asarray(ref.block(list(knobs)), np.float32))
        lines.append("B " + " ".join(str(k) for k in knobs))
    want = np.concatenate(out)
    got = np.frombuffer(subprocess.run([exe], input=("\n".join(lines) + "\n").encode(), capture_output=True,
                                       check=True).stdout, np.float32)
    if len(got) != len(want):
        return False, float("inf"), 0.0
    same = np.array_equal(got.view(np.uint32), want.view(np.uint32))
    return same, float(np.max(np.abs(got.astype(np.float64) - want))) * 32768, float(np.max(np.abs(want)))


def main(exe, m, runs=8, nblocks=500):
    KN[m] = names(m)
    bad = 0
    for s in range(runs):
        start = defaults(m) if s == 0 else None          # run 0: the machine's own init sound, held
        same, d, pk = run(exe, m, s, nblocks, start, moves=s != 0)
        bad += not same
        print(f"{m} run {s:2d}: {'IDENTICAL' if same else 'DIFFERS'}  max {d:.4f} LSB  peak {pk:.3f}", flush=True)
    print(f"{m}: {runs - bad}/{runs} identical")


if __name__ == "__main__":
    a = sys.argv[1:]
    main(a[0], a[1], *(int(x) for x in a[2:]))
