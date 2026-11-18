# rift-norns: dev

How the ports are built and checked. Everything here lives under `ignore/`, which norns'
SuperCollider never compiles. Commands run from the repo root, with DN2-Rift beside it
(`../DN2-Rift`).

## The source (`ignore/src`)

Each machine is a line-for-line float32 port of its DN2-Rift `live.py` (RIFT commit
841448d), every expression in live.py's order, built with `-ffp-contract=off`:

| file | port of |
|---|---|
| `fascia_dsp.hpp` | `fascia/live.py` |
| `organ_dsp.hpp` | `organ/live.py` |
| `silt_dsp.hpp` | `silt/live.py` |
| `ligature_dsp.hpp` | `ligature/live.py` |
| `reson_dsp.hpp` | `reson/live.py` |
| `rv_math.hpp` | `organ/live.py`'s Ravel leaves (rv_math.h) |

- `*_tables.h` are generated (`ignore/dev/tools/gen_*.py`), so every constant is live.py's
  exact float32.
- `machines.hpp` puts the five voices behind one interface.
- `RiftNorns.cpp` is the five UGens: RiftFascia, RiftOrgan, RiftSilt, RiftLigature and
  RiftResonator, in one plugin.

## Generating and building

- `ignore/dev/tools/gen_machines.py` writes `ignore/RiftNorns/RiftNorns.sc` (the UGen classes)
  and `lib/machines.lua` (pages, labels, choices, defaults, CCs, LIGATURE's wiring) from
  DN2-Rift's panel tables.
- `ignore/dev/tools/gen_patches.py` writes `lib/patches.lua` (FASCIA's audition patches).
- `ignore/dev/build.sh host|armhf` builds the plugin. armhf rebuilds
  `ignore/RiftNorns/RiftNorns.so`, the stock norns image's plugin. It needs only GLIBC_2.4
  symbols, with no libstdc++.

## Checks (all passed 2026-11-18)

- `test/compare.py`: FASCIA's 30 audition patches against live.py. All 30 are bit-identical.
- `test/compare_machines.py MACHINE`: a machine's voice block by block against live.py, with
  random patches, controls moving, retriggers and note-offs. Bit-identical:

  | machine | runs |
  |---|---|
  | FASCIA | 3/3 |
  | ORGAN | 6/6 |
  | RESONATOR | 8/8 |
  | SILT | 4/4 |
  | LIGATURE | all 13 patches |

- `test/sweep_silt.py`: SILT on every material × gesture × mode. 160/160 are bit-identical.
- An armhf build of every machine under qemu: bit-identical to x86 (15/15 runs, with controls
  moving and note-offs).
- `test/nrt_machines.py`: each UGen in scsynth 3.13 (NRT) against the driver, with SILT's and
  LIGATURE's gate-offs. 5/5 are bit-identical.
- `test/engine_test.scd`: Engine_RiftNorns live on JACK's dummy driver, with a CroneEngine
  stub.
  - Every machine plays and frees itself.
  - The voice limit steals, and mono steals in place.
  - Note-offs close SILT, LIGATURE and ORGAN.
- The engine's class compiles with the UGens absent: it looks them up at run time.
- `test/mock_rift.lua` + `test/render_screens.py`: the script against a mocked norns API. It
  covers:
  - every machine, page and control;
  - every LIGATURE wiring with each block selected;
  - patch browsing and machine select;
  - MIDI notes, offs and CCs.

  Its 618 screens are replayed with cairo and norns' own font: no text leaves the screen,
  overlaps another, or touches a box.

The drivers:

    g++ -std=c++17 -O2 -ffp-contract=off -o ignore/dev/build/render ignore/dev/test/render.cpp
    g++ -std=c++17 -O2 -ffp-contract=off -o ignore/dev/build/drive_all ignore/dev/test/drive_all.cpp
