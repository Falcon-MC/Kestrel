#include "world/assets/BlockRules.h"
#include "world/BlockModels.h"

#include <cstdio>
#include <cstdlib>

using namespace kestrel::world;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

int main()
{
    require(rules::classify("honey_block") == rules::Family::Model, "Honey must use layered model geometry");
    require(rules::modelKind("honey_block") == rules::ModelKind::Honey, "Honey must select its dedicated model");
    require(rules::classify("ice") == rules::Family::TransparentCube, "Ice must retain its independent cube geometry");
    require(rules::classify("slime") == rules::Family::TransparentCube, "Slime must retain its cube geometry");
    const models::Materials materials { 11, 12, 13, 14, 15, 16 };
    const auto quads = models::honey(materials);
    require(quads.size() == 12, "Honey must contain six core faces and six coating faces");
    for (size_t i = 0; i < quads.size(); ++i) {
        bool core = i < 6;
        const auto& quad = quads[i];
        require(quad.material == (core ? materials[i] : materials[models::Down]), "The core uses face textures and the coating uses the bottom texture");
        require((quad.flags & QuadCullFaceMask) == 0, "Honey faces must survive adjacent honey and opaque blocks");
        require((quad.flags & QuadFaceMask) == models::faceId(uint32_t(i % 6)), "Both layers must retain face lighting directions");
        for (const auto& point : quad.positions) {
            for (int16_t coordinate : point) {
                require(core ? coordinate == 16 || coordinate == 240 : coordinate == 0 || coordinate == 256,
                    "The core must be inset by one pixel while the coating fills the block");
            }
        }
        for (const auto& uv : quad.uvs) {
            for (uint16_t coordinate : uv) {
                require(coordinate <= 4096, "Honey UVs must stay within the texture");
            }
        }
    }
}
