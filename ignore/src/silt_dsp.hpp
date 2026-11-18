// SILT — RIFT's material synthesis voice (FIBER, SILT, SKIN, FLUID, played by ten gestures),
// as a portable C++ voice.
//
// A line-for-line port of DN2-Rift's src/dnfw/silt/live.py (float32 in live.py's order, its
// own rounding at every operation). Build with -ffp-contract=off.
//
// Voice v(seed); v.note(P, note, vel); v.block(P, out32, off). P: the 32 controls as live.py
// reads them (pages 1-3 as 0..127, page 4 as option numbers: eff_of). The gate holds until the
// note-off; a released voice rests once it is 80 dB down.
#pragma once

#include <cmath>
#include <cstring>

#include "fascia_dsp.hpp"
#include "rv_math.hpp"

namespace silt {

using fascia::exp2_;
using fascia::recip;
using fascia::tanh_r;
static inline float sqrtf_(float x) { return fascia::sqrt_(x); }
using rv::cosf_;
using rv::sinf_;
using rv::tanf_;

static const float FS = 48000.0f;
static const float IFS = (float)(1.0 / 48000.0);
static const int SUB = 32;
static const float DT = (float)(32.0 / 48000.0);
static const float PI = (float)3.14159265358979;
static const float TWO_PI = (float)6.2831853;
static const float INV127 = (float)(1.0 / 127.0);
static const int NBR = 2048, NSEC = 8, NSEC_DEEP = 4, NSL = 1024;
static const int LINE_WORDS = 2 * NBR + NSEC * NSL;
static const float REST_AMP = 1e-4f;
static const int CLEAR_WORDS = 512;
static const int QN_MASS[3] = {2, 3, NSEC_DEEP};
static const int QN_SOLO[3] = {1, 2, 4};
static const float LOG2_10 = (float)3.32192809488736;
static const float INV_LN2_2 = (float)(2.0 / 0.693147180559945);
static const float HARM[8] = {0.0f, 1200.0f, 702.0f, 1902.0f, 2400.0f, 1586.0f, 969.0f, 1902.0f};
static const float MEM[6] = {1.0f, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f};
static const float BFR[4] = {290.0f, 470.0f, 1150.0f, 2600.0f};
static const float BGN[4] = {(float)(1.0 * 0.012), (float)(0.7 * 0.012), (float)(0.45 * 0.012), (float)(0.25 * 0.012)};

static inline float fack(int k) { return (0.85f + (0.3f * (float)((k * 7) % 5)) * 0.25f) - 1.0f; }
static inline float impk(int k) { return 0.6f + (0.4f * (float)((k * 3) % 4)) * (float)(1.0 / 3.0); }
static inline float sgnk(int k) { return (k % 2) ? 1.0f : -1.0f; }
static inline float altk(int k) { return (k % 2 == 0) ? 1.0f : -1.0f; }

static inline float mn(float a, float b) { return a < b ? a : b; }
static inline float mx(float a, float b) { return a > b ? a : b; }
static inline float clip(float x, float lo, float hi) { return mn(mx(x, lo), hi); }
static inline float fab(float x) { return x < 0.0f ? -x : (x == 0.0f ? 0.0f : x); }
static inline float flo(float x) { return std::floor(x); }

static inline float log2f_(float x) {
    if (x <= 0.0f) return -126.0f;
    uint32_t b;
    std::memcpy(&b, &x, 4);
    int e = (int)((b >> 23) & 0xFF) - 127;
    uint32_t mb = (b & 0x7FFFFFu) | 0x3F800000u;
    float m;
    std::memcpy(&m, &mb, 4);
    float t = (m - 1.0f) * recip(m + 1.0f);
    float t2 = t * t;
    float p = (float)(1.0 / 9.0);
    const float cs[4] = {(float)(1.0 / 7.0), (float)(1.0 / 5.0), (float)(1.0 / 3.0), 1.0f};
    for (int i = 0; i < 4; ++i) p = p * t2 + cs[i];
    return (float)e + INV_LN2_2 * (t * p);
}

static inline float powf_(float x, float y) {
    if (x <= 0.0f) return 0.0f;
    return exp2_(y * log2f_(x));
}

static inline float ex(float x) { return exp2_(x * (float)1.44269504088896); }

static inline float crv(float x, int shape) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    if (shape == 0) return x * x * (3.0f - 2.0f * x);
    if (shape == 2) return powf_(x, 2.5f);
    return x;
}

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1) : s(seed | 1u) {}
    float bi() {
        s = rv::xs32(s);
        return (float)(int32_t)s * (float)(1.0 / 2147483648.0);
    }
    float u() { return bi() * 0.5f + 0.5f; }
};

static inline float rd(const float *buf, int n, int w, float d) {
    int di = (int)d;
    float fr = d - (float)di;
    int r0 = w - di;
    if (r0 < 0) r0 += n;
    int r1 = r0 - 1;
    if (r1 < 0) r1 += n;
    return buf[r0] + (buf[r1] - buf[r0]) * fr;
}

static inline void wander(float *x, float k, Rng &rng, int slot) {
    float a = mn(1.0f, k * DT * 2.0f);
    x[slot + 1] = x[slot + 1] + (rng.bi() * 1.7f - x[slot + 1]) * a;
    x[slot] = x[slot] + (x[slot + 1] - x[slot]) * mn(1.0f, k * DT);
}

static inline float bow_k(float dv, float slope, float offset) {
    float bt = fab(dv * slope) + offset;
    float b2 = bt * bt;
    float b4 = b2 * b2;
    if (b4 <= 1.0f) return 1.0f;
    return recip(b4);
}

// the 32 knobs (0..127) as the voice reads them: pages 1-3 as they are, page 4 as options
static const int NOPT[8] = {4, 10, 4, 4, 5, 4, 4, 3};
static inline void eff_of(const int *knobs, float *P) {
    for (int i = 0; i < 24; ++i) P[i] = (float)knobs[i];
    for (int j = 0; j < 8; ++j) {
        int o = (knobs[24 + j] * NOPT[j]) >> 7;
        P[24 + j] = (float)(o < NOPT[j] - 1 ? o : NOPT[j] - 1);
    }
}

struct Voice {
    Rng rng, rng2;
    float bpm;
    float lines[LINE_WORDS];
    float *br, *nk;
    int wb = 0, wk = 0;
    int sw[NSEC];
    float slp[NSEC], sbow[NSEC], soff[NSEC], sdrift[2 * NSEC], svph[NSEC], sdx[NSEC], sdy[NSEC], sdel[NSEC];
    int scnt[NSEC];
    float simp = 0;
    float body[4][2], md[6][2];
    float bub_f[4], bub_a[4], bub_p[4], bub_r[4];
    float wnd[16], sk_ap[8];
    float lp, bow, hp, fm1, fm2, env_e, amp, sk_dx, sk_dy, sk_Es, sk_lp, sk_E, sk_s1, sk_s2, p_a, aval, prev_tgt,
        loop_t, qph, cl_hp, cl_b, mo_c, h1, h2, h3, h4, hx1, hx2, hx3, hx4, f_ax, f_ay, g_hold, t_note, part_t,
        burst, strike, sq, vib_ph, vib_env, gl_amt;
    float loop_per = 1.0f, f0 = 110.0f, vel = 1.0f;
    int p_n = 0, p_i = 0, g_cnt = 0;
    int gate = 0;
    int L4[8];
    int clear_at = LINE_WORDS;
    bool rest_ = true, fade = false, w_off = false;
    uint32_t seed;
    bool waiting = false;
    float w_note = 0, w_vel = 0;

    float *sb(int k) { return lines + 2 * NBR + k * NSL; }

    explicit Voice(uint32_t seed_ = 0x2545F491u, float bpm_ = 120.0f) { init(seed_, bpm_); }

    void init(uint32_t seed_, float bpm_) {
        rng = Rng(seed_);
        rng2 = Rng(seed_ * 7u);
        bpm = bpm_;
        std::memset(lines, 0, sizeof lines);
        br = lines;
        nk = lines + NBR;
        wb = wk = 0;
        for (int k = 0; k < NSEC; ++k) {
            sw[k] = 0; scnt[k] = 0;
            slp[k] = sbow[k] = soff[k] = svph[k] = sdx[k] = sdy[k] = sdel[k] = 0.0f;
        }
        for (int k = 0; k < 2 * NSEC; ++k) sdrift[k] = 0.0f;
        simp = 0.0f;
        std::memset(body, 0, sizeof body);
        std::memset(md, 0, sizeof md);
        for (int k = 0; k < 4; ++k) bub_f[k] = bub_a[k] = bub_p[k] = bub_r[k] = 0.0f;
        for (int k = 0; k < 16; ++k) wnd[k] = 0.0f;
        for (int k = 0; k < 8; ++k) { sk_ap[k] = 0.0f; L4[k] = 0; }
        lp = bow = hp = fm1 = fm2 = env_e = amp = sk_dx = sk_dy = sk_Es = sk_lp = sk_E = sk_s1 = sk_s2 = p_a =
            aval = prev_tgt = loop_t = qph = cl_hp = cl_b = mo_c = h1 = h2 = h3 = h4 = hx1 = hx2 = hx3 = hx4 =
                f_ax = f_ay = g_hold = t_note = part_t = burst = strike = sq = vib_ph = vib_env = gl_amt = 0.0f;
        loop_per = 1.0f; f0 = 110.0f; vel = 1.0f;
        p_n = p_i = g_cnt = 0;
        gate = 0;
        clear_at = LINE_WORDS;
        rest_ = true; fade = false; w_off = false;
        seed = seed_;
        waiting = false;
    }

    bool resting() const { return rest_ && !waiting && !fade && clear_at >= LINE_WORDS; }

    void become() { clear_at = 0; }

    void clear_step() {
        int a = clear_at;
        int hi = a + CLEAR_WORDS < LINE_WORDS ? a + CLEAR_WORDS : LINE_WORDS;
        for (int i = a; i < hi; ++i) lines[i] = 0.0f;
        clear_at = a + CLEAR_WORDS;
    }

    void note(const float *P, float note_, float vel_) {
        int mat = (int)P[24];
        if (clear_at < LINE_WORDS || fade) {
            waiting = true; w_note = note_; w_vel = vel_; w_off = false;
            return;
        }
        if (!rest_ && mat != L4[0]) {
            fade = true;
            waiting = true; w_note = note_; w_vel = vel_; w_off = false;
            return;
        }
        f0 = 440.0f * exp2_((note_ - 69.0f) * (float)(1.0 / 12.0));
        vel = vel_; gate = 1; rest_ = false;
        t_note = 0.0f; strike = 1.0f; burst = 0.0f; vib_env = 0.0f;
        gl_amt = 0.0f;
        for (int c = 0; c < 8; ++c) L4[c] = (int)P[24 + c];
        int dist = L4[3], tune = L4[6];
        float spread = P[16];
        for (int k = 0; k < NSEC; ++k) {
            float uu = rng.bi();
            float c;
            if (dist == 0) c = uu * 0.5f;
            else if (dist == 1) c = uu;
            else if (dist == 2) c = (0.6f + 0.4f * fab(uu)) * altk(k);
            else c = powf_(fab(uu), 0.7f);
            float cents = c * (5.0f + spread * (float)(195.0 / 127.0));
            if (tune == 1) {
                if (spread > 70.0f) cents = flo(cents * 0.01f + 0.5f) * 100.0f;
            } else if (tune == 2) {
                cents = flo(cents * 0.02f + 0.5f) * 50.0f;
            } else if (tune == 3) {
                cents = cents * 0.2f + HARM[k] * mn(1.0f, spread * (float)(1.0 / 64.0));
            }
            soff[k] = cents;
            sbow[k] = 0.0f;
            float s0 = (0.002f + P[22] * INV127 * 0.035f + (1.0f - P[3] * INV127) * 0.02f);
            sdel[k] = s0 * rng2.u();
            scnt[k] = -(int)(sdel[k] * FS);
        }
        int g_ = L4[1];
        simp = (g_ == 5 || g_ == 7 || (L4[0] == 2 && (g_ == 2 || g_ == 4))) ? (0.6f + 0.4f * vel) : 0.0f;
        loop_t = 0.0f;
        float a = (0.04f + (1.0f - P[9] * INV127) * 0.5f);
        loop_per = a * (1.0f + (P[12] + P[22]) * (float)(0.8 / 254.0) * rng2.bi());
    }

    void off() {
        if (waiting) w_off = true;
        else gate = 0;
    }

    void block(const float *P, float *out, bool offv = false) {
        if (offv) off();
        if (fade) {
            fade = false;
            render(P, out);
            for (int i = 0; i < SUB; ++i) out[i] = out[i] * (1.0f - (float)(i + 1) * (1.0f / SUB));
            rest_ = true;
            become();
            check(out);
            return;
        }
        if (clear_at < LINE_WORDS) {
            clear_step();
            for (int i = 0; i < SUB; ++i) out[i] = 0.0f;
            return;
        }
        if (waiting) {
            float wn = w_note, wv = w_vel;
            bool wo = w_off;
            waiting = false;
            note(P, wn, wv);
            if (wo) gate = 0;
        }
        if (rest_) {
            for (int i = 0; i < SUB; ++i) out[i] = 0.0f;
            return;
        }
        render(P, out);
        check(out);
    }

    void check(float *out) {
        for (int i = 0; i < SUB; ++i) {
            if (!std::isfinite(out[i])) {
                for (int k = 0; k < SUB; ++k) out[k] = 0.0f;
                uint32_t sd = seed;
                float bp = bpm;
                init(sd, bp);
                become();
                return;
            }
        }
    }

    void render(const float *P, float *out) {
        int *L = L4;
        float tn = t_note + DT;
        int gt = gate;
        int loopk = L[4];
        if (gt && loopk > 0 && tn > DT) {
            if (loopk < 4) {
                const float m[3] = {0.25f, 0.5f, 1.0f};
                loop_per = 60.0f * recip(bpm) * m[loopk - 1];
            }
            loop_t = loop_t + DT;
            if (loop_t >= loop_per) {
                loop_t = loop_t - loop_per;
                tn = DT;
                strike = 1.0f;
                for (int k = 0; k < NSEC; ++k) scnt[k] = -(int)(sdel[k] * FS);
                if (loopk == 4) {
                    float a = (0.04f + (1.0f - P[9] * INV127) * 0.5f);
                    loop_per = a * (1.0f + (P[12] + P[22]) * (float)(0.8 / 254.0) * rng2.bi());
                }
            }
        }
        t_note = tn;
        float n[24];
        for (int i = 0; i < 24; ++i) n[i] = P[i] * INV127;
        float part = n[0], tens = n[1], mass = n[2], cohe = n[3], visc = n[4], grain = n[5], dens = n[6],
              bodyk = n[7];
        float press = n[8], speedk = n[9], pos = n[10], rosin = n[11], scrap = n[12], snap = n[13], air = n[14],
              inst = n[15];
        float spread = n[16], voices = n[17], drift = n[18], vibr = n[19], form = n[21], rough = n[22],
              morph = n[23];
        int mat = L[0], gest = L[1], mode = L[2], distk = L[3], shape = L[5], qual = L[7];
        bool ens = mode == 1 || mode == 2;
        float lock = cohe > 0.55f ? (cohe - 0.55f) * (float)(1.0 / 0.45) : 0.0f;
        float loose = cohe < 0.55f ? (0.55f - cohe) * (float)(1.0 / 0.55) : 0.0f;
        float sadd = 0.0f, entry = 1.0f;
        float tgt = 0.0f, ppush = 0.0f, imp = 0.0f, damp_x = 1.0f, noisy = 0.0f, gn = 0.0f;
        float *w = wnd;
        float oms = 1.0f - snap;
        float atk = (0.004f + oms * oms * 0.6f) * (shape == 0 ? 3.0f : (shape == 2 ? 0.3f : 1.0f));
        float spd = (0.04f + speedk * 0.28f) * (0.6f + 0.4f * vel);
        float stlen = 0.03f + grain * 0.25f;
        bool held = gt || tn < 0.05f;
        if (gest == 0) {
            tgt = held ? spd : 0.0f;
        } else if (gest == 1) {
            wander(w, 9.0f, rng, 6);
            tgt = held ? spd * (0.7f + 0.6f * w[6]) : 0.0f;
            ppush = 0.35f + 0.2f * w[6];
            noisy = 0.25f;
        } else if (gest == 2) {
            tgt = tn < stlen ? spd * 1.3f : 0.0f;
            ppush = 0.15f;
            if (tn >= stlen) damp_x = 1.0f + 6.0f * (1.0f - mn(1.0f, (tn - stlen) * (float)(1.0 / 0.08)));
        } else if (gest == 3) {
            float rate = 6.0f + speedk * 14.0f;
            float ph = tn * rate;
            ph = ph - flo(ph);
            tgt = (gt || tn < 0.1f) ? (ph < 0.5f ? spd : -spd) : 0.0f;
        } else if (gest == 4) {
            int k_b = 0;
            float tt = tn;
            float per = 0.09f + (1.0f - speedk) * 0.12f;
            float amp_b = 1.0f;
            while (tt > per && k_b < 12) {
                tt = tt - per;
                per = per * 0.82f;
                k_b += 1;
                amp_b = amp_b * 0.85f;
            }
            tgt = (tt < stlen * 0.6f && k_b < 12) ? spd * 1.4f * amp_b : 0.0f;
        } else if (gest == 5) {
            imp = strike * (0.6f + 0.4f * vel);
        } else if (gest == 6) {
            tgt = held ? spd : 0.0f;
            gn = clip((P[20] - 64.0f) * (float)(1.0 / 63.0), -1.0f, 1.0f);
            gl_amt = gn * 12.0f * crv(tn * recip(0.4f + (1.0f - speedk) * 3.0f), shape);
        } else if (gest == 8) {
            float ramp = crv(tn * recip(0.3f + (1.0f - speedk) * 4.0f), shape);
            tgt = held ? spd * (0.15f + 0.85f * ramp) : 0.0f;
            ppush = 0.3f * ramp;
            sadd = 0.35f * ramp;
            entry = ramp;
        } else if (gest == 9) {
            float ramp = crv(tn * recip(0.5f + (1.0f - speedk) * 4.0f), shape);
            float q = qph + (3.0f + 17.0f * ramp) * DT;
            qph = q - flo(q);
            wander(w, 6.0f + 20.0f * ramp, rng, 10);
            tgt = held ? spd * (1.0f + 0.5f * ramp * sinf_(TWO_PI * qph)) : 0.0f;
            ppush = 0.3f * ramp * (0.5f + 0.5f * w[10]);
            sadd = 0.7f * ramp;
        } else {
            imp = strike;
            if (tn < 0.05f + grain * 0.8f && rng.u() < (0.15f + dens * 0.8f) * (1.0f - tn))
                imp = imp + 0.5f * rng.u() * (1.0f - mn(1.0f, tn * 1.5f));
        }
        float imp_sub = gest == 7 ? imp - strike : 0.0f;
        strike = 0.0f;
        if (gest != 6 && fab(P[20] - 64.0f) > 0.5f) {
            gn = clip((P[20] - 64.0f) * (float)(1.0 / 63.0), -1.0f, 1.0f);
            gl_amt = gn * 12.0f * (1.0f - crv(tn * recip(0.03f + (1.0f - speedk) * 0.6f), shape));
        }
        if (loose > 0.0f && tgt != 0.0f) {
            wander(w, 2.5f, rng2, 12);
            tgt = tgt * (1.0f + loose * 0.45f * w[12]);
        }
        if (shape == 2 && tn < 0.15f) ppush = ppush + 0.3f * ex(-tn * 20.0f);
        float stress = mn(1.0f, 0.45f * press * press + 0.25f * rosin * inst + 0.5f * inst * inst + 0.3f * rough +
                                    0.25f * scrap + sadd);
        if (stress > 0.6f) stress = 0.6f + 0.25f * tanh_r((stress - 0.6f) * 4.0f);
        if (morph > 0.01f && (gest == 2 || gest == 4 || gest == 5 || gest == 7) && tgt == 0.0f &&
            tn < 0.05f + 0.6f * morph && rng2.u() < morph * (0.1f + dens * 0.6f))
            imp = imp + morph * 0.5f * rng2.u();
        if (mat == 2 && P[6] > 32.0f && tn < 0.05f + grain * 0.4f &&
            rng2.u() < (P[6] - 32.0f) * (float)(0.25 / 95.0))
            imp = imp + 0.25f * (0.4f + 0.6f * rng2.u());
        if (imp > 0.0f) {
            int pn = (int)((0.00015f + oms * oms * 0.004f) * FS);
            p_n = pn > 4 ? pn : 4;
            p_i = 0;
            p_a = imp;
        }
        if (mat == 2 && tgt != 0.0f && prev_tgt == 0.0f && (gest == 2 || gest == 3 || gest == 4)) {
            int pn = (int)((0.0002f + oms * oms * 0.003f) * FS);
            p_n = pn > 4 ? pn : 4;
            p_i = 0;
            p_a = 0.3f + 0.5f * snap;
        }
        prev_tgt = tgt;
        if (shape == 3) {
            wander(w, 5.0f, rng, 8);
            tgt = tgt * (1.0f + 0.5f * w[8]);
        }
        if (morph > 0.01f && tgt != 0.0f) {
            part_t = part_t - DT;
            float pmin = 0.002f + (1.0f - dens) * 0.08f;
            if (part_t <= 0.0f) {
                part_t = pmin * (0.5f + rng.u());
                burst = rng.u() < 0.3f + 0.7f * cohe ? 1.0f : 0.3f;
                mo_c = morph * morph * 350.0f * rng2.bi();
            }
            tgt = tgt * (1.0f - morph) + tgt * morph * burst * (part_t > pmin * 0.6f ? 2.0f : 0.0f);
        }
        // pitch
        vib_env = mn(1.0f, vib_env + DT * (float)(1.0 / 0.6));
        float v = vib_ph + (4.5f + 2.0f * inst) * DT;
        vib_ph = v - flo(v);
        wander(w, 0.6f, rng, 0);
        wander(w, 11.0f, rng, 2);
        wander(w, 7.0f, rng, 4);
        float vib_s = sinf_(TWO_PI * vib_ph);
        float cents = vibr * 45.0f * vib_env * vib_s + drift * 25.0f * w[0] + stress * stress * 25.0f * w[2];
        float glp = distk != 3 ? gl_amt * 100.0f : 0.0f;
        cents = cents + glp;
        if (morph > 0.01f) cents = cents + mo_c;
        float alpha = (0.15f + 0.6f * press) * snap * (mat == 2 ? 0.0f : 0.25f);
        float tau = 0.01f + mass * 0.12f;
        float ov = 1200.0f * log2f_(1.0f + alpha * ex(-tn * recip(tau)));
        cents = cents + ov;
        sq = sq * 0.6703200460f;
        if (mat == 0 && stress > 0.35f && tgt != 0.0f && rng.u() < stress * stress * stress * DT * 6.0f)
            sq = 0.6f + 0.4f * stress;
        float f = f0 * exp2_(cents * (float)(1.0 / 1200.0));
        // per-block coefficients
        float per = FS * recip(mx(f, 25.0f));
        float lpc = (0.32f + 0.55f * (1.0f - visc * 0.8f) * (0.55f + 0.45f * tens)) * (1.0f - 0.3f * mass);
        lpc = clip(damp_x != 1.0f ? lpc * recip(sqrtf_(damp_x)) : lpc, 0.08f, 0.95f);
        float lpd = (1.0f - lpc) * recip(lpc);
        float tot = mn(per - lpd, (float)(NBR - 4));
        float beta = 0.04f + pos * 0.4f;
        float db = clip(tot * beta, 1.5f, (float)NBR - 4.0f);
        float dn = clip(tot - db, 1.5f, (float)NBR - 4.0f);
        float half = clip(db + per * 0.5f, 2.0f, (float)NBR - 4.0f);
        float a_hp = recip(1.0f + TWO_PI * 0.45f * f * IFS);
        float lossm = morph > 0.0f ? 0.995f - 0.4f * morph * morph : 0.995f;
        float stiff = P[0] > 42.0f ? (P[0] - 42.0f) * (float)(1.0 / 85.0) : 0.0f;
        float fapc = -0.9f * stiff;
        if (stiff > 0.0f) {
            float fad = (1.0f - fapc) * recip(1.0f + fapc);
            if (fad > tot * 0.25f) {
                fad = tot * 0.25f;
                fapc = (1.0f - fad) * recip(1.0f + fad);
            }
            db = clip(db - fad, 1.5f, (float)NBR - 4.0f);
        }
        int ghold = 1;
        if (P[5] > 55.0f) {
            float gg = (P[5] - 55.0f) * (float)(1.0 / 72.0);
            ghold = 1 + (int)(gg * gg * 60.0f);
        }
        float crk = P[6] > 32.0f ? (P[6] - 32.0f) * (float)(1.0 / 95.0) : 0.0f;
        float pr = mn(1.0f, press * 0.85f + ppush + 0.3f * stress * w[4]);
        float slope = 5.5f - pr * 4.2f;
        float offset = 0.92f - rosin * 0.3f;
        float breakup = pr > 0.7f ? (pr - 0.7f) * 1.6f : 0.0f;
        float att = 1.0f - ex(-recip(atk * FS));
        float rel = 0.000694203f;
        float scr = mn(1.0f, 0.5f * scrap + noisy + stress * 0.15f);
        if (scr > 0.3f) scr = 0.3f + (scr - 0.3f) * 0.5f;
        float fs_b = exp2_(-mass * 1.9f);
        float rr = ex(-recip((0.02f + 0.03f * (1.0f - visc)) * FS));
        float bc[4], bs[4];
        for (int k = 0; k < 4; ++k) {
            float wk_ = TWO_PI * BFR[k] * fs_b * IFS;
            bc[k] = rr * cosf_(wk_);
            bs[k] = rr * sinf_(wk_);
        }
        float fcen = 300.0f * exp2_(form * 4.5f + env_e * 2.0f * form);
        float g_f = tanf_(PI * mn(fcen, 16000.0f) * IFS);
        float kq = (float)(1.0 / 3.0);
        float fa1 = recip(1.0f + g_f * (g_f + kq)), fa2 = g_f * fa1, fa3 = g_f * fa2;
        int nm = 2 + (int)(4.0f * voices + 0.5f);
        float mc[6] = {0, 0, 0, 0, 0, 0}, ms[6] = {0, 0, 0, 0, 0, 0};
        if (mat == 1) {
            float ppart = 1.0f + part;
            for (int k = 0; k < 6; ++k) {
                float har = (float)(k + 1);
                float mr = powf_(MEM[k] + (har - MEM[k]) * tens, ppart);
                float fk = f * mr * (1.0f + inst * 0.02f * env_e * (float)(k + 1));
                fk = fk * exp2_(soff[k] * spread * (float)(3.0 / 1200.0));
                fk = mn(fk, 20000.0f);
                float dec = (0.04f + (1.0f - visc) * 0.9f) * (1.0f + mass) * recip((1.0f + 0.6f * (float)k) * damp_x);
                dec = dec * (1.0f - 0.85f * morph);
                float r = ex(-recip(dec * FS));
                mc[k] = r * cosf_(TWO_PI * fk * IFS);
                ms[k] = r * sinf_(TWO_PI * fk * IFS);
            }
        }
        float Lsk = 0, apc = 0, a_lp = 0, gsk = 0, nlk = 1, inlk = 1, b1 = 0, b2 = 0, b3 = 0;
        int skd = 1;
        if (mat == 2) {
            float el = 0.03f + (1.0f - tens) * 0.15f + press * 0.08f;
            float skE = sk_E;
            float sk_cut = mn(16000.0f, (f * (1.0f + 3.0f * tens + 30.0f * skE * (0.3f + press)) + 2500.0f * snap * skE) *
                                            (1.0f - 0.4f * visc));
            float cut_l = mn(18000.0f, f * (3.0f + 25.0f * tens) * (1.0f + 0.5f * skE) * (1.0f - 0.5f * visc));
            a_lp = 1.0f - ex(-TWO_PI * cut_l * IFS);
            float fsk = f * (1.0f + el * 2.5f * mx(0.0f, skE - sk_Es));
            float perk = FS * recip(fsk);
            float dmax = perk * (float)(1.0 / 16.0);
            apc = -0.95f * powf_(part, 0.7f);
            if ((1.0f - apc) * recip(1.0f + apc) > dmax) apc = (1.0f - dmax) * recip(1.0f + dmax);
            Lsk = clip(perk - (1.0f - a_lp) * recip(a_lp) - 4.0f * (1.0f - apc) * recip(1.0f + apc), 2.0f,
                       (float)NBR - 4.0f);
            float om = 1.0f - visc;
            float dsk = (0.05f + om * sqrtf_(om) * 2.5f) * (0.5f + mass) * recip(damp_x) * (1.0f - 0.8f * morph);
            gsk = exp2_(-3.0f * LOG2_10 * recip(dsk * fsk));
            nlk = 1.0f + 2.0f * inst + 1.5f * stress;
            inlk = recip(nlk);
            float g2 = tanf_(PI * mn(sk_cut, 18000.0f) * IFS);
            float k2 = recip(0.6f + inst * 2.5f);
            b1 = recip(1.0f + g2 * (g2 + k2));
            b2 = g2 * b1;
            b3 = g2 * b2;
            int sk = (int)(Lsk * (0.03f + 0.47f * pos));
            skd = sk > 1 ? sk : 1;
        }
        wander(w, 0.8f + 3.0f * (1.0f - visc), rng2, 14);
        float fl_f = 0, fl_a = 0, fl_apc = 0, fl_L = 0, gfl = 0, drate = 0, drp_dec = 0;
        if (mat == 3) {
            float melt = crv(tn * recip((0.4f + visc * 4.0f) * (0.5f + (1.0f - speedk))), shape);
            fl_f = f * exp2_(-(1.0f - tens) * (float)(160.0 / 1200.0) * melt);
            float fl_cut = mn(16000.0f, fl_f * (2.0f + 22.0f * tens * (1.0f - 0.7f * melt)) * (1.0f - 0.5f * visc));
            fl_a = 1.0f - ex(-TWO_PI * fl_cut * IFS);
            fl_apc = -0.5f * part - 0.3f * melt * (0.5f + 0.5f * w[14]);
            float perf = FS * recip(fl_f);
            float dmx = perf * (float)(1.0 / 16.0);
            if ((1.0f - fl_apc) * recip(1.0f + fl_apc) > dmx) fl_apc = (1.0f - dmx) * recip(1.0f + dmx);
            fl_L = clip(perf - (1.0f - fl_a) * recip(fl_a) - 4.0f * (1.0f - fl_apc) * recip(1.0f + fl_apc), 2.0f,
                        (float)NBR - 4.0f);
            float dfl = (0.3f + 2.0f * (1.0f - visc)) * (0.5f + mass) * (1.0f - 0.5f * melt) * recip(damp_x) *
                        (1.0f - 0.8f * morph);
            gfl = exp2_(-3.0f * LOG2_10 * recip(dfl * fl_f));
            drate = (0.5f + dens * dens * 30.0f) * (0.3f + 0.7f * melt) * (1.0f + 2.0f * stress);
            drp_dec = ex(-recip((0.01f + 0.05f * (1.0f - visc) + 0.03f * grain) * FS));
        }
        int pw0i = (int)((0.00015f + oms * oms * 0.004f) * FS);
        int pw0 = pw0i > 4 ? pw0i : 4;
        float av_dec = ex(-recip((0.01f + grain * 0.08f) * FS));
        float wpos[6] = {0, 0, 0, 0, 0, 0};
        if (mat == 1)
            for (int k = 0; k < 6; ++k) wpos[k] = 0.25f + 0.75f * fab(sinf_(PI * (float)(k + 1) * (0.03f + 0.47f * pos)));
        // filaments
        int nsec = 0;
        if (mode >= 1) {
            nsec = QN_MASS[qual];
            int ns = (int)((float)nsec * (0.25f + 0.75f * voices) * (0.4f + 0.6f * vel) + 0.5f);
            nsec = ns > 1 ? ns : 1;
        } else if (voices > 0.01f && (mat == 0 || mat == 2)) {
            int ns = (int)((float)QN_SOLO[qual] * voices + 0.5f);
            nsec = ns > 1 ? ns : 1;
        }
        float sper[NSEC] = {0, 0, 0, 0, 0, 0, 0, 0};
        float base_c = cents - glp - ov;
        for (int k = 0; k < nsec; ++k) {
            wander(sdrift, 0.3f + 0.8f * drift, rng, 2 * k);
            float vv = svph[k] + (4.0f + 0.37f * (float)k + 2.0f * inst) * DT;
            svph[k] = vv - flo(vv);
            if (lock > 0.0f) {
                float dphi = vib_ph - svph[k];
                dphi = dphi - flo(dphi + 0.5f);
                svph[k] = svph[k] + dphi * lock * 0.2f;
            }
            int den = nsec - 1 > 1 ? nsec - 1 : 1;
            float uk = ((float)k * recip((float)den) - 0.5f) * 2.0f;
            float sc = 1.0f, gk = 0.0f;
            if (gest == 6) {
                float gp = tn * recip(0.4f + (1.0f - speedk) * 3.0f);
                if (distk == 0) gk = gl_amt * 100.0f;
                else if (distk == 1) gk = gn * 1200.0f * crv(gp * recip(1.0f + 0.35f * uk), shape) * (1.0f + 0.15f * uk);
                else if (distk == 2) gk = gl_amt * 100.0f * altk(k) * (1.0f + 0.75f * uk);
                else {
                    float pp = crv(gp, shape);
                    sc = gn > 0.0f ? 1.0f + gn * pp * 3.0f : 1.0f + gn * pp;
                }
            }
            float dr = sdrift[2 * k] * (1.0f - lock) + w[0] * lock;
            float c = soff[k] * sc + drift * 60.0f * dr * (1.0f + loose) +
                      vibr * 40.0f * vib_env * sinf_(TWO_PI * svph[k]) + gk + stress * 30.0f * w[2] * sgnk(k);
            float fk = f0 * exp2_((c + base_c) * (float)(1.0 / 1200.0));
            sper[k] = mx(mn(FS * recip(mx(fk, 25.0f)), (float)(NSL - 4)), 2.0f);
        }
        float prate = 5.0f + dens * dens * 2500.0f;
        float sgn = recip(sqrtf_(1.0f + (prate * (fab(bow) * 6.0f + imp * 40.0f) +
                                         aval * 30.0f * (dens + 0.2f) * prate) * (float)(1.0 / 400.0)));
        bool bowc = gest == 0 || gest == 1 || gest == 6 || gest == 8 || gest == 9;
        bool hpchain = gest != 0 && gest != 6 && gest != 8;
        float prate_fs = prate * IFS;
        // -- samples --
        for (int i = 0; i < SUB; ++i) {
            float nz = rng.bi();
            float y = 0.0f;
            float pv = 0.0f;
            if (p_i < p_n) {
                float pn = (float)p_n;
                pv = p_a * sqrtf_(8.0f * recip(pn)) * sinf_(PI * ((float)p_i + 0.5f) * recip(pn));
                if (p_i < 24) pv = pv + nz * snap * 0.4f * p_a;
                p_i += 1;
            }
            float exv = 0.0f;
            if (mat == 0) {
                float a0 = rd(br, NBR, wb, db);
                float a1 = rd(nk, NBR, wk, dn);
                lp = lp + (a0 - lp) * lpc;
                float brr;
                if (stiff > 0.0f) {
                    float fy = fapc * lp + f_ax - fapc * f_ay;
                    f_ax = lp;
                    f_ay = fy;
                    brr = -fy * lossm;
                } else {
                    brr = -lp * lossm;
                }
                float nr = -a1;
                float sv = brr + nr;
                bow = bow + (tgt - bow) * (tgt != 0.0f ? att : rel);
                float contact = (tgt != 0.0f || bowc) ? 1.0f : mn(1.0f, fab(bow) * 60.0f);
                float dv = bow - sv;
                float bt = bow_k(dv, slope, offset);
                if (breakup > 0.0f) bt = bt * (1.0f + breakup * 0.6f * nz);
                float slip = 1.0f - bt;
                float gz = nz;
                if (ghold > 1) {
                    g_cnt -= 1;
                    if (g_cnt <= 0) {
                        g_hold = nz;
                        g_cnt = ghold;
                    }
                    gz = g_hold * 1.4f;
                }
                float nv = (dv * bt + gz * scr * slip * 0.06f * (0.4f + pr)) * contact;
                if (crk > 0.0f && bow != 0.0f && rng2.u() < crk * crk * 0.004f) nv = nv + rng2.bi() * 0.35f * crk;
                nv = nv + pv * 0.9f;
                nk[wk] = clip(brr + nv, -2.0f, 2.0f);
                br[wb] = clip(nr + nv, -2.0f, 2.0f);
                wb = wb + 1 < NBR ? wb + 1 : 0;
                wk = wk + 1 < NBR ? wk + 1 : 0;
                y = a0;
                if (sq > 0.01f) y = a0 * (1.0f - sq) + sq * 0.8f * (a0 + rd(br, NBR, wb, half));
                if (hpchain) {
                    h1 = a_hp * (h1 + y - hx1); hx1 = y;
                    h2 = a_hp * (h2 + h1 - hx2); hx2 = h1;
                    h3 = a_hp * (h3 + h2 - hx3); hx3 = h2;
                    h4 = a_hp * (h4 + h3 - hx4); hx4 = h3;
                    y = h4;
                }
                exv = nv;
                float hn = nz - hp;
                hp = hp + (nz - hp) * 0.25f;
                y = y + hn * air * fab(bow) * 0.3f;
            } else if (mat == 2) {
                bow = bow + (tgt - bow) * (tgt != 0.0f ? att : rel);
                float a = rd(br, NBR, wb, Lsk);
                sk_lp = sk_lp + (a - sk_lp) * a_lp;
                float xa = sk_lp;
                float *ap = sk_ap;
                for (int q = 0; q < 4; ++q) {
                    float apy = apc * xa + ap[2 * q] - apc * ap[2 * q + 1];
                    ap[2 * q] = xa;
                    ap[2 * q + 1] = apy;
                    xa = apy;
                }
                float vv = tanh_r(xa * gsk * nlk) * inlk;
                float dcy = vv - sk_dx + 0.995f * sk_dy;
                sk_dx = vv;
                sk_dy = dcy;
                vv = dcy;
                float drive = pv * 0.8f;
                if (bow != 0.0f) {
                    float dv = bow - vv * 0.5f;
                    float bt = bow_k(dv, slope, offset);
                    float reg = recip(1.0f + 6.0f * mx(0.0f, sk_E - (0.2f + 0.5f * press)));
                    drive = drive + (dv * bt * 1.6f + nz * scr * (1.0f - bt) * 0.03f) * reg;
                }
                if (stress > 0.3f && rng2.u() < stress * stress * stress * 0.002f) drive = drive + nz * 0.2f * stress;
                exv = drive;
                if (pv != 0.0f) {
                    int jx = wb - skd;
                    if (jx < 0) jx += NBR;
                    br[jx] = br[jx] - pv * 0.8f;
                }
                br[wb] = clip(vv + drive, -2.0f, 2.0f);
                wb = wb + 1 < NBR ? wb + 1 : 0;
                float v3 = a - sk_s2;
                float v1 = b1 * sk_s1 + b2 * v3;
                float v2 = sk_s2 + b2 * sk_s1 + b3 * v3;
                sk_s1 = 2.0f * v1 - sk_s1;
                sk_s2 = 2.0f * v2 - sk_s2;
                y = v2 * 2.2f + nz * air * 0.02f * (fab(bow) + fab(pv));
                float e = fab(y) * 2.5f;
                sk_E = sk_E + (e - sk_E) * (e > sk_E ? 0.01f : 0.0006f);
                if (sk_E > 1.0f) sk_E = 1.0f;
                sk_Es = sk_Es + (sk_E - sk_Es) * (0.00015f + 0.0006f * (1.0f - mass));
            } else if (mat == 1) {
                bool p_on = rng.u() < prate_fs * (fab(bow) * 6.0f + imp * 40.0f + aval * 30.0f * (dens + 0.2f));
                float d = 0.0f;
                if (p_on) {
                    d = (0.3f + 0.7f * rng.u()) * (1.0f + cohe * 2.0f * burst) * 0.012f * (0.4f + press) * sgn;
                    aval = mn(1.5f, aval + cohe * (0.25f + 0.5f * stress) * (0.5f + 0.5f * rosin));
                }
                aval = aval * av_dec;
                d = d + pv * 0.08f;
                exv = d;
                float acc = 0.0f;
                float pk = 1.0f;
                float hpart = 1.0f - part * 0.5f;
                for (int k = 0; k < 6; ++k) {
                    float re = mc[k] * md[k][0] - ms[k] * md[k][1];
                    if (k < nm && d != 0.0f) re = re + d * (0.4f + 0.6f * rng.u()) * pk * wpos[k];
                    float im = ms[k] * md[k][0] + mc[k] * md[k][1];
                    md[k][0] = re;
                    md[k][1] = im;
                    acc = acc + im;
                    pk = pk * hpart;
                }
                y = acc * 0.5f * (1.0f - 0.5f * morph) + nz * d * (grain * 2.0f + morph * 3.0f);
                hp = hp + (nz - hp) * 0.3f;
                y = y + (hp * fab(bow) * (scrap * 0.12f + air * 0.06f) + nz * aval * air * 0.01f);
                bow = bow + (tgt - bow) * (tgt != 0.0f ? att : rel);
            } else {
                bow = bow + (tgt - bow) * (tgt != 0.0f ? att : rel);
                float a = rd(br, NBR, wb, fl_L);
                sk_lp = sk_lp + (a - sk_lp) * fl_a;
                float xa = sk_lp;
                float *ap = sk_ap;
                for (int q = 0; q < 4; ++q) {
                    float apy = fl_apc * xa + ap[2 * q] - fl_apc * ap[2 * q + 1];
                    ap[2 * q] = xa;
                    ap[2 * q + 1] = apy;
                    xa = apy;
                }
                float vv = xa * gfl;
                float dcy = vv - sk_dx + 0.995f * sk_dy;
                sk_dx = vv;
                sk_dy = dcy;
                vv = dcy;
                float drive = pv * 0.6f;
                if (bow != 0.0f) {
                    float dv = bow - vv * 0.5f;
                    float bt = bow_k(dv, slope, offset);
                    float reg = recip(1.0f + 6.0f * mx(0.0f, sk_E - (0.15f + 0.4f * press)));
                    drive = drive + dv * bt * 1.2f * reg;
                }
                if ((gt || tn < 2.0f) && rng2.u() < drate * (1.0f + aval * 4.0f) * IFS) {
                    int j = (int)(rng2.u() * 3.999f);
                    float uu = rng2.u();
                    int kh = 1 + (int)(powf_(uu, 0.5f + pos * 1.5f) * (1.0f + part * 6.0f));
                    bub_f[j] = fl_f * (float)kh * (1.0f + 0.004f * rng2.bi());
                    bub_a[j] = (0.3f + 0.7f * rng2.u()) * (0.3f + press) * 0.5f;
                    bub_r[j] = 0.0f;
                    drive = drive + bub_a[j] * 0.15f;
                    aval = mn(1.5f, aval + cohe * 0.5f);
                }
                aval = aval * av_dec;
                float drs = 0.0f;
                for (int j = 0; j < 4; ++j) {
                    if (bub_a[j] > 1e-4f) {
                        bub_r[j] = bub_r[j] + IFS;
                        float fj = bub_f[j] * (1.0f + (1.0f - visc) * 3.0f * mn(bub_r[j], 0.03f));
                        float bp = bub_p[j] + fj * IFS;
                        bub_p[j] = bp - flo(bp);
                        drs = drs + bub_a[j] * sinf_(TWO_PI * bub_p[j]);
                        bub_a[j] = bub_a[j] * drp_dec;
                    }
                }
                exv = drive + drs * 0.1f;
                br[wb] = clip(vv + drive, -2.0f, 2.0f);
                wb = wb + 1 < NBR ? wb + 1 : 0;
                y = a * 1.6f + drs * 0.35f;
                hp = hp + (nz - hp) * 0.08f;
                y = y + hp * (fab(bow) + drs) * air * 0.1f;
                float e = fab(a) * 2.5f;
                sk_E = sk_E + (e - sk_E) * (e > sk_E ? 0.01f : 0.0006f);
                if (sk_E > 1.0f) sk_E = 1.0f;
            }
            // filaments
            float yprev = y;
            if (nsec > 0) {
                float sacc = 0.0f;
                for (int k = 0; k < nsec; ++k) {
                    float *sbk = sb(k);
                    float a = rd(sbk, NSL, sw[k], sper[k]);
                    slp[k] = slp[k] + (a - slp[k]) * lpc;
                    float v_s;
                    if (mode == 2) {
                        v_s = -slp[k] * 0.996f;
                    } else {
                        v_s = slp[k] * 0.996f - sdx[k] + 0.995f * sdy[k];
                        sdx[k] = slp[k] * 0.996f;
                        sdy[k] = v_s;
                    }
                    float nvk;
                    if (mode == 0) {
                        nvk = yprev * 0.03f * (0.4f + 1.2f * cohe);
                    } else if (mat == 0 || mat == 1) {
                        float fac = 1.0f + fack(k) * (1.0f - lock) * (1.0f + 2.0f * loose);
                        float ent = gest == 8 ? clip(entry * (float)nsec - (float)k, 0.0f, 1.0f) : 1.0f;
                        sbow[k] = sbow[k] + (tgt * fac * ent - sbow[k]) * (tgt != 0.0f ? att : rel);
                        float dvk = sbow[k] - v_s;
                        float btk = bow_k(dvk, slope * 1.1f, offset);
                        float ck = (tgt != 0.0f || bowc) ? 1.0f : mn(1.0f, fab(sbow[k]) * 60.0f);
                        nvk = (dvk * btk + nz * scr * (1.0f - btk) * 0.05f) * ck;
                        if (imp_sub > 0.0f && i < 6)
                            nvk = nvk + imp_sub * 0.25f * impk(k) * (1.0f - (float)i * (float)(1.0 / 6.0));
                    } else {
                        nvk = exv * 0.3f;
                    }
                    if (simp > 0.0f && mode >= 1 && scnt[k] < pw0) {
                        scnt[k] += 1;
                        if (scnt[k] > 0)
                            nvk = nvk + simp * 0.5f * sqrtf_(8.0f * recip((float)pw0)) *
                                            sinf_(PI * (float)scnt[k] * recip((float)pw0));
                    }
                    sbk[sw[k]] = clip(v_s + nvk, -2.0f, 2.0f);
                    sw[k] = sw[k] + 1 < NSL ? sw[k] + 1 : 0;
                    sacc = sacc + a;
                }
                if (mode == 0) y = y + sacc * 0.6f * voices;
                else y = y * (ens ? 0.55f : 0.8f) + sacc * 1.6f * recip(sqrtf_((float)nsec) + 1.0f);
            }
            if (mode == 3) {
                if (rng.u() < prate_fs * (fab(bow) * 8.0f + 0.2f)) burst = mn(1.5f, burst + 0.5f * (0.3f + cohe));
                burst = burst * (0.9995f - (1.0f - cohe) * 0.002f);
                hp = hp + (nz - hp) * (0.05f + 0.4f * part);
                y = y + hp * burst * 0.12f * (0.3f + grain);
            }
            if (ens && dens > 0.25f) {
                if (rng2.u() < prate_fs * (fab(bow) * 8.0f + 0.1f)) cl_b = mn(1.5f, cl_b + 0.5f * (0.3f + cohe));
                cl_b = cl_b * (0.9995f - (1.0f - cohe) * 0.002f);
                cl_hp = cl_hp + (nz - cl_hp) * (0.05f + 0.4f * part);
                y = y + cl_hp * cl_b * 0.12f * (0.3f + grain) * (dens - 0.25f) * (float)(1.0 / 0.75);
            }
            float bo = 0.0f;
            for (int k = 0; k < 4; ++k) {
                float re = bc[k] * body[k][0] - bs[k] * body[k][1] + y * BGN[k];
                float im = bs[k] * body[k][0] + bc[k] * body[k][1];
                body[k][0] = re;
                body[k][1] = im;
                bo = bo + im;
            }
            y = y + bodyk * bo * 1.5f;
            float v3 = y - fm2;
            float v1 = fa1 * fm1 + fa2 * v3;
            float v2 = fm2 + fa2 * fm1 + fa3 * v3;
            fm1 = 2.0f * v1 - fm1;
            fm2 = 2.0f * v2 - fm2;
            y = y * (1.0f - form * 0.6f) + v1 * form * 1.8f;
            float ee = mn(1.0f, fab(exv) * 4.0f);
            env_e = env_e + (ee - env_e) * (ee > env_e ? 0.02f : 0.0015f);
            amp = amp + ((gt ? 1.0f : 0.0f) - amp) * (gt ? 0.01f : 0.0004f);
            out[i] = tanh_r(y * 0.6f) * amp;
        }
        if (!gt && amp < REST_AMP) rest_ = true;
    }
};

}  // namespace silt
