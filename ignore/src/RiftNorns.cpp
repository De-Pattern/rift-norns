// RiftFascia: one FASCIA voice as a SuperCollider UGen (for norns).
//
//   RiftFascia.ar(trig, note, vel, seed, kill, p0 .. p31)
//
// trig  a rise above 0 strikes (note, vel 0..1 read then); a rise on a sounding voice steals
//       it the DN2 way (one block faded, then the strike)
// seed  the voice's noise seed (read at creation); kill > 0 fades the voice out in one block
// p0..  the 32 engine controls in RIFT's page order, 0..127 (read every 32 samples, as the
//       DN2 reads p-locks and LFOs once a block)
//
// The voice runs at 48 kHz in 32-sample blocks, exactly as RIFT's DSP and live.py do; output
// is 32 samples behind the strike at most. The UGen sets its done flag once the voice rests,
// so FreeSelfWhenDone frees the synth.

#include <new>

#include "SC_PlugIn.h"
#include "fascia_dsp.hpp"

static InterfaceTable *ft;

struct RiftFascia : public Unit {
    fascia::Voice *v;
    float buf[fascia::BLOCK];
    int pos;
    float prevtrig;
    bool struck;
    int killing;      // 0 no, 1 fade this block, 2 gone
};

static const int IN_TRIG = 0, IN_NOTE = 1, IN_VEL = 2, IN_SEED = 3, IN_KILL = 4, IN_P0 = 5;

static void RiftFascia_next(RiftFascia *unit, int inNumSamples);
static void RiftFascia_Ctor(RiftFascia *unit);
static void RiftFascia_Dtor(RiftFascia *unit);

static void RiftFascia_silence(RiftFascia *unit, int inNumSamples) { ClearUnitOutputs(unit, inNumSamples); }

static void RiftFascia_Ctor(RiftFascia *unit) {
    unit->v = nullptr;
    void *mem = RTAlloc(unit->mWorld, sizeof(fascia::Voice));
    if (!mem) {
        Print("RiftFascia: out of real-time memory\n");
        SETCALC(RiftFascia_silence);
        ClearUnitOutputs(unit, 1);
        return;
    }
    float s = IN0(IN_SEED);
    uint32_t seed = (s >= 1.0f && s < 4294967040.0f) ? (uint32_t)s : 1u;
    unit->v = new (mem) fascia::Voice(seed);
    unit->pos = fascia::BLOCK;
    unit->prevtrig = 0.0f;
    unit->struck = false;
    unit->killing = 0;
    for (int j = 0; j < fascia::BLOCK; ++j) unit->buf[j] = 0.0f;
    if (SAMPLERATE != 48000.0) Print("RiftFascia: the server runs at %g Hz; FASCIA is tuned for 48 kHz\n", SAMPLERATE);
    SETCALC(RiftFascia_next);
    OUT0(0) = 0.0f;
}

static void RiftFascia_Dtor(RiftFascia *unit) {
    if (unit->v) {
        unit->v->~Voice();
        RTFree(unit->mWorld, unit->v);
    }
}

static inline int ctl(float x) {
    if (!(x == x)) return 0;
    if (x <= 0.0f) return 0;
    if (x >= 127.0f) return 127;
    return (int)(x + 0.5f);
}

static void RiftFascia_next(RiftFascia *unit, int inNumSamples) {
    float *out = OUT(0);
    fascia::Voice *v = unit->v;
    float trig = IN0(IN_TRIG);
    if (trig > 0.0f && unit->prevtrig <= 0.0f && unit->killing == 0) {
        v->note_on(IN0(IN_NOTE), IN0(IN_VEL));
        unit->struck = true;
    }
    unit->prevtrig = trig;
    if (IN0(IN_KILL) > 0.0f && unit->killing == 0) unit->killing = 1;
    int q[fascia::NPARAM];
    for (int i = 0; i < inNumSamples; ++i) {
        if (unit->pos >= fascia::BLOCK) {
            unit->pos = 0;
            if (unit->killing == 2 || (unit->struck && v->idle())) {
                for (int j = 0; j < fascia::BLOCK; ++j) unit->buf[j] = 0.0f;
                if (!unit->mDone) unit->mDone = true;
            } else {
                for (int p = 0; p < fascia::NPARAM; ++p) q[p] = ctl(IN0(IN_P0 + p));
                v->block(q, unit->buf);
                if (unit->killing == 1) {
                    for (int j = 0; j < fascia::BLOCK; ++j)
                        unit->buf[j] = unit->buf[j] * (1.0f - (float)(j + 1) * (1.0f / fascia::BLOCK));
                    v->rest();
                    unit->killing = 2;
                }
            }
        }
        out[i] = unit->buf[unit->pos++];
    }
}

PluginLoad(RiftNorns) {
    ft = inTable;
    DefineDtorUnit(RiftFascia);
}
