// The machines behind one interface (the UGens and the test driver use it).
#pragma once

#include <cstring>

#include "fascia_dsp.hpp"
#include "organ_dsp.hpp"
#include "reson_dsp.hpp"
#include "silt_dsp.hpp"
#include "ligature_dsp.hpp"

namespace machines {

struct AnyVoice {
    virtual ~AnyVoice() {}
    virtual int nknobs() const = 0;
    virtual void note_on(float note, float vel) = 0;
    virtual void note_off() = 0;                     // machines with a gate of their own
    virtual void block(const int *knobs, float *out32) = 0;
    virtual bool idle() const = 0;
    virtual void rest() = 0;
};

struct FasciaV : AnyVoice {
    fascia::Voice v;
    explicit FasciaV(unsigned seed) : v(seed) {}
    int nknobs() const override { return 32; }
    void note_on(float n, float vel) override { v.note_on(n, vel); }
    void note_off() override {}
    void block(const int *k, float *o) override { v.block(k, o); }
    bool idle() const override { return v.idle(); }
    void rest() override { v.rest(); }
};

struct OrganV : AnyVoice {
    organ::Voice v;
    explicit OrganV(unsigned seed) : v(seed) {}
    int nknobs() const override { return 32; }
    void note_on(float n, float vel) override { v.note_on(n, vel); }
    void note_off() override {}                      // ORGAN plays on: the UGen's release closes it
    void block(const int *k, float *o) override { v.block(k, o); }
    bool idle() const override { return v.idle(); }
    void rest() override { v.rest(); }
};

struct ResonV : AnyVoice {
    reson::Voice v;
    explicit ResonV(unsigned seed) : v(seed) {}
    int nknobs() const override { return 8; }
    void note_on(float n, float vel) override { v.note_on(n, vel); }
    void note_off() override {}                      // a struck bar rings on
    void block(const int *k, float *o) override { v.block(k, o); }
    bool idle() const override { return v.idle(); }
    void rest() override { v.rest(); }
};

struct SiltV : AnyVoice {
    silt::Voice v;
    bool pend = false, offp = false;
    float pn = 0, pv = 0;
    explicit SiltV(unsigned seed) : v((0x2545F491u + seed) | 1u) {}
    int nknobs() const override { return 32; }
    // vel 0..1, read as the DN2's 0..127
    void note_on(float n, float vel) override {
        int k = (int)(vel * 127.0f + 0.5f);
        k = k < 0 ? 0 : (k > 127 ? 127 : k);
        pend = true; pn = n; pv = (float)k * silt::INV127;
    }
    void note_off() override { offp = true; }
    void block(const int *k, float *o) override {
        float P[32];
        silt::eff_of(k, P);
        if (pend) { pend = false; v.note(P, pn, pv); }
        bool off = offp;
        offp = false;
        v.block(P, o, off);
    }
    bool idle() const override { return !pend && v.resting(); }
    void rest() override { v.rest_ = true; v.gate = 0; v.waiting = false; pend = false; }
};

struct LigatureV : AnyVoice {
    ligature::Voice v;
    bool pend = false, offp = false;
    float pn = 0, pv = 0;
    explicit LigatureV(unsigned seed) : v(ligature::slot_seed(seed)) {}
    int nknobs() const override { return 32; }
    void note_on(float n, float vel) override {
        int k = (int)(vel * 127.0f + 0.5f);
        k = k < 0 ? 0 : (k > 127 ? 127 : k);
        pend = true; pn = n; pv = (float)k * ligature::INV127;
    }
    void note_off() override { offp = true; }
    void block(const int *k, float *o) override {
        if (pend) { pend = false; v.note(pn, pv); }
        if (offp) { offp = false; v.off(); }
        v.block(k, o);
    }
    bool idle() const override { return !pend && !v.on; }
    void rest() override { v.on = false; pend = false; }
};

static inline AnyVoice *make(const char *name, unsigned seed) {
    if (!std::strcmp(name, "fascia")) return new FasciaV(seed);
    if (!std::strcmp(name, "organ")) return new OrganV(seed);
    if (!std::strcmp(name, "reson")) return new ResonV(seed);
    if (!std::strcmp(name, "silt")) return new SiltV(seed);
    if (!std::strcmp(name, "ligature")) return new LigatureV(seed);
    return nullptr;
}

}  // namespace machines
