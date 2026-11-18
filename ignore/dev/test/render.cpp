// Offline renderer for the port check: reads a spec on stdin, writes float32 mono on stdout.
//   P v0 .. v31        the 32 engine controls
//   S seconds          length
//   E t note vel       an event (every event its own voice, seed k + 1: live.render_notes)
#include <cstdio>
#include <vector>

#include "../../src/fascia_dsp.hpp"

using namespace fascia;

int main() {
    int q[NPARAM];
    for (int i = 0; i < NPARAM; ++i) q[i] = INTERNAL_DEFAULTS[i];
    double seconds = 1.0;
    struct Ev { double t; double note; double vel; };
    std::vector<Ev> evs;
    char tag[8];
    while (std::scanf("%7s", tag) == 1) {
        if (tag[0] == 'P') for (int i = 0; i < NPARAM; ++i) std::scanf("%d", &q[i]);
        else if (tag[0] == 'S') std::scanf("%lf", &seconds);
        else if (tag[0] == 'E') { Ev e; std::scanf("%lf %lf %lf", &e.t, &e.note, &e.vel); evs.push_back(e); }
    }
    long total = (long)(seconds * 48000.0);
    std::vector<float> out(total, 0.0f);
    for (size_t k = 0; k < evs.size(); ++k) {
        const Ev &e = evs[k];
        Voice v((uint32_t)(k + 1));
        v.note_on((float)e.note, (float)e.vel);
        long n = (long)((seconds - e.t) * 48000.0);
        long nb = (n + BLOCK - 1) / BLOCK;
        std::vector<float> x(nb * BLOCK, 0.0f);
        for (long i = 0; i < nb; ++i) {
            v.block(q, &x[i * BLOCK]);
            if (v.idle()) break;
        }
        long i0 = (long)(e.t * 48000.0);
        long m = n < total - i0 ? n : total - i0;
        for (long j = 0; j < m; ++j) out[i0 + j] = out[i0 + j] + x[j];
    }
    std::fwrite(out.data(), sizeof(float), out.size(), stdout);
    return 0;
}
