#pragma once

#include "Core/NBT/Tag.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kestrel::world {

inline constexpr const char* BannerMovingKey = "KestrelBannerMoving";
inline constexpr std::array<uint32_t, 16> BannerDyeColors {
    0x1D1D21, 0xB02E26, 0x5E7C16, 0x835432, 0x3C44AA, 0x8932B8, 0x169C9C, 0x9D9D97,
    0x474F52, 0xF38BAA, 0x80C71F, 0xFED83D, 0x3AB3DA, 0xC74EBD, 0xF9801D, 0xF9FFFE,
};

struct BannerPatternName {
    std::string_view code;
    std::string_view texture;
};

inline constexpr std::array<BannerPatternName, 42> BannerPatternNames {{
    { "bo", "border" }, { "bri", "bricks" }, { "mc", "circle" }, { "cre", "creeper" },
    { "cr", "cross" }, { "cbo", "curly_border" }, { "lud", "diagonal_left" }, { "rd", "diagonal_right" },
    { "ld", "diagonal_up_left" }, { "rud", "diagonal_up_right" }, { "flo", "flower" }, { "gra", "gradient" },
    { "gru", "gradient_up" }, { "hh", "half_horizontal" }, { "hhb", "half_horizontal_bottom" }, { "vh", "half_vertical" },
    { "vhr", "half_vertical_right" }, { "moj", "mojang" }, { "mr", "rhombus" }, { "sku", "skull" },
    { "ss", "small_stripes" }, { "bl", "square_bottom_left" }, { "br", "square_bottom_right" }, { "tl", "square_top_left" },
    { "tr", "square_top_right" }, { "sc", "straight_cross" }, { "bs", "stripe_bottom" }, { "cs", "stripe_center" },
    { "dls", "stripe_downleft" }, { "drs", "stripe_downright" }, { "ls", "stripe_left" }, { "ms", "stripe_middle" },
    { "rs", "stripe_right" }, { "ts", "stripe_top" }, { "bt", "triangle_bottom" }, { "tt", "triangle_top" },
    { "bts", "triangles_bottom" }, { "tts", "triangles_top" }, { "glb", "globe" }, { "pig", "piglin" },
    { "flw", "flow" }, { "gus", "guster" },
}};

struct BannerPattern {
    uint8_t pattern = 0;
    uint8_t color = 0;
};

struct BannerDisplay {
    uint8_t color = 15;
    bool illager = false;
    std::vector<BannerPattern> patterns;
};

inline BannerDisplay bannerDisplay(const Tag& data)
{
    BannerDisplay display;
    if (const Tag* color = data.get("Base"); color && color->getType() == Tag::Type::Int && color->asInt() >= 0 && color->asInt() < 16) {
        display.color = uint8_t(color->asInt());
    }
    if (const Tag* type = data.get("Type"); type && type->getType() == Tag::Type::Int) display.illager = type->asInt() == 1;
    const Tag* patterns = data.get("Patterns");
    if (!patterns || !patterns->isList()) return display;
    size_t read = 0;
    for (const Tag& entry : patterns->getList()) {
        if (++read > 16) break;
        if (!entry.isCompound()) continue;
        const Tag* code = entry.get("Pattern");
        const Tag* color = entry.get("Color");
        if (!code || code->getType() != Tag::Type::String || !color || color->getType() != Tag::Type::Int
            || color->asInt() < 0 || color->asInt() >= 16) continue;
        for (size_t pattern = 0; pattern < BannerPatternNames.size(); ++pattern) {
            if (code->asString() == BannerPatternNames[pattern].code) {
                display.patterns.push_back({ uint8_t(pattern), uint8_t(color->asInt()) });
                break;
            }
        }
    }
    return display;
}

inline std::array<float, 3> bannerClothPosition(std::array<float, 3> point, bool wall, float yaw, double ticks, const std::array<int32_t, 3>& cell)
{
    constexpr double Pi = 3.141592653589793;
    const double phase = std::fmod(ticks + double(cell[0]) * 7 + double(cell[1]) * 9 + double(cell[2]) * 13, 100.0) / 100.0;
    const float pitch = float((-0.0125 + 0.01 * std::cos(phase * 2 * Pi)) * Pi);
    const float hingeY = wall ? 14 : 42;
    const float hingeZ = wall ? 2 : 10;
    const float dy = point[1] - hingeY;
    const float dz = point[2] - hingeZ;
    const float y = hingeY + dy * std::cos(pitch) - dz * std::sin(pitch);
    const float z = hingeZ + dy * std::sin(pitch) + dz * std::cos(pitch);
    return { 8 + (point[0] - 8) * std::cos(yaw) - (z - 8) * std::sin(yaw), y,
        8 + (point[0] - 8) * std::sin(yaw) + (z - 8) * std::cos(yaw) };
}

}
