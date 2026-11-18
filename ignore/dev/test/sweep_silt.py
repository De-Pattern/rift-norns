"""SILT across every material x gesture x mode (from the init sound, controls moving), against live.py.
    PYTHONPATH=../DN2-Rift/src python3 ignore/dev/test/sweep_silt.py ignore/dev/build/drive_all"""
import sys
sys.path.insert(0, __file__.rsplit("/", 1)[0])
import compare_machines as C
from dnfw.silt import live as S

exe = sys.argv[1]
C.KN["silt"] = C.names("silt")
base = C.defaults("silt")
bad = n = 0
for mat in range(4):
    for gest in range(10):
        for mode in range(4):
            k = list(base)
            k[24] = S.option_knob(mat, 4)
            k[25] = S.option_knob(gest, 10)
            k[26] = S.option_knob(mode, 4)
            k[17] = 90                                   # VOICES up: filaments in every mode
            same, d, pk = C.run(exe, "silt", 1000 + mat * 100 + gest * 10 + mode, 160, k, moves=True)
            n += 1
            bad += not same
            if not same:
                print(f"DIFFERS mat {mat} gest {gest} mode {mode}: {d:.4f} LSB", flush=True)
print(f"silt sweep: {n - bad}/{n} identical", flush=True)
