# FASCIA for norns: dev

How the port is built and checked. Everything here lives under `ignore/`, which norns'
SuperCollider never compiles.

- `../src/fascia_dsp.hpp`: the voice, a line-for-line float32 port of DN2-Rift
  `src/dnfw/fascia/live.py` (RIFT commit 841448d).
- `../src/fascia_tables.h`: every constant as live.py's exact float32. It is generated, run
  from the repo root with DN2-Rift beside it:

      PYTHONPATH=../DN2-Rift/src python3 ignore/dev/tools/gen_tables.py > ignore/src/fascia_tables.h

- `../src/RiftNorns.cpp`: the SuperCollider UGens (one plugin, RiftNorns.so).
- `build.sh host|armhf`: builds the UGen. armhf rebuilds `ignore/RiftNorns/RiftNorns.so`, the stock
  norns image's plugin. It needs only GLIBC_2.4 symbols, with no libm or libstdc++.
- `tools/gen_patches.py`: writes `lib/patches.lua` from DN2-Rift's `scripts/fascia_audition.py`.

## Checks (all passed 2026-11-18)

- `test/compare.py`: the 30 audition patches against live.py. All 30 are bit-identical.
- `test/compare_mod.py`: random per-block control moves, stages switched mid-note, and
  steals. 12 of 12 are bit-identical.
- An armhf build under qemu: bit-identical to x86 on 6 patches, including BOW, BLOW, WATR,
  MORPH and TRADE.
- `test/nrt_test.scd`: the UGen in scsynth 3.13 (NRT) against the renderer. Bit-identical.
- `test/engine_test.scd`: Engine_RiftNorns live on JACK's dummy driver, with a CroneEngine stub.
  - Voices sound and free themselves.
  - The voice limit steals.
  - Mono steals in place.
- The engine's class compiles with the UGen absent: it looks the UGen up at run time.
- `lua5.3 ignore/dev/test/mock_norns.lua .`: the script against a mocked norns API. It covers
  every page and control, patch browse and load, and MIDI notes and CCs.

The renderers for the compare scripts:

    g++ -std=c++17 -O2 -ffp-contract=off -o ignore/dev/build/render ignore/dev/test/render.cpp
    g++ -std=c++17 -O2 -ffp-contract=off -o ignore/dev/build/drive ignore/dev/test/drive.cpp
