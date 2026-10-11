#include "world/Painting.h"

#include <cstdio>
#include <cstdlib>
#include <set>

namespace {
void require(bool value, const char* message)
{
    if (!value) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
}

int main()
{
    using namespace kestrel::world;
    std::set<std::string_view> names;
    for (const auto& motive : PaintingMotives) {
        require(names.insert(motive.name).second, "Painting motives must be unique");
        require(paintingMotive(motive.name) == &motive, "Every painting must resolve to its definition");
        require(motive.width > 0 && motive.height > 0 && motive.width <= 64 && motive.height <= 64,
            "Painting dimensions must be bounded");
        require(motive.width % 16 == 0 && motive.height % 16 == 0, "Painting dimensions must use whole blocks");
        require(motive.separate || (motive.x >= 0 && motive.y >= 0
                    && motive.x + motive.width <= 256 && motive.y + motive.height <= 256),
            "Atlas paintings must stay within their texture");
    }
    const auto* wanderer = paintingMotive("Wanderer");
    require(wanderer && wanderer->width == 16 && wanderer->height == 32 && wanderer->y == 64,
        "Wanderer must use the vertical one-by-two atlas region");
    require(paintingMotive("minecraft:dennis") && paintingMotive("minecraft:dennis")->separate,
        "Namespaced paintings must resolve to their separate texture");
    require(paintingMotive("PrairieRide") == paintingMotive("prairie_ride"),
        "Packet motive names must resolve to the installed texture filename");
    require(!paintingMotive("") && !paintingMotive("../../kz") && !paintingMotive("unknown"),
        "Unrecognised motives must not resolve or become texture paths");
    require(!paintingMotive(std::string_view("Kebab\0extra", 11)), "Embedded zero bytes must not truncate a motive");
    for (const auto& normal : PaintingNormals) {
        require(normal[0] * normal[0] + normal[2] * normal[2] == 1.0f && normal[1] == 0.0f,
            "Painting directions must use horizontal unit normals");
    }
    require(PaintingNormals[2][2] == -1.0f, "Direction two must face north");
    require(paintingHalfExtents(*wanderer, 2) == std::array<double, 3> { 0.5, 1.0, 1.0 / 32 },
        "North-facing painting picking must use its centered thin bounds");
    require(paintingHalfExtents(*wanderer, 1) == std::array<double, 3> { 1.0 / 32, 1.0, 0.5 },
        "Side-facing painting picking must turn its width into the Z axis");
    require(paintingHalfExtents(*wanderer, -1) == std::array<double, 3> {}
            && paintingHalfExtents(*wanderer, 4) == std::array<double, 3> {},
        "Invalid directions must not produce a painting extent");
}
