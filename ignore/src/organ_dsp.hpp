// ORGAN — RIFT's pipe organ (Ravel's), as a portable C++ voice.
//
// A line-for-line port of DN2-Rift's src/dnfw/organ/live.py (Organ, Voice): every float a
// float32, every expression in live.py's order. Build with -ffp-contract=off.
//
// Voice v(slot); v.note_on(note, vel 0..1); v.block(knobs, out32) every 32 samples at 48 kHz.
// knobs: the 32 controls (RIFT's page order), 0..127. The organ plays until the next note (on
// norns the UGen's gate and release close it).
#pragma once

#include <cmath>

#include "organ_tables.h"
#include "rv_math.hpp"

namespace organ {

using namespace rv;

static const int SUB = 32;
static const int ORG_N = 256;
static const float INV127 = fascia::INV127;
static const float INV20 = 1.0f / 20.0f;
static const float INV3 = 1.0f / 3.0f;
static const float CHIFF_FLOOR = 1e-30f;
static const float CEL_STEP = 0.125f;
static const float KC_COEF = 0.9917013f;
static const float KC_TIME = 0.0125f;
static const float ROT_SLEW = 1.4f;
static const float J_CLIP = 3.0f;
static const float BK_UP = 0.15f, BK_DOWN = 0.03f;
static const float TRIM = 0.519f;
static const int MCHR = 20, MTUN = 18;
static const float GAIN[5] = {1.0f, 1.05f, 1.5f, 2.4f, 2.4f};
static const float RATIO[8] = {0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 8.0f, 1.0f};
// v0.4 sounds hold these on pages 3 and 4: read as all 0 (organ.live.LEGACY)
static const int LEGACY[16] = {64, 64, 0, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 0, 0, 0};

struct Organ {
    float ph[8], dly[8], cents[8];
    float t = 0, chiff = 0, air_lp = 0, wind = 0, wind_v = 0, trem_ph = 0;
    SVF ch, ed;
    uint32_t ns = 0, kns = 0, jns = 0;
    uint32_t rng;
    float pe = 0, pph = 0, kc = 0, kprev = 0, rph = 0, rhz = 0, rlp = 0, cel_g = 0, rs = 0;
    float mph[2] = {0, 0};
    float pvel = 1.0f;
    float jv1 = 0, jv2 = 0, bk = 0, hph = 0, blp = 0, j = 0, press = 0, dep = 0, tot = 0;

    explicit Organ(uint32_t rng_) : rng(rng_) {
        for (int k = 0; k < 8; ++k) ph[k] = dly[k] = cents[k] = 0.0f;
    }

    void note(const float *e, float vel) {
        float spread = e[10] * (0.035f / 127.0f);
        float wnd = e[12] * (4.0f / 127.0f);
        if (!ns) ns = rng | 1u;
        t = 0.0f;
        chiff = 1.0f;
        for (int k = 0; k < 8; ++k) {
            dly[k] = spread * (xs_bi(ns) * 0.5f + 0.5f);
            cents[k] = (wnd * 0.5f) * xs_bi(ns);
            ph[k] = xs_bi(ns) * 0.5f + 0.5f;
        }
        if (!kns) kns = (rng + 0x3C6EF372u) | 1u;
        if (!jns) jns = (rng + 0x7F4A7C15u) | 1u;
        hph = 0.0f;
        pe = kc = 1.0f;
        pph = kprev = 0.0f;
        mph[0] = mph[1] = 0.0f;
        cel_g = e[15] * (14.0f / 127.0f) > 0.01f ? 1.0f : 0.0f;
        pvel = vel;
    }

    void render(const float *e, float f, float *out) {
        const int n = SUB;
        int stop = clampi((int)(int32_t)(e[0] + 0.5f), 0, 4);
        float speech = 0.01f * exp2f_(e[9] * (3.9f / 127.0f));
        float dt = (float)n * DT;
        float chf = e[8] * (1.0f / 127.0f);
        float air = e[11] * (0.06f / 127.0f);
        float wnd = e[12] * (1.0f / 127.0f);
        float trem = e[13] * (1.0f / 127.0f);
        float trem_hz = 3.0f * exp2f_(e[14] * (1.6f / 127.0f));
        float celeste = e[15] * (14.0f / 127.0f);
        float tt = 0.0f, onset = 0.0f;
        int idx[8], nr = 0;
        float lvl[8], inc[8], share[8];
        wind_v = wind_v + ((xs_bi(ns) - wind_v) * dt) * 3.0f;
        wind = wind + ((wind_v - wind) * dt) * 1.5f;
        trem_ph = trem_ph + trem_hz * dt;
        trem_ph = trem_ph - floorf_(trem_ph);
        float tr = trem * sin_turns(trem_ph);
        float wob = wind * wnd;
        if (celeste > 0.01f) {
            if (cel_g < 1.0f) {
                cel_g = cel_g + CEL_STEP;
                if (cel_g > 1.0f) cel_g = 1.0f;
            }
        } else if (cel_g > 0.0f) {
            cel_g = cel_g - CEL_STEP;
            if (cel_g < 0.0f) cel_g = 0.0f;
        }
        float rot = e[22] * (1.0f / 127.0f);
        float rotc = 0.0f;
        rs = 0.0f;
        if (rot > 0.0f) {
            float target = 0.6f * exp2f_(e[23] * (3.55f / 127.0f));
            if (rhz <= 0.0f) rhz = target;
            else rhz = rhz + ((target - rhz) * dt) * ROT_SLEW;
            rph = rph + rhz * dt;
            rph = rph - floorf_(rph);
            rs = sin_turns(rph);
            rotc = (rot * 8.0f) * sin_turns(rph + 0.25f);
        }
        float b = e[24] * (1.0f / 127.0f);
        float brk = e[26] * (1.0f / 127.0f);
        float edge = e[30] * (1.0f / 127.0f);
        float pr = e[28] * (1.0f / 127.0f) + (e[31] * (1.0f / 127.0f)) * (pvel - 0.5f);
        pr = clampf(pr, 0.0f, 1.0f);
        press = pr;
        j = 0.0f;
        if (b > 0.0f || brk > 0.0f || edge > 0.0f) {
            float fc = 1.5f * exp2f_(e[25] * (5.3f / 127.0f));
            float a = (fc * TWO_PI) * dt;
            if (a > 1.0f) a = 1.0f;
            jv1 = jv1 + (xs_bi(jns) - jv1) * a;
            jv2 = jv2 + (jv1 - jv2) * a;
            j = clampf((jv2 * sqrtf_(recip(a))) * 3.5f, -J_CLIP, J_CLIP);
        }
        rotc = rotc + ((b * 50.0f) * j + pr * 15.0f);
        if (brk > 0.0f) {
            float tgt = clampf(((j + pr * 1.5f) - (2.2f - brk * 2.4f)) * 2.0f, 0.0f, 1.0f);
            bk = bk + (tgt - bk) * (tgt > bk ? BK_UP : BK_DOWN);
        } else {
            bk = 0.0f;
        }
        dep = bk * (0.35f + e[27] * (1.0f / 127.0f));
        for (int k = 0; k < 8; ++k) {
            float lv;
            if (k < 7) lv = e[1 + k] * (1.0f / 127.0f);
            else lv = cel_g > 0.0f ? (e[2] * (0.8f / 127.0f)) * cel_g : 0.0f;
            float age = t - dly[k];
            float sp = speech * (1.0f - (float)(k < 7 ? k : 1) * 0.07f);
            if (lv < 0.002f) continue;
            float g = age <= 0.0f ? 0.0f : 1.0f - expf_(-age * recip(sp));
            float cents_ = ((((cents[k] - (1.0f - g) * 18.0f) + wob * 3.0f) + tr * 6.0f) + (k == 7 ? celeste : 0.0f)) + rotc;
            float fr = (f * RATIO[k]) * exp2f_(cents_ * (1.0f / 1200.0f));
            if (fr > 0.45f * 48000.0f) continue;
            idx[nr] = k;
            lvl[nr] = (lv * g) * ((1.0f + tr * 0.22f) + wob * 0.03f);
            inc[nr] = (fr * DT) * (float)ORG_N;
            float room = (0.45f * 48000.0f) * recip(fr);
            float sh;
            if (room >= 24.0f) sh = 0.0f;
            else if (room >= 4.0f) sh = (24.0f - room) * INV20;
            else sh = 1.0f + clampf((4.0f - room) * INV3, 0.0f, 1.0f);
            share[nr] = sh;
            nr++;
            tt = tt + lv;
            onset = onset + g;
        }
        tt = (tt > 1.0f ? 0.55f * recip(sqrtf_(tt)) : 0.55f) * GAIN[stop];
        tot = tt;
        ch.set(f * 3.0f < 9000.0f ? f * 3.0f : 9000.0f, 5.0f);
        float yb[SUB];
        for (int i = 0; i < n; ++i) yb[i] = 0.0f;
        for (int jj = 0; jj < nr; ++jj) {
            int r = idx[jj];
            int lo = share[jj] < 1.0f ? 0 : 1;
            float sh = share[jj] - (float)lo;
            float p = ph[r], dp = inc[jj], lv = lvl[jj];
            const int8_t *ta = ORG_TAB[stop][lo], *tb = ORG_TAB[stop][lo + 1];
            for (int i = 0; i < n; ++i) {
                p = p + dp;
                if (p >= (float)ORG_N) p = p - (float)ORG_N;
                int i0 = (int)p;
                float fr = p - (float)i0;
                int i1 = (i0 + 1) & (ORG_N - 1);
                float a = (float)ta[i0] + (float)(ta[i1] - ta[i0]) * fr;
                if (sh > 0.001f) {
                    float bb = (float)tb[i0] + (float)(tb[i1] - tb[i0]) * fr;
                    a = a + (bb - a) * sh;
                }
                yb[i] = yb[i] + a * lv;
            }
            ph[r] = p;
        }
        float ons = nr ? onset * recip((float)nr) : 0.0f;
        for (int i = 0; i < n; ++i) {
            float y = yb[i];
            float nsv = xs_bi(ns);
            y = y * (1.0f / 127.0f);
            float bp = ch.tick_bp(nsv);
            air_lp = air_lp + (nsv - air_lp) * 0.08f;
            y = y + (((bp * chf) * chiff) * 0.5f + (air_lp * air) * (0.3f + 0.7f * ons));
            out[i] = y * tot;
            chiff = chiff * 0.9992f;
            if (chiff < CHIFF_FLOOR) chiff = 0.0f;
        }
        t = t + dt;
    }

    void post(float *y, const float *e, float f) {
        const int n = SUB;
        int stop = clampi((int)(int32_t)(e[0] + 0.5f), 0, 4);
        float perc = e[16] * (0.9f / 127.0f);
        bool pon = false;
        float pinc = 0, pcoef = 0;
        if (perc > 0.0f) {
            float pf = f * 2.0f;
            pon = pf < 0.45f * 48000.0f;
            pinc = pf * DT;
            float tau = 0.04f * exp2f_(e[17] * (5.2f / 127.0f));
            pcoef = expf_(-DT * recip(tau));
        } else {
            pe = 0.0f;
        }
        float kk = e[19] * (0.48f / 127.0f);
        if (t > KC_TIME) kc = 0.0f;
        bool kon = kk > 0.0f && kc > 0.0f;
        int nmix = 0;
        int mj[2];
        float minc[2], mlv[2];
        if (e[21] > 0.0f) {
            float st = floorf_(e[MTUN] * (36.0f / 127.0f) + 0.5f);
            float mt = exp2f_(-st * (1.0f / 12.0f));
            const float ratios[2] = {6.0f, 16.0f}, gains[2] = {0.55f, 0.35f};
            for (int q = 0; q < 2; ++q) {
                float fr = (f * ratios[q]) * mt;
                if (fr < 0.42f * 48000.0f) {
                    mj[nmix] = q;
                    minc[nmix] = (fr * DT) * (float)ORG_N;
                    mlv[nmix] = (e[21] * (gains[q] / 127.0f)) * (1.0f / 127.0f);
                    nmix++;
                }
            }
        }
        bool xon = pon || kon || nmix > 0;
        float vg = 1.0f;
        float mf = e[MCHR] * (1.0f / 127.0f);
        float rot = e[22] * (1.0f / 127.0f);
        float am = 0, rsw = 0;
        if (rot > 0.0f) {
            am = 1.0f - (rot * 0.3f) * (0.5f + 0.5f * rs);
            rsw = (rot * 0.5f) * (0.5f - 0.5f * rs);
        }
        const int8_t *ta = ORG_TAB[stop][1];
        const int8_t *tm = ORG_TAB[stop != 4 ? 4 : 0][1];
        float b = e[24] * (1.0f / 127.0f);
        float edge = e[30] * (1.0f / 127.0f);
        bool blon = e[29] > 0.0f;
        float gb = 0;
        if (blon) {
            float tb = 0.03f * exp2f_(e[29] * (6.7f / 127.0f));
            gb = 1.0f - expf_(-t * recip(tb));
            b = b * gb;
        }
        float lfl = 0, bdark = 0;
        if (b > 0.0f) {
            lfl = 1.0f + (b * 0.45f) * j;
            if (lfl < 0.0f) lfl = 0.0f;
            bdark = (b * 0.5f) * clampf(0.5f - 0.5f * j, 0.0f, 1.0f);
        }
        bool eon = edge > 0.0f;
        float eg = 0;
        if (eon) {
            eg = edge * 2.0f;
            if (blon) eg = eg * gb;
            eg = ((eg * (1.0f + (b * 0.5f) * j)) * tot) * TRIM;
            ed.set(f, 6.0f);
        }
        bool hon = dep > 0.0f;
        float hinc = 0, hn = 0;
        if (hon) {
            hinc = (f * 0.5f) * DT;
            hn = recip(1.0f + dep * 0.5f);
        }
        for (int i = 0; i < n; ++i) {
            float v = y[i];
            if (xon) {
                float x = 0.0f;
                if (pon) {
                    x = x + (sin_turns(pph) * pe) * perc;
                    pph = pph + pinc;
                    pph = pph - floorf_(pph);
                    pe = pe * pcoef;
                    if (pe < CHIFF_FLOOR) pe = 0.0f;
                }
                if (kon) {
                    float nz = xs_bi(kns);
                    x = x + ((nz - kprev) * kc) * kk;
                    kprev = nz;
                    kc = kc * KC_COEF;
                }
                for (int q = 0; q < nmix; ++q) {
                    int jq = mj[q];
                    float p = mph[jq] + minc[q];
                    if (p >= (float)ORG_N) p = p - (float)ORG_N;
                    mph[jq] = p;
                    int i0 = (int)p;
                    float fr = p - (float)i0;
                    int i1 = (i0 + 1) & (ORG_N - 1);
                    float a = (float)ta[i0] + (float)(ta[i1] - ta[i0]) * fr;
                    if (mf > 0.0f) {
                        float bm = (float)tm[i0] + (float)(tm[i1] - tm[i0]) * fr;
                        a = a + (bm - a) * mf;
                    }
                    x = x + a * mlv[q];
                }
                v = v + (x * 0.45f) * vg;
            }
            if (b > 0.0f) v = v * lfl;
            if (hon) {
                v = (v * (1.0f + dep * sin_turns(hph))) * hn;
                hph = hph + hinc;
                hph = hph - floorf_(hph);
            }
            if (b > 0.0f) {
                blp = blp + (v - blp) * 0.12f;
                v = v + bdark * (blp - v);
            }
            if (eon) v = v + ed.tick_bp(xs_bi(jns)) * eg;
            if (rot > 0.0f) {
                rlp = rlp + (v - rlp) * 0.15f;
                v = v * am + rsw * (rlp - v);
            }
            y[i] = v;
        }
    }
};

// knobs (0..127) -> Ravel's eff: STOP its option, pages 3-4 at 0 on a v0.4 sound
static inline void eff_of(const int *knobs, float *e) {
    for (int i = 0; i < 32; ++i) e[i] = (float)knobs[i];
    int s = (knobs[0] * 5) >> 7;
    e[0] = (float)(s < 4 ? s : 4);
    bool legacy = true;
    for (int i = 0; i < 16; ++i) if (knobs[16 + i] != LEGACY[i]) legacy = false;
    if (legacy) for (int i = 16; i < 32; ++i) e[i] = 0.0f;
}

struct Voice {
    uint32_t slot;
    bool on = false, pending = false, fading = false;
    float pnote = 0.0f, pvel = 1.0f, f = 0.0f;
    bool has_org = false;
    Organ org;

    explicit Voice(uint32_t slot_ = 0) : slot(slot_), org(0x2545F491u + slot_) {}

    // vel: 0..1, read as the DN2's 0..127 (round(vel x 127) / 127)
    void note_on(float note, float vel) {
        int v = (int)(vel * 127.0f + 0.5f);
        v = v < 0 ? 0 : (v > 127 ? 127 : v);
        pnote = note;
        pvel = (float)v * INV127;
        pending = true;
        fading = on;
    }
    bool idle() const { return !on && !pending; }
    void rest() { on = pending = fading = false; }

    void block(const int *knobs, float *out) {
        float e[32];
        eff_of(knobs, e);
        if (fading && on) {
            org.render(e, f, out);
            for (int i = 0; i < SUB; ++i) out[i] = out[i] * TRIM;
            org.post(out, e, f);
            for (int i = 0; i < SUB; ++i) out[i] = out[i] * (1.0f - (float)(i + 1) * (1.0f / SUB));
            fading = on = false;
            check(out);
            return;
        }
        if (pending) {
            pending = false;
            float n = std::isfinite(pnote) ? pnote : 0.0f;
            f = note_hz(n);
            if (!has_org) { org = Organ(0x2545F491u + slot); has_org = true; }
            org.note(e, pvel);
            on = true;
        }
        if (!on) {
            for (int i = 0; i < SUB; ++i) out[i] = 0.0f;
            return;
        }
        org.render(e, f, out);
        for (int i = 0; i < SUB; ++i) out[i] = out[i] * TRIM;
        org.post(out, e, f);
        check(out);
    }

    void check(float *y) {
        for (int i = 0; i < SUB; ++i) {
            if (!std::isfinite(y[i])) {
                on = fading = false;
                has_org = false;
                for (int k = 0; k < SUB; ++k) y[k] = 0.0f;
                return;
            }
        }
    }
};

}  // namespace organ
