#pragma once

#include <array>
#include <string_view>

namespace kestrel::world {
struct PaintingMotive {
    std::string_view name;
    int x, y, width, height;
    bool separate = false;
};

inline constexpr PaintingMotive PaintingMotives[] {
    { "Kebab", 0, 0, 16, 16 }, { "Aztec", 16, 0, 16, 16 },
    { "Alban", 32, 0, 16, 16 }, { "Aztec2", 48, 0, 16, 16 },
    { "Bomb", 64, 0, 16, 16 }, { "Plant", 80, 0, 16, 16 },
    { "Wasteland", 96, 0, 16, 16 }, { "Pool", 0, 32, 32, 16 },
    { "Courbet", 32, 32, 32, 16 }, { "Sea", 64, 32, 32, 16 },
    { "Sunset", 96, 32, 32, 16 }, { "Creebet", 128, 32, 32, 16 },
    { "Wanderer", 0, 64, 16, 32 }, { "Graham", 16, 64, 16, 32 },
    { "Match", 0, 128, 32, 32 }, { "Bust", 32, 128, 32, 32 },
    { "Stage", 64, 128, 32, 32 }, { "Void", 96, 128, 32, 32 },
    { "SkullAndRoses", 128, 128, 32, 32 }, { "Wither", 160, 128, 32, 32 },
    { "Fighters", 0, 96, 64, 32 }, { "Pointer", 0, 192, 64, 64 },
    { "Pigscene", 64, 192, 64, 64 }, { "BurningSkull", 128, 192, 64, 64 },
    { "Skeleton", 192, 64, 64, 48 }, { "DonkeyKong", 192, 112, 64, 48 },
    { "Earth", 0, 160, 32, 32 }, { "Wind", 32, 160, 32, 32 },
    { "Fire", 64, 160, 32, 32 }, { "Water", 96, 160, 32, 32 },
    { "baroque", 0, 0, 32, 32, true }, { "humble", 0, 0, 32, 32, true },
    { "meditative", 0, 0, 16, 16, true }, { "prairie_ride", 0, 0, 16, 32, true },
    { "unpacked", 0, 0, 64, 64, true }, { "backyard", 0, 0, 48, 64, true },
    { "bouquet", 0, 0, 48, 48, true }, { "cavebird", 0, 0, 48, 48, true },
    { "changing", 0, 0, 64, 32, true }, { "cotan", 0, 0, 48, 48, true },
    { "endboss", 0, 0, 48, 48, true }, { "fern", 0, 0, 48, 48, true },
    { "finding", 0, 0, 64, 32, true }, { "lowmist", 0, 0, 64, 32, true },
    { "orb", 0, 0, 64, 64, true }, { "owlemons", 0, 0, 48, 48, true },
    { "passage", 0, 0, 64, 32, true }, { "pond", 0, 0, 48, 64, true },
    { "sunflowers", 0, 0, 48, 48, true }, { "tides", 0, 0, 48, 48, true },
    { "dennis", 0, 0, 48, 48, true },
};

inline const PaintingMotive* paintingMotive(std::string_view name)
{
    if (name.size() > 128) {
        return nullptr;
    }
    if (name.starts_with("minecraft:")) {
        name.remove_prefix(10);
    }
    auto matches = [](std::string_view left, std::string_view right) {
        auto next = [](std::string_view text, size_t& index) {
            while (index < text.size() && text[index] == '_') {
                ++index;
            }
            if (index == text.size()) {
                return -1;
            }
            char value = text[index++];
            return value >= 'A' && value <= 'Z' ? int(value + ('a' - 'A')) : int(static_cast<unsigned char>(value));
        };
        size_t a = 0, b = 0;
        for (;;) {
            int x = next(left, a), y = next(right, b);
            if (x != y) {
                return false;
            }
            if (x == -1) {
                return true;
            }
        }
    };
    for (const auto& motive : PaintingMotives) {
        if (matches(motive.name, name)) {
            return &motive;
        }
    }
    return nullptr;
}

inline constexpr std::array<std::array<float, 3>, 4> PaintingNormals {{
    { 0, 0, 1 }, { -1, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 },
}};

inline std::array<double, 3> paintingHalfExtents(const PaintingMotive& motive, int direction)
{
    if (direction < 0 || direction > 3) {
        return {};
    }
    const double halfWidth = motive.width / 32.0;
    return { direction % 2 == 0 ? halfWidth : 1.0 / 32,
        motive.height / 32.0, direction % 2 == 0 ? 1.0 / 32 : halfWidth };
}
}
