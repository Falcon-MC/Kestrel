#include "world/assets/BlockRules.h"
#include "world/BlockModels.h"

#include <cstdio>
#include <cstdlib>

using namespace kestrel::world;

void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main()
{
    require(rules::classify("border_block") == rules::Family::Model, "Border blocks must use terrain model geometry instead of a cube");
    require(rules::modelKind("border_block") == rules::ModelKind::Wall, "Border states must use their wall connections and post bit");
    require(rules::classify("allow") == rules::Family::Cube && rules::classify("deny") == rules::Family::Cube, "Allow and deny must retain full cube geometry");
    require(rules::classify("stone") == rules::Family::Cube && rules::modelKind("cobblestone_wall") == rules::ModelKind::Wall, "Unrelated terrain classification must stay unchanged");
    const models::Materials materials { 1, 2, 3, 4, 5, 6 };
    for (unsigned state = 0; state < 162; ++state) {
        unsigned remaining = state / 2;
        unsigned connections = (state % 2) << 8;
        for (unsigned side = 0; side < 4; ++side) {
            connections |= (remaining % 3) << (side * 2);
            remaining /= 3;
        }
        const auto quads = models::wall(materials, connections);
        if (connections == 0) continue;
        require(!quads.empty(), "Connected border states must retain terrain geometry");
        for (const auto& quad : quads) for (const auto& point : quad.positions) {
            require(point[0] >= 0 && point[0] <= 256 && point[1] >= 0 && point[1] <= 256 && point[2] >= 0 && point[2] <= 256, "Border geometry must stay inside its terrain cell");
        }
    }
}
