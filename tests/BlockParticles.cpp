#include "client/BlockParticles.h"

#include <cstdio>
#include <cstdlib>

int main()
{
    kestrel::BlockParticles particles;
    kestrel::ParticleBurst burst;
    burst.cell = { -493, 66, 511 };
    burst.shape = { 0, 0, 0, 1, 1, 1 };
    particles.spawn(burst);
    std::vector<kestrel::world::ModelQuadGpu> quads;
    unsigned samples = 0;
    particles.append(burst.cell, { -493, 68, 515 }, quads, [&](double x, double y, double z) {
        if (x < -493 || x > -492 || y < 66 || y > 67 || z < 511 || z > 512) {
            std::fprintf(stderr, "Block particles must sample light in world coordinates\n");
            std::exit(1);
        }
        ++samples;
        return uint8_t(0xe7);
    });
    if (samples != 100 || quads.size() != 100) return 1;
    for (const auto& quad : quads) {
        if (quad.words[12] != 0xe7e7e7e7u) return 1;
    }
}
