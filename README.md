# rift-norns

RIFT's machines for norns, outside the Digitone II. Each machine's DSP is a line-for-line C++ port
of its float32 reference in DN2-Rift (the `live.py` each RIFT DSP program is held to bit for bit).
So a machine here renders sample for sample what RIFT renders, in SuperCollider and on ARM.

| version | machines |
|---|---|
| v0.1 | FASCIA |
| to come | ORGAN, RESONATOR, SILT and LIGATURE, switched by a MACHINE SEL, one at a time |

## Install

1. In maiden, run `;install https://github.com/De-Pattern/rift-norns`.
2. Load **rift-norns** from SELECT. The first run copies the UGens into
   `~/.local/share/SuperCollider/Extensions/RiftNorns` and asks for a restart.
3. **SYSTEM > RESTART**, then load rift-norns again. An update that changes the UGens asks
   for a restart the same way.

The prebuilt plugin is for the stock 32-bit norns image. On a 64-bit system the script builds it
on the device instead (needs `g++`; log in `/tmp/rift_norns_build.log`).

## Play

- **E1:** page (SYN1 EXCITERS, SYN2 RESONATORS, SYN3 SPECTRAL, SYN4 ONSET/INTERACT).
- **E2:** select a control. **E3:** change it. Choices step one option a click.
- **Hold K2 + turn E3:** browse the 30 approved FASCIA audition patches and init. Let go of
  K2 to load.
- **K3:** play the last note.
- **MIDI:** notes on the chosen device and channel, with velocity.
  - CCs follow RIFT's FASCIA map, so a RIFT FASCIA track with param output sends its knob
    moves straight here.
  - The ten controls without a RIFT CC are on 102..111: DUST, TZFM, BEND, MIX, GEOM, MATL,
    POS, COUP, RING, BUZZ.
- **PARAMS:**
  - The 32 controls are in four groups. PSETs save them.
  - **voices** is 1..16 (default 6). 1 is mono: a new note steals in place, the DN2 way.
  - Also here: level, pan, tremolo (the audition's AMP LFO for the vibraphones), MIDI
    device and channel, and the CC map.

Every note is its own voice and frees itself when it rests. Past the voice limit, the oldest
voice fades over one block.

## CPU

Measured on an x86 server core, not yet on a norns:

| patch | cost a sounding voice |
|---|---|
| init vibraphone | 0.6 % |
| heaviest case: BOW into BELL + GLASS, with every post stage on | 3.2 % |

A Pi's ARM core is several times slower, so expect roughly 10+ vibraphone voices but only a
few of the heaviest. Start at 6 voices and watch the CPU in norns' status.

## Licence

AGPL-3.0, as DN2-Rift. The SuperCollider plugin headers in `ignore/src/include` are GPL-3.0.
How the ports are built and checked: `ignore/dev/README.md`.
