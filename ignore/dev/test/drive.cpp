// One voice driven block by block (for the modulation/steal check):
//   V seed             make the voice
//   N note vel         note_on before the next block
//   B v0 .. v31        run one block on these controls (writes 32 float32 to stdout)
#include <cstdio>

#include "../../src/fascia_dsp.hpp"

using namespace fascia;

int main() {
    Voice v(1);
    int q[NPARAM];
    float out[BLOCK];
    char tag[8];
    while (std::scanf("%7s", tag) == 1) {
        if (tag[0] == 'V') { unsigned s; if (std::scanf("%u", &s) == 1) v = Voice(s); }
        else if (tag[0] == 'N') { double n, ve; if (std::scanf("%lf %lf", &n, &ve) == 2) v.note_on((float)n, (float)ve); }
        else if (tag[0] == 'B') {
            for (int i = 0; i < NPARAM; ++i) if (std::scanf("%d", &q[i]) != 1) return 1;
            v.block(q, out);
            std::fwrite(out, sizeof(float), BLOCK, stdout);
        }
    }
    return 0;
}
