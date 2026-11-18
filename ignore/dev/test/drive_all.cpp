// One voice of any machine, driven block by block (the per-machine port checks):
//   M machine seed     make the voice (fascia organ reson silt ligature)
//   N note vel         note on before the next block (vel 0..1)
//   O                  note off before the next block
//   B k0 .. kN         one block on these knobs (32, or 8 for reson): 32 float32 to stdout
#include <cstdio>
#include <cstring>
#include <memory>

#include "../../src/machines.hpp"

int main() {
    std::unique_ptr<machines::AnyVoice> v;
    char tag[16];
    int kn[32];
    float out[32];
    while (std::scanf("%15s", tag) == 1) {
        if (tag[0] == 'M') {
            char name[16];
            unsigned seed;
            if (std::scanf("%15s %u", name, &seed) != 2) return 1;
            v.reset(machines::make(name, seed));
            if (!v) return 2;
        } else if (tag[0] == 'N') {
            double n, ve;
            if (std::scanf("%lf %lf", &n, &ve) != 2) return 1;
            v->note_on((float)n, (float)ve);
        } else if (tag[0] == 'O') {
            v->note_off();
        } else if (tag[0] == 'B') {
            int nk = v->nknobs();
            for (int i = 0; i < nk; ++i) if (std::scanf("%d", &kn[i]) != 1) return 1;
            v->block(kn, out);
            std::fwrite(out, sizeof(float), 32, stdout);
        }
    }
    return 0;
}
