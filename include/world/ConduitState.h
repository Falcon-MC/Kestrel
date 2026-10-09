#pragma once

#include <cstdlib>
#include <array>
#include <cmath>
#include <string_view>

namespace kestrel::world {

inline constexpr const char* ConduitActiveKey = "KestrelConduitActive";

inline bool conduitFrame(std::string_view name)
{
    return name == "minecraft:prismarine" || name == "minecraft:prismarine_bricks"
        || name == "minecraft:dark_prismarine" || name == "minecraft:sea_lantern";
}

template <class Water, class Frame>
int conduitFrameCount(Water water, Frame frame)
{
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            for (int z = -1; z <= 1; ++z) {
                if (!water(x, y, z)) return 0;
            }
        }
    }
    int count = 0;
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            for (int z = -2; z <= 2; ++z) {
                const bool edge = (x == 0 && (std::abs(y) == 2 || std::abs(z) == 2))
                    || (y == 0 && (std::abs(x) == 2 || std::abs(z) == 2))
                    || (z == 0 && (std::abs(x) == 2 || std::abs(y) == 2));
                if (edge && frame(x, y, z)) ++count;
            }
        }
    }
    return count;
}

inline std::array<float, 3> conduitPartPosition(size_t part, std::array<float, 3> point,
    float ticks, float eyeYaw, float eyePitch, bool inner = false)
{
    float height = 8.0f;
    if (part == 0) {
        constexpr std::array<float, 3> axis { 0.4082483f, 0.8164966f, 0.4082483f };
        const float angle = ticks * -0.0375f;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float dot = point[0] * axis[0] + point[1] * axis[1] + point[2] * axis[2];
        const auto before = point;
        for (size_t i = 0; i < 3; ++i) {
            const size_t j = (i + 1) % 3;
            const size_t k = (i + 2) % 3;
            point[i] = before[i] * cosine + (axis[j] * before[k] - axis[k] * before[j]) * sine
                + axis[i] * dot * (1.0f - cosine);
        }
    } else if (part <= 2) {
        const int phase = int(ticks / 66.0f) % 3;
        if (phase == 0) point = { point[0], -point[2], point[1] };
        else if (phase == 2) point = { -point[1], point[0], point[2] };
        if (inner) point = { -point[0] * 0.875f, point[1] * 0.875f, -point[2] * 0.875f };
    } else {
        const float y = point[1] * std::cos(eyePitch) - point[2] * std::sin(eyePitch);
        point[2] = point[1] * std::sin(eyePitch) + point[2] * std::cos(eyePitch);
        point[1] = y;
        const float x = point[0] * std::cos(eyeYaw) - point[2] * std::sin(eyeYaw);
        point[2] = point[0] * std::sin(eyeYaw) + point[2] * std::cos(eyeYaw);
        point[0] = x;
    }
    if (part == 0 || part >= 3) {
        const float bob = 0.5f + std::sin(ticks * 0.1f) * 0.5f;
        height = 4.8f + 3.2f * (bob * bob + bob);
    }
    return { point[0] + 8.0f, point[1] + height, point[2] + 8.0f };
}

}
