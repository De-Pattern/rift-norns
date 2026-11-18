// LIGATURE — RIFT's block patcher (WEFT on the Digitone): twelve blocks wired into one voice,
// as a portable C++ voice.
//
// A line-for-line port of DN2-Rift's src/dnfw/ligature/live.py (float32, live.py's order).
// Voice v(seed); v.note(note, vel); v.off(); v.block(knobs32, out32). Page 1: PATCH TUNE FINE
// LFO1 LFO2 VSNS DRIV LEVL; pages 2-4: two settings a block (S1A S1B ... S12B), 64 = the patch's own.
#pragma once

#include <cmath>
#include <cstring>

#include "ligature_tables.h"
#include "rv_math.hpp"

namespace ligature {

using namespace rv;

static const int SUB = 32;
static const int NB = 12;
enum {
    NONE, PHASE, WAVE, SLOPE, CYCLE, SLEW, LP, BP, HP, ADD, MULT, MIX,
    FOLD = 14, COMP = 15, XOR = 18, FLIP = 19, HOLD = 22,
    SKIN = 26, BAR = 27, SAT = 32, RUNGLER = 50, IMPULSE = 69, CREASE = 70
};
enum { S_PITCH = 13, S_GATE, S_TRIG, S_VEL, S_NOISE, S_LFO1, S_LFO2, NSRC };
static const int NM = 6;
static const float REST_AMP = 1e-4f;
static const float TRIM = 0.6f;
static const int REST_BLOCKS = 16;
static const float INV127 = fascia::INV127;

static inline int patch_of(int k) {
    k = k < 0 ? 0 : (k > 127 ? 127 : k);
    int p = (k * NP) >> 7;
    return p < NP - 1 ? p : NP - 1;
}

static inline float WC(float x) { return x * (float)(127.0 / 64.0) - 1.0f; }
static inline float WG(float x) { return x * (float)(127.0 / 64.0); }
static inline int WN(float x, int n) {
    int v = (int)(int32_t)(x * 127.0f + 0.5f);
    return v < 0 ? 0 : (v > n - 1 ? n - 1 : v);
}
static inline float wtime(float x) { return 0.0005f * exp2f_(x * 14.0f); }
static inline float wk(float T) {
    T = T > 1e-5f ? T : 1e-5f;
    return 1.0f - expf_(-DT * recip(T));
}
static inline float sat(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return (x * (27.0f + x2)) * recip(27.0f + 9.0f * x2);
}
static inline float wwrap(float p) {
    if (p >= 1.0f) {
        p = p - 1.0f;
        if (p >= 1.0f) p = p - floorf_(p);
    } else if (p < 0.0f) {
        p = p + 1.0f;
        if (p < 0.0f) p = p - floorf_(p);
    }
    return p;
}
static inline float shape_k(int k, float p) {
    if (k == 0) return sin_turns(p);
    if (k == 1) {
        if (p < 0.25f) return 4.0f * p;
        if (p < 0.75f) return 2.0f - 4.0f * p;
        return 4.0f * p - 4.0f;
    }
    if (k == 2) return 2.0f * p - 1.0f;
    return p < 0.5f ? 1.0f : -1.0f;
}
static inline uint32_t xs(uint32_t x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}
static inline float xs_uni(uint32_t &r) {
    r = xs(r);
    return (float)(r >> 8) * INV16;
}
// a rising edge through THR
static inline bool rise(float x, float &prev, float thr) {
    bool e = x > thr && prev <= thr;
    prev = x;
    return e;
}

struct Block {
    float ph = 0, y = 0, prev = 0, prevb = 0, s1 = 0, s2 = 0, hold = 0, g2 = 0, d2 = 0;
    int st = 0;
    float z[2 * NM] = {0};
};

struct Voice {
    uint32_t seed, rng, rng2;
    bool on = false, trig = false;
    float gate = 0, note_n = 60, vel = 1;
    int quiet = 0;
    float lfo[2] = {0, 0};
    Block blk[NB];
    float buf[NB][SUB];
    int last_patch = -1;

    explicit Voice(uint32_t seed_) {
        seed = seed_ ? seed_ : 1u;
        rng = seed;
        uint32_t s2 = seed ^ 0x5BD1E995u;
        rng2 = s2 ? s2 : 1u;
        reset();
    }
    void reset() {
        for (int i = 0; i < NB; ++i) blk[i] = Block();
        std::memset(buf, 0, sizeof buf);
    }
    void note(float n, float v) {
        if (!on) reset();
        on = true;
        trig = true;
        gate = 1.0f;
        note_n = n;
        vel = v;
        quiet = 0;
    }
    void off() { gate = 0.0f; }

    void block(const int *kn, float *out) {
        for (int j = 0; j < SUB; ++j) out[j] = 0.0f;
        for (int q = 0; q < 2; ++q) {
            float hzq = 0.05f * exp2f_(((float)kn[3 + q] * INV127) * 9.0f);
            lfo[q] = wwrap(lfo[q] + (hzq * DT) * (float)SUB);
        }
        if (!on) { trig = false; return; }
        int pk = patch_of(kn[0]);
        if (pk != last_patch) {
            if (last_patch >= 0) reset();
            last_patch = pk;
        }
        const Patch &P = PATCHES[pk];
        float tune = ((float)kn[1] - 64.0f) * (float)(24.0 / 63.0);
        float fine = ((float)kn[2] - 64.0f) * (float)(50.0 / 63.0);
        float nn = (note_n + tune) + fine * 0.01f;
        float hz = note_hz(nn);
        hz = hz < 12000.0f ? hz : 12000.0f;
        static float src_zero[SUB];
        float src[NSRC][SUB];
        std::memset(src, 0, sizeof src);
        float pitch = ((note_n + tune) - 60.0f) * (float)(1.0 / 48.0);
        for (int j = 0; j < SUB; ++j) {
            src[S_PITCH][j] = pitch;
            src[S_GATE][j] = gate;
            src[S_VEL][j] = vel;
        }
        src[S_TRIG][0] = trig ? 1.0f : 0.0f;
        for (int j = 0; j < SUB; ++j) src[S_NOISE][j] = xs_uni(rng) * 2.0f - 1.0f;
        float l1 = sin_turns(lfo[0]), l2 = sin_turns(lfo[1]);
        for (int j = 0; j < SUB; ++j) { src[S_LFO1][j] = l1; src[S_LFO2][j] = l2; }
        (void)src_zero;
        const float *trg = src[S_TRIG];
        float gatev = gate;
        for (int i = 0; i < NB; ++i) {
            const PatchRow &R = P.rows[i];
            if (R.ty == NONE) continue;
            Block &S = blk[i];
            float x1 = clampf(((float)R.d1 + ((float)kn[8 + 2 * i] - 64.0f)) * INV127, 0.0f, 1.0f);
            float x2 = clampf(((float)R.d2 + ((float)kn[9 + 2 * i] - 64.0f)) * INV127, 0.0f, 1.0f);
            int moves = (P.bm >> i) & 1;
            float pa[SUB], pb[SUB];
            const float *ia = (R.a >= 1 && R.a <= NB) ? buf[R.a - 1] : src[R.a];
            const float *ib = (R.b >= 1 && R.b <= NB) ? buf[R.b - 1] : src[R.b];
            std::memcpy(pa, ia, sizeof pa);
            std::memcpy(pb, ib, sizeof pb);
            float ka = (float)R.aa * 0.01f;
            float kb = (float)R.ab * 0.01f;
            bool ac = R.a != 0;
            bool bc = R.b != 0 && !moves;
            if (moves && R.b) x2 = clampf(x2 + pb[0] * kb, 0.0f, 1.0f);
            if (!bc) kb = 0.0f;
            float yo[SUB];
            for (int j = 0; j < SUB; ++j) yo[j] = 0.0f;
            run(R.ty, S, x1, x2, pa, ka, pb, kb, ac, bc, trg, gatev, hz, yo);
            std::memcpy(buf[i], yo, sizeof yo);
        }
        const float *y = buf[P.out - 1];
        float vs = (float)kn[5] * INV127;
        float lv = ((float)kn[7] * INV127) * ((1.0f - vs) + vs * vel);
        float dr = 1.0f + ((float)kn[6] * INV127) * 7.0f;
        bool ok = true;
        float peak = 0.0f;
        for (int j = 0; j < SUB; ++j) {
            float v = y[j];
            if (!(v > -16.0f && v < 16.0f)) { ok = false; break; }
            float o = (sat(v * dr) * lv) * TRIM;
            out[j] = o;
            float a = o >= 0.0f ? o : -o;
            peak = a > peak ? a : peak;
        }
        if (!ok) {
            on = false;
            reset();
            trig = false;
            for (int j = 0; j < SUB; ++j) out[j] = 0.0f;
            return;
        }
        trig = false;
        if (gate == 0.0f) {
            quiet = peak < REST_AMP ? quiet + 1 : 0;
            if (quiet >= REST_BLOCKS) on = false;
        }
    }

    void run(int ty, Block &S, float x1, float x2, const float *pa, float ka, const float *pb, float kb, bool ac,
             bool bc, const float *trg, float gatev, float hz, float *yo) {
        if (ty == PHASE) {
            float c1 = ((hz * RATIO[WN(x1, 16)]) * exp2f_(WC(x2) * (float)(1.0 / 12.0))) * DT;
            float ph = S.ph;
            for (int j = 0; j < SUB; ++j) {
                float a = pa[j] * ka;
                ph = ph + c1 * (1.0f + 8.0f * a);
                ph = wwrap(ph);
                if (bc && rise(pb[j] * kb, S.prevb, 0.0f)) ph = 0.0f;
                yo[j] = ph * 2.0f - 1.0f;
            }
            S.ph = ph;
        } else if (ty == WAVE) {
            float k = clampf(0.5f + WC(x2) * 0.5f, 0.03f, 0.97f);
            float rk = 0.5f * recip(k);
            float rk1 = 0.5f * recip(1.0f - k);
            float s3 = x1 * 3.0f;
            int seg = (int)(int32_t)s3;
            seg = seg > 2 ? 2 : seg;
            float fr = s3 - (float)seg;
            float inc = hz * DT;
            float ph = S.ph;
            for (int j = 0; j < SUB; ++j) {
                float p;
                if (ac) {
                    p = (pa[j] * ka) * 0.5f + 0.5f;
                } else {
                    ph = wwrap(ph + inc);
                    p = ph;
                }
                p = p + (pb[j] * kb) * 0.5f;
                p = wwrap(p);
                p = p < k ? p * rk : 0.5f + (p - k) * rk1;
                float v = shape_k(seg, p);
                if (fr > 0.0f) v = v + (shape_k(seg + 1, p) - v) * fr;
                yo[j] = v;
            }
            S.ph = ph;
        } else if (ty == SLOPE || ty == SLEW) {
            float kr = wk(wtime(x1));
            float kf = wk(wtime(x2));
            float yv = S.y;
            for (int j = 0; j < SUB; ++j) {
                float sc = bc ? exp2f_(((-pb[j]) * kb) * 4.0f) : 1.0f;
                if (ty == SLOPE) {
                    float gg = ac ? pa[j] * ka : gatev;
                    bool e = rise(gg, S.prev, 0.5f);
                    if (e || (!ac && trg[j] > 0.0f)) S.st = 1;
                    if (S.st == 1) {
                        yv = yv + (1.25f - yv) * clampf(kr * sc, 0.0f, 1.0f);
                        if (yv >= 1.0f) { yv = 1.0f; S.st = 2; }
                    } else if (S.st == 2 && gg <= 0.5f) {
                        S.st = 0;
                    }
                    if (S.st == 0) yv = yv - yv * clampf(kf * sc, 0.0f, 1.0f);
                } else {
                    float r = pa[j] * ka;
                    float kk = r > yv ? kr : kf;
                    yv = yv + (r - yv) * clampf(kk * sc, 0.0f, 1.0f);
                }
                yo[j] = yv;
            }
            S.y = yv;
        } else if (ty == CYCLE) {
            float c1 = (0.02f * exp2f_(x1 * 17.0f)) * DT;
            float k = clampf(x2, 0.01f, 0.99f);
            float rk = recip(k);
            float rk1 = recip(1.0f - k);
            float ph = S.ph;
            for (int j = 0; j < SUB; ++j) {
                float inc = ac ? c1 * exp2f_((pa[j] * ka) * 5.0f) : c1;
                ph = wwrap(ph + inc);
                if (bc && rise(pb[j] * kb, S.prevb, 0.0f)) ph = 0.0f;
                float v = ph < k ? ph * rk : (1.0f - ph) * rk1;
                yo[j] = v * 2.0f - 1.0f;
            }
            S.ph = ph;
        } else if (ty == LP || ty == BP || ty == HP) {
            float c1 = 20.0f * exp2f_(x1 * 10.0f);
            float R = (2.0f - 1.96f * x2) * 0.5f;
            float R2 = 2.0f * R;
            float g = tanf_((3.14159265f * clampf(c1, 20.0f, 18000.0f)) * DT);
            float d = recip((1.0f + R2 * g) + g * g);
            float s1 = S.s1, s2 = S.s2;
            for (int j = 0; j < SUB; ++j) {
                float a = pa[j] * ka;
                float gg = g, dd = d;
                if (bc) {
                    if (!(j & 3)) {
                        float b = pb[j] * kb;
                        S.g2 = tanf_((3.14159265f * clampf(c1 * exp2f_(b * 5.0f), 20.0f, 18000.0f)) * DT);
                        S.d2 = recip((1.0f + R2 * S.g2) + S.g2 * S.g2);
                    }
                    gg = S.g2;
                    dd = S.d2;
                }
                float hp = ((a - (R2 + gg) * s1) - s2) * dd;
                float bp = gg * hp + s1;
                s1 = gg * hp + bp;
                float lp = gg * bp + s2;
                s2 = gg * bp + lp;
                float y = ty == LP ? lp : (ty == BP ? bp * R2 : hp);
                if (!(y > -8.0f && y < 8.0f)) s1 = s2 = y = 0.0f;
                yo[j] = y;
            }
            S.s1 = s1;
            S.s2 = s2;
        } else if (ty == ADD || ty == MULT || ty == MIX) {
            float o = WC(x1) * 2.0f;
            float gn = WG(x2);
            for (int j = 0; j < SUB; ++j) {
                float a = pa[j] * ka;
                float b = pb[j] * kb;
                if (ty == ADD) yo[j] = ((a + b) + o) * gn;
                else if (ty == MULT) yo[j] = (a * ((bc ? b : 1.0f) + o)) * gn;
                else yo[j] = (a + (b - a) * x1) * gn;
            }
        } else if (ty == FOLD) {
            float dr = 1.0f + x1 * 15.0f;
            float o = WC(x2);
            for (int j = 0; j < SUB; ++j) {
                float a = pa[j] * ka;
                float b = pb[j] * kb;
                yo[j] = sin_turns((((a * dr) * (1.0f + b)) + o) * 0.25f);
            }
        } else if (ty == COMP || ty == XOR || ty == FLIP) {
            float thr = WC(x2);
            for (int j = 0; j < SUB; ++j) {
                float a = pa[j] * ka;
                float b = pb[j] * kb;
                bool r;
                if (ty == COMP) {
                    r = a > b + thr;
                } else if (ty == XOR) {
                    r = (a > thr) != (b > thr);
                } else {
                    if (rise(a, S.prev, thr)) S.st ^= 1;
                    if (bc && rise(b, S.prevb, thr)) S.st = 0;
                    r = S.st == 1;
                }
                yo[j] = r ? x1 : -x1;
            }
        } else if (ty == HOLD) {
            float kh = x2 > 0.0f ? wk(wtime(x2 * 0.8f)) : 1.0f;
            float lv = 2.0f + x1 * 30.0f;
            float rl = recip(lv);
            for (int j = 0; j < SUB; ++j) {
                bool e;
                if (bc) e = rise(pb[j] * kb, S.prevb, 0.0f);
                else e = trg[j] > 0.0f;
                if (e) {
                    float qv = pa[j] * ka;
                    if (x1 > 0.0f) qv = (floorf_(((qv * 0.5f) + 0.5f) * lv + 0.5f) * rl) * 2.0f - 1.0f;
                    S.hold = qv;
                }
                S.y = S.y + (S.hold - S.y) * kh;
                yo[j] = S.y;
            }
        } else if (ty == SKIN || ty == BAR) {
            const float *modes = ty == SKIN ? MODES_SKIN : MODES_BAR;
            const float *l2rel = ty == SKIN ? L2REL_SKIN : L2REL_BAR;
            float pm = bc ? pb[0] * kb : 0.0f;
            float base = hz * exp2f_(pm);
            float dark = 1.0f - x2;
            float T0 = 0.002f * exp2f_(x1 * 12.0f);
            float cs[NM][3];
            for (int k = 0; k < NM; ++k) {
                float f = base * modes[k];
                float T = T0 * exp2f_((-dark * 1.2f) * l2rel[k]);
                float r = expf_((-6.9078f * DT) * recip(T));
                float gk = exp2f_((-dark * 1.5f) * L2K[k]) * 0.5f;
                float x = f * DT;
                if (x > 0.45f || x <= 0.0f) {
                    cs[k][0] = cs[k][1] = cs[k][2] = 0.0f;
                } else {
                    cs[k][0] = (2.0f * r) * sin_turns(x + 0.25f);
                    cs[k][1] = -(r * r);
                    cs[k][2] = sin_turns(x) * gk;
                }
            }
            float acc[SUB], ain[SUB];
            for (int j = 0; j < SUB; ++j) { acc[j] = 0.0f; ain[j] = pa[j] * ka; }
            float z[2 * NM];
            std::memcpy(z, S.z, sizeof z);
            for (int k = 0; k < NM; ++k) {
                float c0 = cs[k][0], c1 = cs[k][1], c2 = cs[k][2];
                float y0 = z[2 * k], y1 = z[2 * k + 1];
                for (int j = 0; j < SUB; ++j) {
                    float yk = (c0 * y0 + c1 * y1) + c2 * ain[j];
                    y1 = y0;
                    y0 = yk;
                    acc[j] = acc[j] + yk;
                }
                z[2 * k] = y0;
                z[2 * k + 1] = y1;
            }
            bool fine = true;
            for (int j = 0; j < SUB; ++j) if (!(-16.0f < acc[j] && acc[j] < 16.0f)) fine = false;
            if (fine) {
                std::memcpy(S.z, z, sizeof z);
                std::memcpy(yo, acc, sizeof acc);
            } else {
                for (int k = 0; k < 2 * NM; ++k) S.z[k] = 0.0f;
                for (int j = 0; j < SUB; ++j) yo[j] = 0.0f;
            }
        } else if (ty == SAT) {
            float dr = 1.0f + x1 * 15.0f;
            float o = WC(x2);
            for (int j = 0; j < SUB; ++j) {
                float a = pa[j] * ka;
                float m = bc ? exp2f_((pb[j] * kb) * 2.0f) : 1.0f;
                yo[j] = sat((((a * dr) * m) + o) * 0.8f);
            }
        } else if (ty == IMPULSE) {
            float d = (0.02f * exp2f_(-x1 * 9.0f)) * 48000.0f;
            float r = clampf(1.0f - x2, 0.01f, 1.0f);
            if (bc) d = d * exp2f_(-clampf(pb[0] * kb, -1.0f, 1.0f) * 3.0f);
            d = d > 2.0f ? d : 2.0f;
            float rdv = r * d;
            float up = 2.0f * recip(rdv > 1.0f ? rdv : 1.0f);
            float rd1 = (1.0f - r) * d;
            float dn = r >= 1.0f ? 2.0f : 2.0f * recip(rd1 > 1.0f ? rd1 : 1.0f);
            float ph = S.ph, lv = S.y;
            int st = S.st;
            float alv = lv < 0.0f ? -lv : lv;
            if (st > 2 || !(lv == lv) || alv > 1.0f) { st = 0; lv = -1.0f; }
            float inc = hz * DT;
            for (int j = 0; j < SUB; ++j) {
                bool t;
                if (ac) {
                    t = rise(pa[j] * ka, S.prev, 0.0f);
                } else {
                    ph = ph + inc;
                    t = ph >= 1.0f;
                    if (t) ph = ph - floorf_(ph);
                }
                if (t && st != 1) st = 1;
                if (st == 1) {
                    lv = lv + up;
                    if (lv >= 1.0f) { lv = 1.0f; st = 2; }
                } else if (st == 2) {
                    lv = lv - dn;
                    if (lv <= -1.0f) { lv = -1.0f; st = 0; }
                }
                yo[j] = lv;
            }
            S.ph = ph; S.y = lv; S.st = st;
        } else if (ty == CREASE) {
            float sh = WG(x1);
            float o = WC(x2);
            for (int j = 0; j < SUB; ++j) {
                float y = ((pa[j] * ka) + (pb[j] * kb)) + o;
                yo[j] = y < 0.0f ? y + sh : y - sh;
            }
        } else if (ty == RUNGLER) {
            int L = 3 + WN(x2, 6);
            int mask = (1 << L) - 1;
            for (int j = 0; j < SUB; ++j) {
                bool e;
                if (ac) e = rise(pa[j] * ka, S.prev, 0.0f);
                else e = trg[j] > 0.0f;
                if (e) {
                    int last = (S.st >> (L - 1)) & 1;
                    int nb;
                    if (bc) nb = (pb[j] * kb) > 0.0f ? 1 : 0;
                    else nb = xs_uni(rng2) < 0.5f ? 1 : 0;
                    bool keep = xs_uni(rng2) * 0.999f < x1;
                    S.st = ((S.st << 1) | (keep ? last : nb)) & mask;
                }
                yo[j] = (float)((S.st >> (L - 3)) & 7) * (float)(2.0 / 7.0) - 1.0f;
            }
        }
    }
};

static inline uint32_t slot_seed(uint32_t slot) { return 0x6C696700u + 0x01000193u * (slot + 1u); }

}  // namespace ligature
