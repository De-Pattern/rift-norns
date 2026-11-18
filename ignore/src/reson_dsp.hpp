// RESONATOR — RIFT's Rings-style modal resonator, as a portable C++ voice.
//
// A line-for-line port of DN2-Rift's src/dnfw/reson/live.py (float32, live.py's order).
// Voice v(slot); v.note_on(note, vel); v.block(knobs8, out32). knobs: TUNE LEV STRC BRIT DAMP
// POS EXCT HIT, 0..127. A struck voice rings until it is quiet, then rests.
#pragma once

#include <cmath>

#include "reson_tables.h"
#include "rv_math.hpp"

namespace reson {

using namespace rv;

static const int SUB = 32;
static const int MODES = 24;
static const float TOP_NOTE = 135.0f;
static const float IDLE = 1.0e-5f;
static const float OUT_GAIN = 0.12f;
static const float NORM = 0.25f;
static const int NOISE_LEN = 576;
static const int EXC_END = 1024;
static const float INV127 = 1.0f / 127.0f;
static const float INV100 = 1.0f / 100.0f;
static const float INV12 = 1.0f / 12.0f;
static const float KNEE_K = 0.6f, KNEE_ROOM = 0.6f;
static const float GUARD = 65536.0f;

static inline float knee(float x) {
    float a = x < 0.0f ? -x : x;
    if (a <= KNEE_K) return x;
    float o = a - KNEE_K;
    if (KNEE_ROOM < o) o = KNEE_ROOM;
    float y = KNEE_K + (o - (o * o) * (0.5f / KNEE_ROOM));
    return x > 0.0f ? y : -y;
}

struct Controls { float pitch, frac; int a0; float l0, kexp, pos, mix, hit, lev, tilt_g, minc, mnorm, nlp_a; };

static inline Controls controls(const int *k, float note) {
    Controls c;
    float tun = (float)(k[0] - 64);
    float p = note + tun;
    p = p > 0.0f ? p : 0.0f;
    c.pitch = p < 127.0f ? p : 127.0f;
    float st = (float)k[2] * INV127;
    float b = 0.15f + 0.8f * ((float)k[3] * INV127);
    float kd = (float)k[4] * INV127;
    c.l0 = L_DAMP0 - kd * L_DAMP1;
    c.kexp = ((1.0f - b) * 1.3f) + 0.2f;
    c.pos = 0.02f + 0.48f * ((float)k[5] * INV127);
    c.mix = (float)k[6] * INV127;
    c.hit = (float)k[7] * INV100;
    c.lev = (float)k[1] * INV100;
    if (st <= 0.5f) { c.frac = st * 2.0f; c.a0 = 0; }
    else { c.frac = st * 2.0f - 1.0f; c.a0 = 1; }
    c.tilt_g = 0.6f + 0.4f * b;
    float d = 1.0f - b;
    float width = (float)(0.0004 * 48000) + (float)(0.008 * 48000) * (d * d);
    float rw = recip(width);
    c.minc = 0.5f * rw;
    float mn = 24.0f * rw;
    c.mnorm = mn < 1.0f ? mn : 1.0f;
    float na = ((b * b) * 12000.0f + 300.0f) * (6.28318530717958647692f * DT);
    c.nlp_a = na < 1.0f ? na : 1.0f;
    return c;
}

struct Voice {
    uint32_t slot;
    float y1[MODES], y2[MODES];
    bool on = false;
    int exc = 0;
    float nenv = 0, nlp = 0;
    uint32_t rng;
    float note = 0;
    bool trig = false;
    float tnote = 0;

    explicit Voice(uint32_t slot_ = 0) : slot(slot_) { reset(); }
    void reset() {
        for (int m = 0; m < MODES; ++m) y1[m] = y2[m] = 0.0f;
        on = false; exc = 0; nenv = nlp = 0.0f;
        rng = 0x2545F491u + slot;
        note = 0.0f;
    }
    void note_on(float n, float) { trig = true; tnote = n; }
    bool idle() const { return !on && !trig; }
    void rest() { on = false; trig = false; }

    void block(const int *knobs, float *out) {
        if (trig) {
            trig = false;
            on = true; exc = 0; nenv = 1.0f; nlp = 0.0f;
            note = tnote;
        }
        if (!on) { for (int i = 0; i < SUB; ++i) out[i] = 0.0f; return; }
        Controls c = controls(knobs, note);
        float x[SUB];
        for (int i = 0; i < SUB; ++i) x[i] = 0.0f;
        const float ND = NOISE_DECAY;
        for (int i = 0; i < SUB; ++i) {
            if (exc >= EXC_END) break;
            float ph = (float)exc * c.minc;
            float mal = ph < 0.5f ? sin_turns(ph) * c.mnorm : 0.0f;
            float nz = xs_bi(rng);
            if (exc < NOISE_LEN) {
                nz = nz * nenv;
                nenv = nenv * ND;
            } else {
                nz = 0.0f;
            }
            nlp = nlp + c.nlp_a * (nz - nlp);
            float xx = (1.0f - c.mix) * mal + c.mix * (0.5f * nlp);
            x[i] = xx * c.hit;
            exc += 1;
        }
        float acc[SUB];
        for (int i = 0; i < SUB; ++i) acc[i] = 0.0f;
        // the modes' coefficients, low to high, until one is past the top
                float asum = 0.0f, tilt = 1.0f;
        int nm = 0;
        for (int m = 0; m < MODES; ++m) {
            float lo = ANCH[c.a0][m], hi = ANCH[c.a0 + 1][m];
            float semi = lo + c.frac * (hi - lo);
            float q = c.pitch + semi;
            if (q >= TOP_NOTE) break;
            float ft = note_hz(q) * DT;
            float cw = sin_turns(ft + 0.25f), sw = sin_turns(ft);
            float L = c.l0 + c.kexp * (semi * INV12);
            float R = expf_(0.0f - exp2f_(L));
            float amp = sin_turns(((float)(m + 1) * c.pos) * 0.5f) * tilt;
            float a1 = 2.0f * (R * cw), a2 = R * R, b0 = amp * sw;
            float aa = amp < 0.0f ? -amp : amp;
            asum = asum + aa;
            tilt = tilt * c.tilt_g;
            float yy1 = y1[m], yy2 = y2[m];
            for (int i = 0; i < SUB; ++i) {
                float y = (a1 * yy1 - a2 * yy2) + b0 * x[i];
                yy2 = yy1;
                yy1 = y;
                acc[i] = acc[i] + y;
            }
            y1[m] = yy1;
            y2[m] = yy2;
            nm = m + 1;
        }
        for (int m = nm; m < MODES; ++m) y1[m] = y2[m] = 0.0f;
        for (int i = 0; i < SUB; ++i) {
            if (!(-GUARD < acc[i] && acc[i] < GUARD)) {
                reset();
                for (int k = 0; k < SUB; ++k) out[k] = 0.0f;
                return;
            }
        }
        float tilt_p = exp2f_((c.pitch - 60.0f) * (float)(1.0 / 24.0));
        float g = ((c.lev * OUT_GAIN) * recip(1.0f + NORM * asum)) * tilt_p;
        float peak = 0.0f;
        for (int i = 0; i < SUB; ++i) {
            out[i] = knee(acc[i] * g);
            float a = acc[i] < 0.0f ? -acc[i] : acc[i];
            peak = peak > a ? peak : a;
        }
        if (exc >= EXC_END && peak < IDLE) {
            on = false;
            for (int m = 0; m < MODES; ++m) y1[m] = y2[m] = 0.0f;
        }
    }
};

}  // namespace reson
