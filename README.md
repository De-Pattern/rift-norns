# rift-norns

RIFT's machines for norns, outside the Digitone II:

| machine | what it is |
|---|---|
| **FASCIA** | physical and spectral percussion and tuned bodies |
| **ORGAN** | Ravel's pipe organ |
| **SILT** | material synthesis played by gestures |
| **LIGATURE** | a twelve-block patcher |
| **RESONATOR** | a Rings-style modal resonator |

Each machine's DSP is a line-for-line C++ port of its float32 reference in DN2-Rift: the
`live.py` that RIFT's DSP program for it is held to, bit for bit. So a machine here renders
sample for sample what RIFT renders, in SuperCollider and on ARM. One machine plays at a time.

## Install

1. In maiden, run `;install https://github.com/De-Pattern/rift-norns`.
2. Load **rift-norns** from SELECT. The first run copies the UGens into
   `~/.local/share/SuperCollider/Extensions/RiftNorns` and asks for a restart.
3. **SYSTEM > RESTART**, then load rift-norns again. An update that changes the UGens asks
   for a restart the same way.

The prebuilt plugin is for the stock 32-bit norns image. On a 64-bit system the script builds it
on the device instead (needs `g++`; log in `/tmp/rift_norns_build.log`).

## Play

| control | what it does |
|---|---|
| **E1** | page (RIFT's pages for the machine; LIGATURE adds WIRING) |
| **E2** | select a control (on WIRING: a block) |
| **E3** | change it. A choice steps one option a click. On WIRING it changes the PATCH. |
| **hold K2 + E1** | choose the machine; let go of K2 to switch |
| **hold K2 + E3** | browse patches: FASCIA's 30 approved audition patches, or a machine's init; let go to load |
| **K3** | play the last note while held |

The screen follows RIFT's rules:
- The cells are laid out as the Digitone's knobs, A–D over E–H.
- Choices show as a word in a thin box.
- Values show in the header only while a control moves.
- Nothing overflows or touches.

**LIGATURE's WIRING** shows the patch's twelve blocks in three rows of four, in run order: the
same order as its BLOCKS pages.
- Each block's inputs are on its top edge (A left, B right) and its output is on its bottom
  edge.
- Cables hang from outputs to inputs.
- A source that is not a block is a tag over the input it feeds: PIT, GAT, TRG, VEL, NOI,
  LF1, LF2.
- The voice's output block has a notch on its right.
- The selected block and its cables are lit, and the header names the block and what its A
  and B inputs come from.

**Notes:**
- MIDI notes and note-offs come from the chosen device and channel. K3 does the same.
- SILT and LIGATURE release on their own gates.
- ORGAN, which plays until its next note on the Digitone, fades to silence over **organ
  release**: the Digitone's amp envelope's job.
- FASCIA and RESONATOR ring on, as struck things do.
- Every note is its own voice and frees itself when it rests. Past the voice limit (1..16,
  default 6), the oldest voice fades over one block. At 1 voice the machine is mono: a new
  note steals in place, the Digitone's way.

**MIDI CCs** follow RIFT: each control answers the CC of the record it rides on the Digitone, so
a RIFT track with param output drives the same machine here. The ten records without a RIFT
CC are on 102..111.

**PARAMS** holds each machine's 32 controls (RESONATOR's 8) in its own group. PSETs save every
machine. Also here: voices, level, pan, organ release, tremolo (FASCIA's audition AMP LFO for
the vibraphones), MIDI device and channel, and the CC map. SILT's LOOP gestures follow the
norns clock tempo.

## CPU

Measured on an x86 server core, not yet on a norns:

| machine | cost a sounding voice |
|---|---|
| FASCIA, init vibraphone | 0.4 % |
| FASCIA, heaviest case (BOW into BELL + GLASS, every post stage) | 3.2 % |
| ORGAN, init | 0.2 % |
| ORGAN, everything on | 0.4 % |
| SILT, init | 0.4 % |
| SILT, SWARM, DEEP, VOICES 127 | 0.8 % |
| LIGATURE | 0.2 % |
| RESONATOR | 0.5 % |

A Pi's ARM core is several times slower. Start at 6 voices and watch the CPU in norns' status.

## Licence

AGPL-3.0, as DN2-Rift. The SuperCollider plugin headers in `ignore/src/include` are GPL-3.0.
How the ports are built and checked: `ignore/dev/README.md`.
