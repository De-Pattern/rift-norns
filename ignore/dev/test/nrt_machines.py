"""Each machine's UGen in scsynth (NRT) against the C++ driver (itself bit-exact to live.py).
    python3 ignore/dev/test/nrt_machines.py ignore/dev/build/drive_all"""
import os, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
exe = sys.argv[1]
# machine, seed, note, vel, off block (0: none; a multiple of 2 = an SC block edge), seconds, knobs from defaults
cases = [("fascia", 3, 60, 0.9, 0, 3.0), ("organ", 5, 57, 0.8, 0, 2.0), ("silt", 7, 52, 0.85, 1500, 3.0),
         ("ligature", 2, 48, 1.0, 1200, 2.5), ("resonator", 9, 64, 0.7, 0, 3.0)]
import compare_machines as C
key = {"resonator": "reson"}
lines = []
for m, seed, note, vel, offb, secs in cases:
    k = C.defaults(key.get(m, m))
    lines.append(" ".join(str(x) for x in (m, seed, note, vel, offb, secs, *k)))
d = tempfile.mkdtemp()
spec = os.path.join(d, "spec.txt")
open(spec, "w").write("\n".join(lines) + "\n")
env = dict(os.environ, QTWEBENGINE_CHROMIUM_FLAGS="--no-sandbox", QT_QPA_PLATFORM="offscreen")
subprocess.run(["sclang", os.path.join(os.path.dirname(__file__), "nrt_machines.scd"), spec, os.path.join(d, "o")],
               env=env, capture_output=True, timeout=300)
ok = 0
for i, (m, seed, note, vel, offb, secs) in enumerate(cases):
    raw = open(os.path.join(d, f"o_{i}.wav"), "rb").read()
    j = raw.find(b"data"); n = int.from_bytes(raw[j + 4:j + 8], "little")
    sc = np.frombuffer(raw[j + 8:j + 8 + n], np.float32)
    nb = int(secs * 48000) // 32
    k = lines[i].split()[6:]
    cmd = [f"M {key.get(m, m)} {seed}", f"N {note} {vel}"]
    for b in range(nb):
        if offb and b == offb:
            cmd.append("O")
        cmd.append("B " + " ".join(k))
    ref = np.frombuffer(subprocess.run([exe], input=("\n".join(cmd) + "\n").encode(), capture_output=True).stdout, np.float32)
    L = min(len(ref), len(sc))
    same = np.array_equal(ref[:L].view(np.uint32), sc[:L].view(np.uint32))
    ok += same
    print(f"{m:10s} scsynth == driver: {same}  ({L} samples, peak {np.abs(ref[:L]).max():.3f}"
          f"{', gate off at %.2fs' % (offb * 32 / 48000) if offb else ''})")
print(f"{ok}/{len(cases)} UGens identical in scsynth")
