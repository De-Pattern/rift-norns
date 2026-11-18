// RiftNorns.so: RIFT's machines as SuperCollider UGens, one voice each.
//
//   RiftFascia.ar / RiftOrgan.ar / RiftSilt.ar / RiftLigature.ar / RiftResonator.ar
//       (trig, note, vel, seed, kill, gate, rel, bpm, k0 .. kN)
//
// trig  a rise above 0 strikes (note, vel 0..1 read then); on a sounding voice it steals it
//       the DN2 way (one block faded, then the strike)
// seed  the voice's noise seed (read at creation); bpm SILT's loop tempo (read at creation)
// kill  > 0 fades the voice out over one block and ends it
// gate  its fall is the note-off: SILT and LIGATURE close their own gates; ORGAN (which plays
//       until its next note on the DN2) fades to silence (-80 dB) over REL seconds, as the DN2's
//       amp envelope would;
//       FASCIA and RESONATOR ring on, as struck things do
// k..   the machine's controls in RIFT's page order, 0..127, read every 32 samples as the DN2
//       reads p-locks and LFOs once a block (32 of them; RESONATOR 8)
//
// Each voice runs at 48 kHz in 32-sample blocks, exactly as RIFT's DSP and live.py do; output
// is 32 samples behind the strike at most. The done flag is set once the voice rests, so
// FreeSelfWhenDone frees the synth.

#include <new>

#include "SC_PlugIn.h"
#include "machines.hpp"

static InterfaceTable *ft;

static const int IN_TRIG = 0, IN_NOTE = 1, IN_VEL = 2, IN_SEED = 3, IN_KILL = 4, IN_GATE = 5, IN_REL = 6,
                 IN_BPM = 7, IN_K0 = 8;
enum OffMode { OFF_NONE, OFF_NATIVE, OFF_RELEASE };

struct RNUnit : public Unit {
    machines::AnyVoice *v;
    int nk, offmode;
    float buf[32];
    int pos;
    float prevtrig, prevgate;
    bool struck;
    int killing;              // 0 no, 1 fade this block, 2 gone
    bool releasing;
    float relenv, relk;
};

static inline int ctl(float x) {
    if (!(x == x)) return 0;
    if (x <= 0.0f) return 0;
    if (x >= 127.0f) return 127;
    return (int)(x + 0.5f);
}

static void RN_silence(RNUnit *unit, int n) { ClearUnitOutputs(unit, n); }

static void RN_next(RNUnit *unit, int inNumSamples) {
    float *out = OUT(0);
    machines::AnyVoice *v = unit->v;
    float trig = IN0(IN_TRIG);
    float gate = IN0(IN_GATE);
    if (trig > 0.0f && unit->prevtrig <= 0.0f && unit->killing == 0) {
        v->note_on(IN0(IN_NOTE), IN0(IN_VEL));
        unit->struck = true;
        unit->releasing = false;
        unit->relenv = 1.0f;
    }
    unit->prevtrig = trig;
    if (gate <= 0.0f && unit->prevgate > 0.0f && unit->struck) {
        if (unit->offmode == OFF_NATIVE) {
            v->note_off();
        } else if (unit->offmode == OFF_RELEASE && !unit->releasing) {
            float rel = IN0(IN_REL);
            rel = rel < 0.002f ? 0.002f : (rel > 30.0f ? 30.0f : rel);
            // -80 dB (1e-4) after REL seconds: k = 2^(log2(1e-4) / (REL x SR))
            unit->relk = fascia::exp2_((float)(-13.287712379549449 / ((double)rel * SAMPLERATE)));
            unit->releasing = true;
        }
    }
    unit->prevgate = gate;
    if (IN0(IN_KILL) > 0.0f && unit->killing == 0) unit->killing = 1;
    int q[32];
    for (int i = 0; i < inNumSamples; ++i) {
        if (unit->pos >= 32) {
            unit->pos = 0;
            bool quiet = unit->releasing && unit->relenv < 1e-4f;
            if (unit->killing == 2 || quiet || (unit->struck && v->idle())) {
                for (int j = 0; j < 32; ++j) unit->buf[j] = 0.0f;
                if (!unit->mDone) unit->mDone = true;
            } else {
                for (int p = 0; p < unit->nk; ++p) q[p] = ctl(IN0(IN_K0 + p));
                v->block(q, unit->buf);
                if (unit->killing == 1) {
                    for (int j = 0; j < 32; ++j) unit->buf[j] = unit->buf[j] * (1.0f - (float)(j + 1) * (1.0f / 32));
                    v->rest();
                    unit->killing = 2;
                }
            }
        }
        float y = unit->buf[unit->pos++];
        if (unit->releasing) {
            y = y * unit->relenv;
            unit->relenv = unit->relenv * unit->relk;
        }
        out[i] = y;
    }
}

template <class V>
static void RN_ctor(RNUnit *unit, int nk, int offmode) {
    unit->v = nullptr;
    void *mem = RTAlloc(unit->mWorld, sizeof(V));
    if (!mem) {
        Print("RiftNorns: out of real-time memory\n");
        SETCALC(RN_silence);
        ClearUnitOutputs(unit, 1);
        return;
    }
    float s = IN0(IN_SEED);
    unsigned seed = (s >= 0.0f && s < 4294967040.0f) ? (unsigned)s : 1u;
    V *v = new (mem) V(seed);
    unit->v = v;
    unit->nk = nk;
    unit->offmode = offmode;
    unit->pos = 32;
    unit->prevtrig = 0.0f;
    unit->prevgate = 1.0f;
    unit->struck = false;
    unit->killing = 0;
    unit->releasing = false;
    unit->relenv = 1.0f;
    unit->relk = 1.0f;
    for (int j = 0; j < 32; ++j) unit->buf[j] = 0.0f;
    if (SAMPLERATE != 48000.0) Print("RiftNorns: the server runs at %g Hz; RIFT's machines are tuned for 48 kHz\n", SAMPLERATE);
    SETCALC(RN_next);
    OUT0(0) = 0.0f;
}

static void RN_dtor(RNUnit *unit) {
    if (unit->v) {
        unit->v->~AnyVoice();
        RTFree(unit->mWorld, unit->v);
    }
}

#define RN_UGEN(NAME, TYPE, NK, OFF)                                                   \
    struct NAME : public RNUnit {};                                                    \
    static void NAME##_Ctor(NAME *unit) { RN_ctor<TYPE>(unit, NK, OFF); }              \
    static void NAME##_Dtor(NAME *unit) { RN_dtor(unit); }

RN_UGEN(RiftFascia, machines::FasciaV, 32, OFF_NONE)
RN_UGEN(RiftOrgan, machines::OrganV, 32, OFF_RELEASE)
RN_UGEN(RiftResonator, machines::ResonV, 8, OFF_NONE)
RN_UGEN(RiftLigature, machines::LigatureV, 32, OFF_NATIVE)

struct RiftSilt : public RNUnit {};
static void RiftSilt_Ctor(RiftSilt *unit) {
    RN_ctor<machines::SiltV>(unit, 32, OFF_NATIVE);
    if (unit->v) {
        float bpm = IN0(IN_BPM);
        static_cast<machines::SiltV *>(unit->v)->v.bpm = (bpm >= 20.0f && bpm <= 300.0f) ? bpm : 120.0f;
    }
}
static void RiftSilt_Dtor(RiftSilt *unit) { RN_dtor(unit); }

PluginLoad(RiftNorns) {
    ft = inTable;
    DefineDtorUnit(RiftFascia);
    DefineDtorUnit(RiftOrgan);
    DefineDtorUnit(RiftResonator);
    DefineDtorUnit(RiftLigature);
    DefineDtorUnit(RiftSilt);
}
