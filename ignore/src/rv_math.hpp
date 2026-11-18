// Ravel's rv_math.h / rv_dsp.h leaves as DN2-Rift's organ/live.py computes them (float32, one
// rounding an operation): shared by ORGAN, RESONATOR, LIGATURE and SILT. Divides are FASCIA's
// recip except where live.py writes a float32 division.
#pragma once

#include <cstdint>
#include <cstring>

#include "fascia_dsp.hpp"

namespace rv {

using fascia::recip;   // the seed 0x7ef311c7 - bits, three Newton steps

static inline uint32_t bits(float x) { uint32_t u; std::memcpy(&u, &x, 4); return u; }
static inline float fbits(uint32_t u) { float x; std::memcpy(&x, &u, 4); return x; }

static const float DT = 1.0f / 48000.0f;
static const float NYQ = 24000.0f;
static const float LOG2E = 1.44269504088896f;
static const float TWO_PI = 6.28318530717958647692f;

static inline float floorf_(float x) {
    float ax = x < 0.0f ? -x : x;
    float t;
    if (ax < 2.1e9f) t = (float)(int32_t)x;
    else t = x;   // |x| huge: already an integer in float32
    return t > x ? t - 1.0f : t;
}

static inline float exp2f_(float x) {
    if (x < -126.0f) return 0.0f;
    if (x > 127.0f) x = 127.0f;
    float n = floorf_(x);
    float f = x - n;
    float p = 0.000218775048f;
    p = p * f + 0.00123878215f;
    p = p * f + 0.00968458045f;
    p = p * f + 0.0554804263f;
    p = p * f + 0.240230502f;
    p = p * f + 0.693146929f;
    p = p * f + 1.0f;
    float s = fbits((uint32_t)((int)n + 127) << 23);
    return p * s;
}

static inline float expf_(float x) { return exp2f_(x * LOG2E); }

static inline float sin_half(float x) {
    float x2 = x * x;
    float p = -2.50521084e-8f;
    p = p * x2 + 2.75573192e-6f;
    p = p * x2 - 1.98412698e-4f;
    p = p * x2 + 8.33333333e-3f;
    p = p * x2 - 1.66666667e-1f;
    return x + (x * x2) * p;
}

static inline float sin_r(float r) {
    if (r > 0.25f) r = 0.5f - r;
    else if (r < -0.25f) r = -0.5f - r;
    return sin_half(r * TWO_PI);
}

static inline float sin_turns(float ph) { return sin_r(ph - floorf_(ph + 0.5f)); }

static inline float sinf_(float x) {
    float k = floorf_(x * 0.159154943f + 0.5f);
    float r = (x - k * 6.28125f) - k * 1.93530718e-3f;
    return sin_r(r * 0.159154943f);
}

static inline float cosf_(float x) { return sinf_(x + 1.57079633f); }

static inline float tanf_(float x) {
    float c = cosf_(x);
    float ac = c < 0.0f ? -c : c;
    if (ac < 1e-7f) c = c < 0.0f ? -1e-7f : 1e-7f;
    return sinf_(x) * recip(c);
}

static inline float sqrtf_(float x) {
    if (x <= 0.0f) return 0.0f;
    uint32_t u = bits(x);
    float y = fbits(0x1FBD1DF5u + (u >> 1));
    for (int i = 0; i < 3; ++i) y = 0.5f * (y + x * recip(y));
    return y;
}

static inline float clampf(float x, float lo, float hi) { return (x > lo && x < hi) ? x : (x > lo ? hi : lo); }
static inline int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }

// xorshift32 (13, 17, 5); a zero state takes 0x2545F491
static inline uint32_t xs32(uint32_t s) {
    uint32_t x = s ? s : 0x2545F491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}
static inline float xs_bi(uint32_t &st) {
    st = xs32(st);
    return (float)(int32_t)st * (1.0f / 2147483648.0f);
}

static inline float note_hz(float n) { return 440.0f * exp2f_((n - 69.0f) * (1.0f / 12.0f)); }

struct SVF {
    float ic1 = 0, ic2 = 0, g = 0, k = 0, a1 = 0, a2 = 0, a3 = 0;
    void set(float hz, float q) {
        hz = clampf(hz, 10.0f, NYQ * 0.9f);
        g = tanf_((3.14159265f * hz) * DT);
        k = 1.0f / (q < 0.05f ? 0.05f : q);
        a1 = recip(1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    float tick_bp(float x) {
        float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = (ic2 + a2 * ic1) + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v1;
    }
};

}  // namespace rv
