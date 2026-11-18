// FASCIA — the RIFT physical/spectral machine as a portable C++ voice.
//
// A line-for-line port of DN2-Rift's src/dnfw/fascia/live.py (the float32, DSP-order
// contract the Digitone II program is held to). Every float is a float32 and every
// expression is evaluated in the order live.py writes it, one rounding per operation, so a
// build with -ffp-contract=off (no fused multiply-add, no fast-math) renders sample for
// sample what live.py renders (test/compare.py checks the 30 audition patches).
//
// Interface: Voice v(seed); v.note_on(note, vel); then v.block(controls, out32) every 32
// samples at 48 kHz. controls: the 32 engine controls (Param order), integers 0..127.
// v.idle() is true at rest. A note_on on a sounding voice steals it: the next block fades the
// old sound out over 32 samples, the strike is the block after.

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "fascia_tables.h"

namespace fascia {

static const int BLOCK = 32;
static const float RATEF = 48000.0f;
static const int MODES = 16;
static const int TWINS = 8;
static const int MAXN = MODES + TWINS;

// -- arithmetic ------------------------------------------------------------------------------
static inline float fmin_(float a, float b) { return b < a ? b : a; }   // numpy minimum (no NaN)
static inline float fmax_(float a, float b) { return b > a ? b : a; }
static inline float fabs_(float x) { return x < 0.0f ? -x : (x == 0.0f ? 0.0f : x); }

static inline uint32_t fbits(float x) { uint32_t u; std::memcpy(&u, &x, 4); return u; }
static inline float bitsf(uint32_t u) { float x; std::memcpy(&x, &u, 4); return x; }

static inline float floor_(float x) {
    // floor for |x| < 2^23 (larger values are integers already)
    if (!(fabs_(x) < 8388608.0f)) return x;
    float i = (float)(int32_t)x;
    return i > x ? i - 1.0f : i;
}

static inline float ldexp_(float p, int i) {
    // p x 2^i, p in 1..2 (exp2's polynomial), i in -100..100: exact, as numpy ldexp
    if (i > 127) return INFINITY;
    if (i >= -126) return p * bitsf((uint32_t)(i + 127) << 23);
    // subnormal result: two exact steps then one rounding, as ldexp
    return (p * bitsf((uint32_t)(i + 64 + 127) << 23)) * bitsf((uint32_t)(-64 + 127) << 23);
}

static inline float exp2_(float x) {
    x = fmin_(fmax_(x, -100.0f), 100.0f);
    float i = floor_(x);
    float f = x - i;
    float p = EXP2_C[5];
    for (int c = 4; c >= 0; --c) p = p * f + EXP2_C[c];
    return ldexp_(p, (int)i);
}

static inline float sinp(float h) {
    float h2 = h * h;
    float p = SIN_C[4];
    for (int c = 3; c >= 0; --c) p = p * h2 + SIN_C[c];
    return p * h;
}

static inline float cosp(float h) {
    float h2 = h * h;
    float p = COS_C[5];
    for (int c = 4; c >= 0; --c) p = p * h2 + COS_C[c];
    return p;
}

static inline float expm1_(float y) {
    float p = EXPM1_C[5];
    for (int c = 4; c >= 0; --c) p = p * y + EXPM1_C[c];
    return p * y;
}

static inline float recip(float x) {
    float r = bitsf(0x7EF311C7u - fbits(x));
    for (int i = 0; i < 3; ++i) r = r * (2.0f - x * r);
    return r;
}

static inline float rsqrt(float x) {
    float r = bitsf(0x5F3759DFu - (fbits(x) >> 1));
    float hx = x * 0.5f;
    for (int i = 0; i < 3; ++i) r = r * (1.5f - (r * r) * hx);
    return r;
}

static inline float sqrt_(float x) {
    float r = x * rsqrt(fmax_(x, 1e-30f));
    return x > 0.0f ? r : 0.0f;
}

static inline float tanh_r(float x) {
    x = fmin_(fmax_(x, -TANH_TOP), TANH_TOP);
    float x2 = x * x;
    float n = TANH_N[3];
    for (int c = 2; c >= 0; --c) n = n * x2 + TANH_N[c];
    float d = TANH_D[3];
    for (int c = 2; c >= 0; --c) d = d * x2 + TANH_D[c];
    return (x * n) * recip(d);
}

static inline float frac(float x) { return x - floor_(x); }

static inline float tab(float x) {
    x = fmin_(fmax_(x, 0.0f), TOPT);
    int i = (int)x;
    float f = x - (float)i;
    float a = SIN_T[i];
    return a + f * (SIN_T[i + 1] - a);
}
static inline float sin_t(float ph) { return tab(ph * 256.0f); }
static inline float cos_t(float ph) { return tab(ph * 256.0f + 64.0f); }

static inline int clampi(int v) { return v < 0 ? 0 : (v > 127 ? 127 : v); }
static inline float u(int v) { return (float)clampi(v) * INV127; }
static inline float expmap(int v, float lo, float lg) { return lo * exp2_(u(v) * lg); }
static inline float onepole_inv(float inv_seconds) { return exp2_(OP1 * inv_seconds); }
static inline float onepole_inv32(float inv_seconds) { return exp2_(OP32 * inv_seconds); }
static inline float fl(float x) { return (-FLOORF < x && x < FLOORF) ? 0.0f : x; }

static inline int choice_n(int v, int n) {
    int c = (clampi(v) * n) >> 7;
    return c < n - 1 ? c : n - 1;
}

static inline float smear_pole(float var, int stages) {
    float s = var * (1.0f / (float)stages);
    if (s <= 0.0f) return 0.0f;
    float t = 1.0f + 2.0f * s;
    return (2.0f * s) * recip(t + sqrt_(1.0f + 4.0f * s));
}

// -- noise ---------------------------------------------------------------------------------
struct XS {
    uint32_t s;
    void init(uint32_t seed) {
        uint32_t v = 0x2545F491u + ((seed & 0xFFFFu) << 16) + (seed & 0xFFFFu);
        s = v ? v : 1u;
        for (int i = 0; i < 3; ++i) next();
    }
    uint32_t next() {
        uint32_t x = s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s = x;
        return x;
    }
    float bi() { return (float)(int32_t)next() * 4.656612873077393e-10f; }   // 2^-31
    float u01() { return (float)(next() >> 8) * 5.960464477539063e-08f; }    // 2^-24
    float gauss() {
        float a = bi();
        return (a + bi()) * SQ15;
    }
};

// -- choices -------------------------------------------------------------------------------
enum ExcKind { STRIKE, BLOW, BOW, DUST, PULSE, TZFM };
enum Click_ { SNAP, TICK, NOISE, THUMP };
enum XMod { EXCITE, XBEND, CHOKE, TRADE };
static const int BODY_OFF = 9;

// -- block controls (live.block_controls) -----------------------------------------------------
struct BC {
    float hard, uhard, tilt, geom_e, inv_t60, matl, pos, bend, ubend, chirp, lpg, ulpg, coup;
    int xmod;
    float fdbk, buzz, morph, ga, gb, dec;
    int DUST;
    float udust, utz, side;
    int ring, shift, form, watr, fold, chr, LPG, TUNE;
};

static inline void block_controls(const int *q, float vel, BC &c) {
    float hv = 0.5f + 0.5f * vel;
    float hard = u(q[P_HARD]) * hv;
    float inv_t60 = INV_003 * exp2_(-u(127 - q[P_DAMP]) * LG_400);
    float mix = u(q[P_MIX]) * HALF_PI;
    float side = u(q[P_SIDE]);
    c.hard = hard;
    c.uhard = u(q[P_HARD]);
    c.tilt = 1.2f * (1.0f - hard);
    c.geom_e = 0.6f + 0.8f * u(q[P_GEOM]);
    c.inv_t60 = inv_t60;
    c.matl = u(q[P_MATL]);
    c.pos = 0.02f + 0.48f * u(q[P_POS]);
    c.bend = (u(q[P_BEND]) * 7.0f) * (0.4f + 0.6f * vel);
    c.ubend = u(q[P_BEND]);
    c.chirp = (u(q[P_HARD]) * vel) * 0.6f;
    c.lpg = u(q[P_LPG]) * 6.0f;
    c.ulpg = u(q[P_LPG]);
    c.coup = u(q[P_COUP]);
    c.xmod = choice_n(q[P_XMOD], 4);
    c.fdbk = u(q[P_FDBK]);
    c.buzz = u(q[P_BUZZ]);
    c.morph = u(q[P_MORPH]) * 1.4f;
    c.ga = fmax_(cosp(mix), 0.0f);
    c.gb = sinp(mix);
    c.dec = expmap(q[P_DEC], 0.004f, LG_DEC);
    c.DUST = q[P_DUST];
    c.udust = u(q[P_DUST]);
    c.utz = u(q[P_TZFM]);
    c.side = 0.5f + 6.5f * (side * sqrt_(side));
    c.ring = q[P_RING];
    c.shift = q[P_SHIFT];
    c.form = q[P_FORM];
    c.watr = q[P_WATR];
    c.fold = q[P_FOLD];
    c.chr = q[P_CHAR];
    c.LPG = q[P_LPG];
    c.TUNE = q[P_TUNE];
}

static inline void f0_of(float note, int tune, float &f0, float &l2f0) {
    float nn = note + ((float)tune - 64.0f) * 0.375f;
    float l2 = (nn - 69.0f) * INV12;
    f0 = 440.0f * exp2_(l2);
    l2f0 = l2 + LOG2_440F;
}

// -- the bodies ------------------------------------------------------------------------------
struct Strike { float geom_e, inv_t60, matl, pos, tilt; };

struct Body {
    int kind = 0, n = 0;
    float L[MAXN], aj[MAXN], offs[MAXN], mult[MAXN], nfull = 0.0f;
    float y[MAXN], d[MAXN], drift[MAXN];
    float m[MAXN], k[MAXN], b[MAXN], phi[MAXN], amp[MAXN];
    float s2[MAXN], sw[MAXN], s2o[MAXN], swo[MAXN];
    bool has_s2 = false, has_prev = false;
    float og[MAXN], og_prev[MAXN];
    bool has_og = false, has_og_prev = false, ramp_og = false;
    float dsig = 0.0f, peak = 1e-9f, level = 1.0f, spatial = 0.5f, bias = 0.0f;

    void init(int kind_, float f0, XS &rng, float vary, const Strike &st) {
        kind = kind_;
        const float *L0 = LOG2R[kind];
        float split = SPLIT[kind];
        float eps[MODES], ajj[MODES], offsv[MODES], ajt[MODES];
        for (int i = 0; i < MODES; ++i) eps[i] = rng.bi() * (JS[i] * vary);
        eps[0] = eps[0] * 0.2f;
        float g2[MODES];
        for (int i = 0; i < MODES; ++i) g2[i] = rng.gauss();
        float sc = (0.10f * vary) * LOG2E;
        for (int i = 0; i < MODES; ++i) ajj[i] = exp2_(g2[i] * sc);
        if (kind == BODY_CHIME)
            for (int i = 0; i < 3; ++i) ajj[i] = ajj[i] * CHIME_W[i];
        if (split != 0.0f) {
            float r1[MODES], r2[MODES];
            for (int i = 0; i < MODES; ++i) r1[i] = rng.u01();
            for (int i = 0; i < MODES; ++i) {
                float ratio = exp2_(L0[i]);
                float beat = fmin_(((f0 * ratio) * split) * (0.6f + 0.8f * r1[i]), BEAT_MAX[kind]);
                offsv[i] = fmax_(beat, 0.15f);
            }
            for (int i = 0; i < MODES; ++i) r2[i] = rng.u01();
            for (int i = 0; i < MODES; ++i) ajt[i] = ajj[i] * (0.5f + 0.5f * r2[i]);
        }
        float Lj[MODES];
        for (int i = 0; i < MODES; ++i) Lj[i] = L0[i] + (eps[i] * (1.0f - 0.5f * eps[i])) * LOG2E;
        if (split != 0.0f) {
            float pair[MODES];
            for (int i = 0; i < MODES; ++i) pair[i] = ajj[i] + ajt[i];
            int sel[TWINS];
            twin_select(Lj, pair, f0, st, sel);
            bool folded[MODES];
            for (int i = 0; i < MODES; ++i) folded[i] = true;
            for (int t = 0; t < TWINS; ++t) folded[sel[t]] = false;
            for (int i = 0; i < MODES; ++i) {
                L[i] = Lj[i];
                aj[i] = folded[i] ? pair[i] : ajj[i];
                offs[i] = 0.0f;
                mult[i] = folded[i] ? 2.0f : 1.0f;
            }
            for (int t = 0; t < TWINS; ++t) {
                L[MODES + t] = Lj[sel[t]];
                aj[MODES + t] = ajt[sel[t]];
                offs[MODES + t] = offsv[sel[t]];
                mult[MODES + t] = 1.0f;
            }
            n = MODES + TWINS;
            nfull = (float)(2 * MODES);
        } else {
            for (int i = 0; i < MODES; ++i) {
                L[i] = Lj[i];
                aj[i] = ajj[i];
                offs[i] = 0.0f;
                mult[i] = 1.0f;
            }
            n = MODES;
            nfull = (float)MODES;
        }
        spatial = SPATIAL[kind];
        bias = RING_BIAS[kind];
        for (int i = 0; i < n; ++i) { y[i] = d[i] = drift[i] = amp[i] = 0.0f; }
        dsig = DSIG * vary;
        peak = 1e-9f;
        level = 1.0f;
        has_og = has_og_prev = ramp_og = false;
        has_s2 = has_prev = false;
    }

    void twin_select(const float *Lj, const float *pair, float f0, const Strike &st, int *sel) {
        float score[MODES];
        for (int i = 0; i < MODES; ++i) {
            float lr = st.geom_e * Lj[i];
            float ratio = exp2_(lr);
            float f = fmin_(fmax_(ratio * f0, F_LO), F_HI);
            float mt = fmin_(fmax_(st.matl + RING_BIAS[kind], 0.0f), 1.0f);
            float om = 1.0f - mt;
            float a1 = 4.0f * (om * om) + 0.015f;
            float a2 = 2.0f * ((om * om) * om) + 0.002f;
            float fk = f * 0.001f;
            float inv = fmin_(st.inv_t60 * ((fk * a2 + a1) * fk + 1.0f), 1000.0f);
            float sp = SPATIAL[kind] == 1.0f ? ratio : exp2_(lr * SPATIAL[kind]);
            float sh = fabs_(sin_t(frac(sp * (st.pos * 0.5f))));
            float s = ((sh * exp2_(lr * (-0.7f * st.tilt))) * pair[i]) * rsqrt(inv);
            bool alive = (ratio * f0 < F_HI) && (ratio * f0 > F_LO);
            score[i] = alive ? s : -1.0f;
        }
        bool taken[MODES] = {false};
        for (int t = 0; t < TWINS; ++t) {
            int best = -1;
            for (int i = 0; i < MODES; ++i) {
                if (taken[i]) continue;
                if (best < 0 || score[i] > score[best]) best = i;
            }
            taken[best] = true;
        }
        int c = 0;
        for (int i = 0; i < MODES; ++i) if (taken[i]) sel[c++] = i;
    }

    void coeffs(XS &rng, float f0b, float geom_e, float inv_t60, float matl, float loss, float pos,
                float tilt, float mexp) {
        float inc[MAXN];
        for (int i = 0; i < n; ++i) inc[i] = rng.bi() * dsig;
        float mt = fmin_(fmax_(matl + bias, 0.0f), 1.0f);
        float om = 1.0f - mt;
        float a1 = 4.0f * (om * om) + 0.015f;
        float a2 = 2.0f * ((om * om) * om) + 0.002f;
        float shape[MAXN], lrv[MAXN];
        bool alive[MAXN];
        has_prev = has_s2;
        for (int i = 0; i < n; ++i) {
            drift[i] = drift[i] * 0.995f + inc[i];
            float lr = geom_e * L[i];
            lrv[i] = lr;
            float rg = exp2_(lr);
            float ratio = rg * (1.0f + drift[i]);
            float f = ratio * f0b + offs[i];
            alive[i] = (f < F_HI) && (f > F_LO);
            f = fmin_(fmax_(f, F_LO), F_HI);
            float h = f * H_SCALE;
            float s = sinp(h);
            float swv = (2.0f * s) * cosp(h);
            float fk = f * 0.001f;
            float den = (fk * a2 + a1) * fk + 1.0f;
            float lf = loss * fmax_(ratio - 1.0f, 0.0f) + 1.0f;
            float inv = fmin_((inv_t60 * den) * lf, 1000.0f);
            if (!alive[i]) inv = 1000.0f;
            float q = -expm1_(inv * C_DECAY);
            float r = 1.0f - q;
            m[i] = q * (2.0f - q);
            float s2v = s * s;
            k[i] = q * q + 4.0f * (r * s2v);
            s2o[i] = s2[i];
            swo[i] = sw[i];
            s2[i] = s2v;
            sw[i] = swv;
            float sp = spatial == 1.0f ? ratio : exp2_(lr * spatial);
            float sh = fabs_(sin_t(frac(sp * (pos * 0.5f))));
            float tw = exp2_(lr * (-0.7f * tilt));
            shape[i] = (sh * tw) * aj[i];
        }
        has_s2 = true;
        float S = shape[0];
        for (int i = 1; i < n; ++i) S = S + shape[i];
        float gn = OUTL * recip(fmax_(S, 1e-6f));
        for (int i = 0; i < n; ++i) {
            amp[i] = alive[i] ? shape[i] * gn : 0.0f;
            b[i] = amp[i] * sw[i];
            phi[i] = amp[i] * PHI_SCALE;
        }
        has_og_prev = has_og;
        if (has_og) std::memcpy(og_prev, og, sizeof(float) * n);
        if (mexp > 0.0f) {
            float we[MAXN];
            for (int i = 0; i < n; ++i) we[i] = exp2_(lrv[i] * mexp);
            float W = we[0] * mult[0];
            for (int i = 1; i < n; ++i) W = W + we[i] * mult[i];
            float rw = recip(W);
            for (int i = 0; i < n; ++i) og[i] = we[i] * (nfull * rw);
            has_og = true;
        } else {
            has_og = false;
        }
    }

    // the sum over modes of one sample (mode order), og-weighted
    inline float outsum(const float *yy, int j) const {
        if (has_og) {
            if (ramp_og && has_og_prev) {
                float r = (float)(j + 1) * INVBLOCK;
                float acc = 0.0f;
                for (int i = 0; i < n; ++i) {
                    float w = og_prev[i] + (og[i] - og_prev[i]) * r;
                    acc = (i == 0) ? yy[i] * w : acc + yy[i] * w;
                }
                return acc;
            }
            float acc = yy[0] * og[0];
            for (int i = 1; i < n; ++i) acc = acc + yy[i] * og[i];
            return acc;
        }
        float acc = yy[0];
        for (int i = 1; i < n; ++i) acc = acc + yy[i];
        return acc;
    }

    void run(const float *x, float *out) {
        for (int j = 0; j < BLOCK; ++j) {
            if (x) {
                float xj = x[j];
                for (int i = 0; i < n; ++i) {
                    float dd = d[i] - m[i] * d[i];
                    dd = dd - k[i] * y[i];
                    dd = dd + b[i] * xj;
                    d[i] = dd;
                    y[i] = y[i] + dd;
                }
            } else {
                for (int i = 0; i < n; ++i) {
                    float dd = d[i] - m[i] * d[i];
                    dd = dd - k[i] * y[i];
                    d[i] = dd;
                    y[i] = y[i] + dd;
                }
            }
            out[j] = outsum(y, j);
        }
        done(out);
    }

    template <class Force>
    void run_driven(Force &force, const float *x, float *out) {
        for (int j = 0; j < BLOCK; ++j) {
            float v = phi[0] * d[0];
            for (int i = 1; i < n; ++i) v = v + phi[i] * d[i];
            v = v * 40.0f;
            float fj = force(j, v);
            if (x) fj = fj + x[j];
            for (int i = 0; i < n; ++i) {
                float dd = d[i] - m[i] * d[i];
                dd = dd - k[i] * y[i];
                dd = dd + b[i] * fj;
                d[i] = dd;
                y[i] = y[i] + dd;
            }
            out[j] = outsum(y, j);
        }
        done(out);
    }

    void done(const float *out) {
        float pk = 0.0f;
        for (int j = 0; j < BLOCK; ++j) pk = fmax_(pk, fabs_(out[j]));
        peak = fmax_(peak, pk);
        level = pk * recip(peak);
    }

    void flush() {
        for (int i = 0; i < n; ++i)
            if (fabs_(y[i]) < FLOORF && fabs_(d[i]) < FLOORF) y[i] = d[i] = 0.0f;
    }

    float energy() const {
        float a = 0.0f, c = 0.0f;
        for (int i = 0; i < n; ++i) {
            a = fmax_(a, fabs_(y[i]));
            c = fmax_(c, fabs_(y[i] - d[i]));
        }
        return a + c;
    }

    void retune() {
        if (!has_prev) return;
        for (int i = 0; i < n; ++i) {
            float t = d[i] - (2.0f * s2o[i]) * y[i];
            d[i] = (2.0f * s2[i]) * y[i] + t * (sw[i] * recip(fmax_(swo[i], 1e-6f)));
        }
    }

    void scale_y(float s) {
        for (int i = 0; i < n; ++i) {
            float t = (1.0f - s) * y[i];
            y[i] = y[i] - t;
            d[i] = d[i] - t;
        }
    }

    bool ok() const {
        for (int i = 0; i < n; ++i) {
            if (!std::isfinite(y[i]) || !std::isfinite(d[i])) return false;
            if (!(fabs_(y[i]) < GUARD) || !(fabs_(d[i]) < GUARD)) return false;
        }
        return true;
    }
};

// -- the exciters ----------------------------------------------------------------------------
struct Smear {
    float ca = 1.0f;
    float s[4] = {0, 0, 0, 0};
    float operator()(float x) {
        for (int i = 0; i < 4; ++i) {
            s[i] = fl(s[i] + (x - s[i]) * ca);
            x = s[i];
        }
        return x;
    }
};

struct TZ {
    float mph = 0.0f, cph = 0.0f, ei = 1.0f, eo = 1.0f;
    float ci = 0.0f, fm = 0.0f, d6 = 0.0f, ki = 0.0f, ko = 0.0f;
    void setup(float f0, float ratio, float depth, float tau) {
        ci = f0 * INVR;
        fm = (f0 * ratio) * INVR;
        d6 = depth * 6.0f;
        float it = recip(tau);
        ki = onepole_inv(it);
        ko = onepole_inv(recip(3.0f * tau + 0.005f));
    }
    float operator()() {
        float mv = sin_t(mph);
        mph = frac(mph + fm);
        float inc = ci + ((ei * d6) * mv) * fm;
        cph = frac(cph + inc);
        float y = sin_t(cph) * eo;
        ei = fl(ei * ki);
        eo = fl(eo * ko);
        return y;
    }
};

struct Gen {
    int kind = STRIKE;
    float vel = 1.0f;
    XS rng;
    long k = 0;
    float width = 0.0f, pinv = 0.0f, en = 1.0f;
    int pn = 2;
    bool has_burst = false;
    TZ burst, tz;
    Smear grain;
    float ph = 0.0f, pe = 1.0f, ep = 1.0f;
    const BC *c = nullptr;
    float f0 = 0.0f;
    float kn = 0.0f, cn = 0.0f, pd = 0.0f, kd = 0.0f, ga = 0.0f, inc0 = 0.0f, dinc = 0.0f, kep = 0.0f;

    void init(int kind_, const int *q, float f0_, float vel_, uint32_t seed) {
        kind = kind_;
        vel = vel_;
        rng.init(seed);
        k = 0;
        float hard = u(q[P_HARD]) * (0.5f + 0.5f * vel);
        width = 4.0e-3f * exp2_(hard * LG_WIDTH);
        int p = (int)(width * RATEF);
        pn = p > 2 ? p : 2;
        pinv = recip((float)pn);
        en = 1.0f;
        has_burst = q[P_TZFM] > 0 && kind != TZFM;
        burst = TZ();
        tz = TZ();
        if (kind == DUST) {
            int dn = (int)(fmax_(width, 1e-4f) * RATEF);
            if (dn < 2) dn = 2;
            grain = Smear();
            grain.ca = 1.0f - smear_pole(HANN_VARF * ((float)dn * (float)dn), 4);
        }
        ph = 0.0f;
        pe = 1.0f;
        ep = 1.0f;
        f0 = f0_;
    }

    void setup() {
        float dec = c->dec;
        if (has_burst) burst.setup(f0, c->side, c->utz, fmin_(dec, 0.03f));
        if (kind == STRIKE) {
            kn = onepole_inv(recip(dec * 0.25f));
            cn = 0.15f * c->udust;
        } else if (kind == DUST) {
            float dens = expmap(c->DUST, 5.0f, LG_DUST) * (0.5f + vel);
            pd = dens * INVR;
            kd = onepole_inv(recip(dec));
            ga = 2.0f * rsqrt(fmax_(dens * dec, 1.0f));
        } else if (kind == PULSE) {
            float ub3 = c->ubend * 3.0f;
            float c2 = (f0 * 2.0f) * INVR;
            float i0 = c2 * exp2_(ub3 * pe);
            pe = pe * K_PE;
            float i1 = c2 * exp2_(ub3 * pe);
            inc0 = i0;
            dinc = (i1 - i0) * INVBLOCK;
            kep = onepole_inv(recip(dec * 0.3f));
        } else if (kind == TZFM) {
            tz.setup(f0, c->side, fmax_(c->utz, 0.15f), dec);
        }
    }

    float operator()() {
        if (k % BLOCK == 0) setup();
        float pulse = 0.0f, rest = 0.0f;
        if (kind == STRIKE) {
            if (k < pn) pulse = (1.0f - cos_t((float)k * pinv)) * pinv;
            if (cn != 0.0f) {
                rest = (rng.gauss() * en) * cn;
                en = fl(en * kn);
            }
        } else if (kind == DUST) {
            float ev = 0.0f;
            if (rng.u01() < pd * en) ev = rng.bi() * ga;
            en = fl(en * kd);
            rest = grain(ev);
        } else if (kind == PULSE) {
            ph = frac(ph + (inc0 + (float)(k % BLOCK) * dinc));
            rest = sin_t(ph) * ep;
            ep = fl(ep * kep);
        } else {
            rest = tz();
        }
        float x = (pulse + rest) * TYPE_GAIN[kind];
        if (has_burst) x = x + burst() * TYPE_GAIN[TZFM];
        k = k + 1;
        return x * vel;
    }
};

static inline float bow_table(float dv, float slope) {
    float y = fabs_(dv * slope) + 0.75f;
    float y2 = y * y;
    float y4 = fmin_(fmax_(y2 * y2, BOW_LO), BOW_HI);
    float kq = fmin_(fmax_(0.25f * recip(y4), 0.0025f), 0.245f);
    return dv * kq;
}

struct Exciter {
    int kind = STRIKE;
    float vel = 1.0f;
    XS *rng = nullptr;
    long t = 0;
    float atk = 0.0f;
    bool driven = false, done = false;
    long n_end = 1;
    bool has_latched = false;
    BC latched;
    float latched_f0 = 0.0f;
    Gen gen, lag;
    bool has_lag = false;
    long am = 0;
    float cS = 0.0f, fmf = 0.0f, fm2 = 0.0f;
    long acc0[2];
    float acc[2][3];
    float oq = 1.0f, hold = 1.0f, sph = 0.0f, grit = 0.0f, breath = 0.0f;
    float press[BLOCK], drive[BLOCK];
    int npress = 0;   // 0: no pressure this block (None)
    BC cur;           // the controls gen_block ran on (for the generators' pointers)

    void init(int kind_, const int *q, float f0, float vel_, XS &rng_, uint32_t seed) {
        kind = kind_;
        vel = vel_;
        rng = &rng_;
        t = 0;
        atk = q[P_ATK] > 0 ? expmap(q[P_ATK], 0.0002f, LG_ATK) : 0.0f;
        driven = (kind == BOW || kind == BLOW);
        done = false;
        n_end = 1;
        has_latched = false;
        has_lag = false;
        if (!driven) {
            gen.init(kind, q, f0, vel, seed);
            if (atk > 0.0f) {
                long mm = (long)(atk * RATEF);
                if (mm < 2) mm = 2;
                am = mm;
                cS = 6.0f * recip(((float)(mm - 1) * (float)mm) * (float)(2 * mm - 1));
                fmf = (float)mm;
                fm2 = (float)mm * (float)mm;
                acc0[0] = 0;
                acc0[1] = mm;
                for (int a = 0; a < 2; ++a) acc[a][0] = acc[a][1] = acc[a][2] = 0.0f;
                lag.init(kind, q, f0, vel, seed);
                has_lag = true;
            }
        }
        oq = 1.0f;
        hold = 1.0f;
        if (kind == BOW) {
            float r = rng->u01();
            sph = (r * 6.0f) * INV_2PI;
            grit = 0.0f;
        }
        if (kind == BLOW) breath = 0.0f;
        npress = 0;
    }

    // this block's excitation into out (impulsive), or press/drive (driven)
    void gen_block(const BC &cin, float f0in, float *out) {
        long k0 = t;
        const BC *c = &cin;
        float f0 = f0in;
        if (atk > 0.0f && !driven) {
            if (!has_latched) {
                latched = cin;
                latched_f0 = f0in;
                has_latched = true;
            }
            c = &latched;
            f0 = latched_f0;
        }
        n_end = (long)(((c->dec * 4.0f + 0.01f) + atk) * RATEF);
        for (int j = 0; j < BLOCK; ++j) out[j] = 0.0f;
        if (done || k0 >= n_end) {
            done = true;
            npress = 0;
            t = k0 + BLOCK;
            return;
        }
        if (driven) {
            run_driven(*c, k0);
            return;
        }
        cur = *c;
        gen.c = &cur;
        gen.f0 = f0;
        if (has_lag) {
            lag.c = &cur;
            lag.f0 = f0;
        }
        for (int j = 0; j < BLOCK; ++j) {
            long k = k0 + j;
            if (k >= n_end) {
                done = true;
                break;
            }
            float x = gen();
            if (!has_lag) {
                out[j] = x;
                continue;
            }
            long m = am;
            float xo = k >= m ? lag() : 0.0f;
            float y = 0.0f;
            for (int a = 0; a < 2; ++a) {
                float *A = acc[a];
                if (k == acc0[a] + 2 * m) {
                    acc0[a] = k;
                    A[0] = A[1] = A[2] = 0.0f;
                }
                if (k < acc0[a]) continue;
                A[2] = (A[2] + 2.0f * A[1]) + A[0];
                A[1] = A[1] + A[0];
                A[0] = A[0] + x;
                if (k - m >= acc0[a]) {
                    A[0] = A[0] - xo;
                    A[1] = A[1] - xo * fmf;
                    A[2] = A[2] - xo * fm2;
                }
                if (acc0[a] == 0 || k >= acc0[a] + m) y = A[2];
            }
            out[j] = y * cS;
        }
        t = k0 + BLOCK;
        if (t >= n_end) done = true;
    }

    void run_driven(const BC &c, long k0) {
        bool bow = kind == BOW;
        float decR = c.dec * RATEF;
        float kh = bow ? KH_BOW : KH_BLOW;
        float pv = bow ? (0.2f + 0.8f * vel) : (0.4f + 0.6f * vel);
        bool has_ko = atk > 0.0f;
        float ko = has_ko ? onepole_inv(recip(atk * THIRD)) : 0.0f;
        float pg = 0, ud = 0, sinc = 0, a = 0, ca = 0, bg = 0;
        for (int j = 0; j < BLOCK; ++j) press[j] = drive[j] = 0.0f;
        if (bow) {
            pg = expmap(c.DUST, 20.0f, LG_GRIT) * INVR;
            ud = c.udust;
            sinc = 0.7f * INVR;
        } else {
            float cut = 400.0f * exp2_(c.uhard * LG_30);
            a = exp2_(cut * BLOW_W);
            ca = 1.0f - a;
            bg = 0.3f + 0.7f * c.udust;
        }
        int n = 0;
        for (int j = 0; j < BLOCK; ++j) {
            long k = k0 + j;
            if (k >= n_end) break;
            float on;
            if (!has_ko) {
                on = fmin_((float)k * K_RAMP, 1.0f);
            } else {
                on = 1.0f - oq;
                oq = oq * ko;
            }
            if ((float)k > decR) hold = hold * kh;
            float pr = (on * hold) * pv;
            if (bow) {
                float sp = 0.25f + 0.15f * sin_t(sph);
                sph = frac(sph + sinc);
                if (ud != 0.0f) {
                    float ev = 0.0f;
                    if (rng->u01() < pg) ev = rng->bi();
                    grit = grit * 0.6f + ev;
                    sp = sp * (1.0f + (grit * ud) * 0.5f);
                } else {
                    grit = 0.0f;
                }
                press[j] = pr;
                drive[j] = sp;
            } else {
                float gz = ca * rng->gauss();
                breath = gz + a * breath;
                press[j] = pr * (1.0f + breath * bg);
            }
            n = j + 1;
        }
        npress = n;
        t = k0 + BLOCK;
        if (t >= n_end) done = true;
    }

    // the force's parameters for this block (false: no force)
    bool force_setup(const BC &c, float g, float &p1, float &p2) const {
        if (!driven || npress == 0 || g < 1e-4f) return false;
        if (kind == BOW) {
            p1 = 1.5f + 6.0f * (1.0f - c.hard);                 // slope
            p2 = (4.0f * g) * (0.5f + 1.5f * c.hard);           // gb
        } else {
            p1 = 0.03f * g;                                      // gf
            p2 = 0.0f;
        }
        return true;
    }

    float force(int j, float v, float p1, float p2) const {
        if (j >= npress) return 0.0f;
        if (kind == BOW) return (bow_table(drive[j] - v, p1) * press[j]) * p2;
        float dp = press[j] - v * 0.6f;
        return (dp * fmax_(0.0f, 1.0f - fabs_(dp) * 0.9f)) * p1;
    }

    void flush() {
        if (driven) {
            oq = fl(oq);
            hold = fl(hold);
            if (kind == BOW) grit = fl(grit);
            else breath = fl(breath);
        }
    }
};

// -- clicks ----------------------------------------------------------------------------------
struct Click {
    int kind = SNAP;
    XS *rng = nullptr;
    long k = 0, len = 0;
    float level = 0.0f;
    int n = 3;
    float ninv = 0, gain = 0, inc = 0, ke = 0, prev = 0, kp = 0, pe = 1, ph = 0, e = 1;

    void init(int kind_, float hard, float level_, XS &rng_) {
        kind = kind_;
        rng = &rng_;
        k = 0;
        level = level_;
        float h1 = 1.0f - hard;
        if (kind == SNAP) {
            int nn = (int)((0.00015f + 0.0006f * h1) * RATEF);
            n = nn > 3 ? nn : 3;
            ninv = recip((float)n);
            len = n + 1;
            gain = (float)n * 0.35f;
        } else if (kind == TICK) {
            len = 192;
            inc = (2500.0f + 3000.0f * hard) * INVR;
            ke = onepole_inv(recip(0.0004f + 0.0008f * h1));
        } else if (kind == NOISE) {
            len = 576;
            prev = 0.0f;
            ke = onepole_inv(recip(0.0008f + 0.003f * h1));
        } else {
            len = 2880;
            ke = onepole_inv(recip(0.012f + 0.01f * h1));
            kp = KP_THUMP;
            pe = 1.0f;
        }
        ph = 0.0f;
        e = 1.0f;
    }
    bool done() const { return k >= len; }
    float hann(long j) const {
        if (j < 0 || j >= n) return 0.0f;
        return 0.5f - 0.5f * cos_t((float)j * ninv);
    }
    void block(float *out) {   // adds into out
        for (int j = 0; j < BLOCK; ++j) {
            if (k >= len) break;
            float x;
            if (kind == SNAP) {
                x = (hann(k) - hann(k - 1)) * gain;
            } else if (kind == TICK) {
                x = sin_t(ph) * e;
                ph = frac(ph + inc);
                e = e * ke;
            } else if (kind == NOISE) {
                float g = rng->gauss();
                x = ((g - prev) * e) * 0.5f;
                prev = g;
                e = e * ke;
            } else {
                ph = frac(ph + (40.0f + 50.0f * pe) * INVR);
                pe = pe * kp;
                x = sin_t(ph) * e;
                e = e * ke;
            }
            out[j] = out[j] + x * level;
            k = k + 1;
        }
    }
};

// -- after the bodies ------------------------------------------------------------------------
template <int N>
struct SVF {
    float ic1[N], ic2[N], a1[N], a2[N], a3[N], kk = 1.0f;
    void reset() { for (int i = 0; i < N; ++i) ic1[i] = ic2[i] = 0.0f; }
    void set(const float *fc, float q) {
        kk = 1.0f * recip(q);
        for (int i = 0; i < N; ++i) {
            float f = fmin_(fc[i], F_HI);
            float h = f * H_SCALE;
            float g = sinp(h) * recip(cosp(h));
            a1[i] = recip(1.0f + g * (g + kk));
            a2[i] = g * a1[i];
            a3[i] = g * a2[i];
        }
    }
    inline void tick(int i, float x, float &v1, float &v2) {
        float v3 = x - ic2[i];
        v1 = a1[i] * ic1[i] + a2[i] * v3;
        v2 = (ic2[i] + a2[i] * ic1[i]) + a3[i] * v3;
        ic1[i] = 2.0f * v1 - ic1[i];
        ic2[i] = 2.0f * v2 - ic2[i];
    }
    void flush() {
        for (int i = 0; i < N; ++i) {
            if (fabs_(ic1[i]) < FLOORF) ic1[i] = 0.0f;
            if (fabs_(ic2[i]) < FLOORF) ic2[i] = 0.0f;
        }
    }
};

struct Shifter {
    float in1 = 0, in2 = 0, iprev = 0, ph = 0;
    float ay1[4] = {0}, ay2[4] = {0}, by1[4] = {0}, by2[4] = {0};
    void reset() { *this = Shifter(); }
    static inline float chain(float x, float in2, const float *c2, float *y1, float *y2) {
        float old2 = in2;
        for (int i = 0; i < 4; ++i) {
            float y = c2[i] * (x + y2[i]) - old2;
            old2 = y2[i];
            y2[i] = y1[i];
            y1[i] = y;
            x = y;
        }
        return x;
    }
    float tick(float x, float inc) {
        float ya = chain(x, in2, SH_A2, ay1, ay2);
        float yb = chain(x, in2, SH_B2, by1, by2);
        in2 = in1;
        in1 = x;
        float i = iprev;
        iprev = ya;
        float y = i * cos_t(ph) + yb * sin_t(ph);
        ph = frac(ph + inc);
        return y;
    }
    void flush() {
        for (int i = 0; i < 4; ++i) {
            if (fabs_(ay1[i]) < FLOORF) ay1[i] = 0.0f;
            if (fabs_(ay2[i]) < FLOORF) ay2[i] = 0.0f;
            if (fabs_(by1[i]) < FLOORF) by1[i] = 0.0f;
            if (fabs_(by2[i]) < FLOORF) by2[i] = 0.0f;
        }
    }
};

struct Water {
    SVF<1> lp1, lp2;
    SVF<6> an, car;
    float e[6], es[6];
    bool use[6];
    float ss = 0, swv = 0;
    bool has_g = false, has_amt = false, has_key = false;
    float g = 0, amt = 0, key_f0 = 0, key_amt = 0;

    void reset() {
        lp1.reset(); lp2.reset(); an.reset(); car.reset();
        for (int i = 0; i < 6; ++i) e[i] = es[i] = 0.0f;
        ss = swv = 0.0f;
        has_g = has_amt = has_key = false;
    }
    bool key_is(float f0, float a) const { return has_key && key_f0 == f0 && key_amt == a; }
    void set(float f0, float l2f0, float a) {
        if (key_is(f0, a)) return;
        has_key = true;
        key_f0 = f0;
        key_amt = a;
        float lf = f0 * 1.8f;
        lp1.set(&lf, SQRT_HALF);
        lp2.set(&lf, SQRT_HALF);
        float span = fmin_(5.0f, LG_WATER - l2f0);
        float b0 = f0 * 2.2f;
        float fc[6], fcc[6];
        for (int i = 0; i < 6; ++i) {
            fc[i] = b0 * exp2_(span * WK7[i + 1]);
            use[i] = fc[i] <= WATER_TOP;
            if (!use[i]) fc[i] = 1000.0f;
        }
        an.set(fc, 3.0f);
        float sc = 1.04f + 0.04f * a;
        for (int i = 0; i < 6; ++i) fcc[i] = fc[i] * sc;
        car.set(fcc, 14.0f);
    }
    void block(const float *z, float a1, float *out) {
        float body[BLOCK], skin[BLOCK], wet[BLOCK];
        for (int j = 0; j < BLOCK; ++j) {
            float v1, l1, l2;
            lp1.tick(0, z[j], v1, l1);
            lp2.tick(0, l1, v1, l2);
            float bb = l2;
            float s = z[j] - bb;
            body[j] = bb;
            skin[j] = s;
            float acc = 0.0f;
            for (int i = 0; i < 6; ++i) {
                float va, vb, ca, cb;
                an.tick(i, s, va, vb);
                float anv = an.kk * va;
                car.tick(i, s, ca, cb);
                float cv = car.kk * ca;
                float av = fabs_(anv);
                float k1 = av > e[i] ? W_KA1 : W_KR1;
                e[i] = av + (e[i] - av) * k1;
                float k2 = av > es[i] ? W_KA2 : W_KR2;
                es[i] = av + (es[i] - av) * k2;
                float ratio = e[i] * recip(es[i] + 1e-6f);
                float gate = fmin_(ratio * ratio, 6.0f);
                float w = use[i] ? cv * gate : 0.0f;
                acc = (i == 0) ? w : acc + w;
            }
            wet[j] = acc;
        }
        for (int j = 0; j < BLOCK; ++j) {
            ss = ss + skin[j] * skin[j];
            swv = swv + wet[j] * wet[j];
        }
        float gg = swv > 1e-20f ? sqrt_(ss * recip(swv)) : 1.0f;
        gg = fmin_(gg, W_GMAX);
        float g0 = has_g ? g : gg;
        g = gg;
        has_g = true;
        float a0 = has_amt ? amt : a1;
        amt = a1;
        has_amt = true;
        for (int j = 0; j < BLOCK; ++j) {
            float st = (float)(j + 1) * INVBLOCK;
            float ramp = g0 + (gg - g0) * st;
            float a = (a0 == a1) ? a1 : a0 + (a1 - a0) * st;
            out[j] = (body[j] + skin[j] * (1.0f - a)) + (wet[j] * ramp) * a;
        }
    }
    void flush() {
        lp1.flush(); lp2.flush(); an.flush(); car.flush();
        for (int i = 0; i < 6; ++i) {
            if (fabs_(e[i]) < FLOORF) e[i] = 0.0f;
            if (fabs_(es[i]) < FLOORF) es[i] = 0.0f;
        }
    }
};

enum Stage { ST_RING, ST_SHIFT, ST_FORM, ST_WATR, ST_FOLD, ST_CHAR, ST_VCA, NSTAGE };
static inline bool ramped(int st) { return st == ST_RING || st == ST_FOLD || st == ST_CHAR || st == ST_VCA; }

// -- the voice -------------------------------------------------------------------------------
struct Voice {
    uint32_t seed = 1;
    bool on = false, pending = false, fading = false;
    float p_note = 60.0f, p_vel = 1.0f;
    int faults = 0;
    long t = 0, blocks = 0;

    float note = 60.0f, vel = 1.0f, vary = 0.0f;
    XS rng;
    Body a, b;
    bool has_b = false;
    float bratio = 1.0f;
    Exciter ea, eb;
    bool has_clk = false;
    Click clk;
    float cenv = 1, lenv = 1, menv = 1, venv = 1;
    float ob_prev[BLOCK];
    float rattle = 0, dc_x = 0, dc_y = 0, lim = 0, ring_ph = 0, morph = 0;
    float lv[NSTAGE];
    bool has_par[NSTAGE];
    float par[NSTAGE];
    bool has_pitch_prev = false;
    float pitch_prev_l2 = 0, pitch_prev_ge = 0;
    bool has_shift = false, has_form = false, has_form_v = false, has_form_wet = false, has_water = false;
    Shifter shift;
    SVF<2> form;
    float form_v = 0, form_wet = 0, form_w0 = 0;
    Water water;

    explicit Voice(uint32_t seed_ = 1) : seed(seed_) {}

    bool idle() const { return !on && !pending; }
    void rest() { on = false; pending = false; fading = false; }

    void note_on(float n, float v) {
        p_note = std::isfinite(n) ? n : 0.0f;
        p_vel = std::isnan(v) ? 1.0f : (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
        pending = true;
        fading = on;
    }

    void strike(const int *q) {
        pending = false;
        note = p_note;
        vel = p_vel;
        rng.init(seed);
        vary = u(q[P_VARY]) * 3.2f;
        float f0, l2f0;
        f0_of(note, q[P_TUNE], f0, l2f0);
        int ka = choice_n(q[P_BODA], 9);
        int kb = choice_n(q[P_BODB], 10);
        BC c0;
        block_controls(q, vel, c0);
        Strike st{c0.geom_e, c0.inv_t60, c0.matl, c0.pos, c0.tilt};
        a.init(ka, f0, rng, vary, st);
        bratio = B_RATIO[kb];
        Strike stb{c0.geom_e, c0.inv_t60, c0.matl, c0.pos * 0.73f, c0.tilt};
        has_b = kb != BODY_OFF;
        if (has_b) b.init(kb, f0 * bratio, rng, vary, stb);
        int ta = choice_n(q[P_TYPA], 6);
        int tb = choice_n(q[P_TYPB], 6);
        ea.init(ta, q, f0, vel, rng, seed * 4 + 1);
        eb.init(tb, q, f0, vel, rng, seed * 4 + 2);
        float hard = u(q[P_HARD]) * (0.5f + 0.5f * vel);
        has_clk = q[P_CLIK] > 0;
        if (has_clk) clk.init(choice_n(q[P_CTYP], 4), hard, (u(q[P_CLIK]) * vel) * 0.5f, rng);
        t = 0;
        cenv = lenv = menv = venv = 1.0f;
        for (int j = 0; j < BLOCK; ++j) ob_prev[j] = 0.0f;
        rattle = dc_x = dc_y = lim = ring_ph = 0.0f;
        for (int s = 0; s < NSTAGE; ++s) { lv[s] = 0.0f; has_par[s] = false; par[s] = 0.0f; }
        has_pitch_prev = false;
        has_shift = has_form = has_form_v = has_form_wet = has_water = false;
        on = true;
        fading = false;
        blocks = 0;
    }

    // one block of 32 samples
    void block(const int *qin, float *out) {
        int q[NPARAM];
        for (int i = 0; i < NPARAM; ++i) q[i] = clampi(qin[i]);
        if (fading && on) {
            run(q, out);
            fading = false;
            on = false;
            for (int j = 0; j < BLOCK; ++j) out[j] = out[j] * (1.0f - (float)(j + 1) * INVBLOCK);
            return;
        }
        if (pending) strike(q);
        if (!on) {
            for (int j = 0; j < BLOCK; ++j) out[j] = 0.0f;
            return;
        }
        run(q, out);
    }

  private:
    struct ForceFn {
        const Exciter *ea, *eb;
        bool fa, fb;
        float a1, a2, b1, b2;
        float operator()(int j, float v) const {
            if (fa && fb) return ea->force(j, v, a1, a2) + eb->force(j, v, b1, b2);
            if (fa) return ea->force(j, v, a1, a2);
            return eb->force(j, v, b1, b2);
        }
    };

    void run(const int *q, float *out) {
        BC c;
        block_controls(q, vel, c);
        float f0, l2f0;
        f0_of(note, q[P_TUNE], f0, l2f0);
        float x[BLOCK], s[BLOCK];
        for (int j = 0; j < BLOCK; ++j) x[j] = 0.0f;
        bool has_x = false;
        Exciter *es[2] = {&ea, &eb};
        float gs[2] = {c.ga, c.gb};
        for (int i = 0; i < 2; ++i) {
            Exciter *e = es[i];
            if (e->done) {
                e->npress = 0;
                continue;
            }
            e->gen_block(c, f0, s);
            if (!e->driven) {
                for (int j = 0; j < BLOCK; ++j) x[j] = x[j] + s[j] * gs[i];
                has_x = true;
            }
        }
        bool exc_done = ea.done && eb.done;
        float ch = c.chirp * cenv;
        cenv = fl(cenv * K_CHIRP);
        float bend = exp2_((c.bend * a.level + ch) * INV12);
        float gl = lenv;
        lenv = fl(lenv * onepole_inv32(fmin_(c.inv_t60 * 4.0f, 100.0f)));
        float loss = c.lpg * (1.0f - gl);
        int xmod = c.xmod;
        float back = c.fdbk + (xmod == TRADE ? c.coup : 0.0f);
        if (has_b && back > 0.0f) {
            bool any = false;
            for (int j = 0; j < BLOCK; ++j) {
                x[j] = x[j] + (tanh_r(ob_prev[j] * 2.0f) * back) * 0.08f;
                if (ob_prev[j] != 0.0f) any = true;
            }
            has_x = has_x || any;
        }
        bool retune = has_pitch_prev && (l2f0 != pitch_prev_l2 || c.geom_e != pitch_prev_ge);
        has_pitch_prev = true;
        pitch_prev_l2 = l2f0;
        pitch_prev_ge = c.geom_e;
        float mexp = 0.0f;
        bool has_m0 = blocks != 0;
        float m0 = morph;
        morph = blocks == 0 ? c.morph : morph + (c.morph - morph) * 0.25f;
        if (morph < 1e-6f) morph = 0.0f;
        bool rog = has_m0 && m0 != morph;
        a.ramp_og = rog;
        if (has_b) b.ramp_og = rog;
        if (morph > 0.0f) {
            float prog = 1.0f - menv;
            mexp = (morph * prog) * 0.5f;
        }
        menv = fl(menv * onepole_inv32(fmin_(c.inv_t60 * INV_035, 20.0f)));
        a.coeffs(rng, f0 * bend, c.geom_e, c.inv_t60, c.matl, loss, c.pos, c.tilt, mexp);
        if (retune) a.retune();
        ForceFn ff{&ea, &eb, false, false, 0, 0, 0, 0};
        ff.fa = ea.force_setup(c, c.ga, ff.a1, ff.a2);
        ff.fb = eb.force_setup(c, c.gb, ff.b1, ff.b2);
        float oa[BLOCK], ob[BLOCK], bz[BLOCK], y[BLOCK];
        if (ff.fa || ff.fb) a.run_driven(ff, has_x ? x : nullptr, oa);
        else a.run(has_x ? x : nullptr, oa);
        bool has_bz = c.buzz > 0.0f;
        if (has_bz) {
            float gap = 0.25f * (1.0f - c.buzz) + 0.004f;
            float nz[BLOCK];
            for (int j = 0; j < BLOCK; ++j) nz[j] = rng.gauss();
            float prv = rattle;
            rattle = nz[BLOCK - 1];
            for (int j = 0; j < BLOCK; ++j) {
                float ex = fmax_(fabs_(oa[j]) - gap, 0.0f);
                float hp = nz[j] - prv;
                prv = nz[j];
                float sg = oa[j] > 0.0f ? 1.0f : (oa[j] < 0.0f ? -1.0f : 0.0f);
                bz[j] = ((hp * ex) * 0.8f + (sg * ex) * 0.5f) * c.buzz;
            }
        }
        for (int j = 0; j < BLOCK; ++j) ob[j] = 0.0f;
        if (has_b) {
            float inv_tb = c.inv_t60;
            float bb = exp2_((c.bend * b.level + ch) * INV12);
            float xb[BLOCK];
            for (int j = 0; j < BLOCK; ++j) xb[j] = has_bz ? x[j] + bz[j] * 0.5f : x[j];
            bool has_xb = has_x || has_bz;
            float coup = c.coup;
            if (xmod == EXCITE || xmod == TRADE) {
                bool any = false;
                for (int j = 0; j < BLOCK; ++j) {
                    xb[j] = (1.0f - coup) * xb[j] + (coup * fmin_(fmax_(oa[j], -1.0f), 1.0f)) * 0.5f;
                    if (oa[j] != 0.0f) any = true;
                }
                has_xb = has_xb || (coup > 0.0f && any);
            } else if (xmod == XBEND) {
                bb = bb * exp2_(coup * a.level);
            } else if (xmod == CHOKE) {
                inv_tb = inv_tb * (1.0f + (coup * 30.0f) * a.level);
            }
            b.coeffs(rng, (f0 * bratio) * bb, c.geom_e, inv_tb, c.matl, loss, c.pos * 0.73f, c.tilt, mexp);
            if (retune) b.retune();
            b.run(has_xb ? xb : nullptr, ob);
            if (xmod == TRADE) a.scale_y(1.0f - coup * 0.002f);
            std::memcpy(ob_prev, ob, sizeof ob);
        }
        float ymax = 0.0f;
        for (int j = 0; j < BLOCK; ++j) {
            y[j] = oa[j] + ob[j];
            if (has_bz) y[j] = y[j] + bz[j];
            ymax = fmax_(ymax, fabs_(y[j]));
        }
        float ra[BLOCK], rb[BLOCK];
        std::memcpy(ra, oa, sizeof ra);
        std::memcpy(rb, ob, sizeof rb);
        a.flush();
        if (has_b) b.flush();
        ea.flush();
        eb.flush();
        bool ended = exc_done && (!has_clk || clk.done()) && ymax < IDLE && a.energy() < IDLE &&
                     (!has_b || b.energy() < IDLE);
        if (has_clk && !clk.done()) clk.block(y);
        float z[BLOCK];
        float x1 = dc_x, y1 = dc_y;
        for (int j = 0; j < BLOCK; ++j) {
            y1 = (y[j] - x1) + DC_A * y1;
            x1 = y[j];
            z[j] = y1;
        }
        dc_x = x1;
        dc_y = fabs_(y1) < FLOORF ? 0.0f : y1;
        float pk = 0.0f;
        for (int j = 0; j < BLOCK; ++j) pk = fmax_(pk, fabs_(z[j]));
        lim = pk > lim ? pk : lim * LIM_KR;
        if (lim > LIM_TH) {
            float gg = LIM_TH * recip(lim);
            for (int j = 0; j < BLOCK; ++j) z[j] = z[j] * gg;
        }
        post(z, ra, rb, c, f0, l2f0);
        t += BLOCK;
        blocks += 1;
        bool fin = true;
        for (int j = 0; j < BLOCK; ++j) if (!std::isfinite(z[j])) fin = false;
        if (!(a.ok() && (!has_b || b.ok()) && fin)) {
            faults += 1;
            on = false;
            for (int j = 0; j < BLOCK; ++j) out[j] = 0.0f;
            return;
        }
        if (ended) on = false;
        std::memcpy(out, z, sizeof z);
    }

    float par_of(int st, const BC &c) const {
        switch (st) {
            case ST_RING: return u(c.ring);
            case ST_SHIFT: {
                float sv = ((float)c.shift - 64.0f) * INV63;
                float hz = 5.0f * exp2_(fabs_(sv) * LG_400);
                return (sv > 0.0f ? hz : -hz) * INVR;
            }
            case ST_FORM: return (float)c.form;
            case ST_WATR: return u(c.watr);
            case ST_FOLD: return u(c.fold);
            case ST_CHAR: return u(c.chr);
            default: return c.ulpg;
        }
    }

    void drop(int st) {
        if (st == ST_SHIFT) has_shift = false;
        else if (st == ST_FORM) { has_form = false; has_form_v = false; has_form_wet = false; }
        else if (st == ST_WATR) has_water = false;
    }

    static void shape(int st, const float *z, float p, float *w) {
        if (st == ST_FOLD) {
            float dr = 1.0f + 8.0f * p;
            float kq = (dr * 4.0f) * INV_2PI;
            float gq = 0.25f * (1.0f - p * 0.3f);
            for (int j = 0; j < BLOCK; ++j) w[j] = sin_t(frac(z[j] * kq)) * gq;
            return;
        }
        float dr = 1.0f + 20.0f * (p * p);
        float b0 = 0.1f * p;
        float rd = recip(tanh_r(dr));
        float tb = tanh_r(b0);
        for (int j = 0; j < BLOCK; ++j) w[j] = tanh_r(z[j] * dr + b0) * rd - tb;
    }

    void stage(int st, const float *z, const float *ra, const float *rb, float f0, float l2f0,
               float p, const float *g, bool has_p0, float p0, float *w) {
        float r[BLOCK];
        if (has_p0) for (int j = 0; j < BLOCK; ++j) r[j] = (float)(j + 1) * INVBLOCK;
        if (st == ST_RING) {
            float car[BLOCK];
            if (has_b) {
                for (int j = 0; j < BLOCK; ++j) car[j] = rb[j] * 8.0f;
            } else {
                float inc = (f0 * SQ2) * INVR;
                for (int j = 0; j < BLOCK; ++j) {
                    car[j] = sin_t(ring_ph);
                    ring_ph = frac(ring_ph + inc);
                }
            }
            for (int j = 0; j < BLOCK; ++j) {
                float d = has_p0 ? p0 + (p - p0) * r[j] : p;
                w[j] = z[j] * (1.0f - d) + (tanh_r(ra[j] * 8.0f) * tanh_r(car[j])) * (0.25f * d);
            }
            return;
        }
        if (st == ST_SHIFT) {
            if (!has_shift) { shift.reset(); has_shift = true; }
            for (int j = 0; j < BLOCK; ++j) w[j] = shift.tick(z[j], p);
            shift.flush();
            return;
        }
        if (st == ST_FORM) {
            if (!has_form) { form.reset(); has_form = true; }
            if (!has_form_v || form_v != p) {
                has_form_v = true;
                form_v = p;
                float v = u((int)p) * 4.0f;
                int jj = (int)v;
                if (jj > 3) jj = 3;
                float fr = v - (float)jj;
                float fc[2];
                for (int i = 0; i < 2; ++i)
                    fc[i] = VOWELS[jj * 2 + i] * (1.0f - fr) + VOWELS[(jj + 1) * 2 + i] * fr;
                form.set(fc, 8.0f);
                float w1 = fmin_(p * INV16, 1.0f) * 0.8f;
                form_w0 = has_form_wet ? form_wet : w1;
                form_wet = w1;
                has_form_wet = true;
            } else {
                form_w0 = form_wet;
            }
            float w0 = form_w0, w1 = form_wet;
            float dw = (w1 - w0) * INVBLOCK;
            for (int j = 0; j < BLOCK; ++j) {
                float v1a, v2a, v1b, v2b;
                form.tick(0, z[j], v1a, v2a);
                form.tick(1, z[j], v1b, v2b);
                float fsum = form.kk * v1a + form.kk * v1b;
                float wet = dw == 0.0f ? w1 : w0 + (float)(j + 1) * dw;
                w[j] = (1.0f - wet) * z[j] + (wet * fsum) * 2.0f;
            }
            form.flush();
            return;
        }
        if (st == ST_WATR) {
            if (!has_water) { water.reset(); has_water = true; }
            water.set(f0, l2f0, p);
            water.block(z, p, w);
            water.flush();
            return;
        }
        if (st == ST_FOLD || st == ST_CHAR) {
            shape(st, z, p, w);
            if (has_p0) {
                float w0[BLOCK];
                shape(st, z, p0, w0);
                for (int j = 0; j < BLOCK; ++j) w[j] = w0[j] + r[j] * (w[j] - w0[j]);
            }
            return;
        }
        for (int j = 0; j < BLOCK; ++j) {
            float ul = has_p0 ? p0 + (p - p0) * r[j] : p;
            w[j] = z[j] * ((1.0f - ul * 0.5f) + (ul * 0.5f) * g[j]);
        }
    }

    void post(float *z, const float *ra, const float *rb, const BC &c, float f0, float l2f0) {
        bool first = blocks == 0;
        bool ons[NSTAGE] = {c.ring > 0, c.shift != 64, c.form > 0, c.watr > 0,
                            c.fold > 0, c.chr > 0, c.LPG > 0};
        float kv = onepole_inv(fmin_(c.inv_t60, 100.0f));
        float g[BLOCK], w[BLOCK];
        for (int st = 0; st < NSTAGE; ++st) {
            bool on_ = ons[st];
            float lv0 = lv[st], lv1;
            if (first) {
                lv0 = lv1 = on_ ? 1.0f : 0.0f;
            } else {
                lv1 = on_ ? fmin_(lv0 + FADE_STEP, 1.0f) : fmax_(lv0 - FADE_STEP, 0.0f);
            }
            lv[st] = lv1;
            bool has_p0 = has_par[st];
            float p0 = par[st];
            if (on_) {
                par[st] = par_of(st, c);
                has_par[st] = true;
            }
            if (first || !ramped(st)) has_p0 = false;
            if (st == ST_VCA) {
                float e = venv;
                for (int j = 0; j < BLOCK; ++j) {
                    g[j] = e;
                    e = e * kv;
                }
                venv = fl(e);
            }
            if (lv0 == 0.0f && lv1 == 0.0f) {
                drop(st);
                continue;
            }
            bool movep = has_p0 && p0 != par[st];
            stage(st, z, ra, rb, f0, l2f0, par[st], g, movep, p0, w);
            if (lv0 == 1.0f && lv1 == 1.0f) {
                std::memcpy(z, w, sizeof w);
            } else {
                for (int j = 0; j < BLOCK; ++j) {
                    float r = lv0 + (lv1 - lv0) * ((float)(j + 1) * INVBLOCK);
                    z[j] = z[j] + r * (w[j] - z[j]);
                }
            }
        }
        for (int j = 0; j < BLOCK; ++j) z[j] = tanh_r(z[j] * 1.5f) * INV_15;
    }
};

}  // namespace fascia
