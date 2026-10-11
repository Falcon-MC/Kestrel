#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace kestrel::world {

inline std::array<float, 3> shulkerLidPosition(std::array<float, 3> point, float openness, uint32_t facing)
{
    const float progress = std::clamp(openness, 0.0f, 1.0f);
    const float angle = progress * 4.71238898f;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    const float x = point[0] - 8.0f;
    const float z = point[2] - 8.0f;
    const float cx = x * cosine - z * sine;
    const float cy = point[1] - 8.0f + progress * 8.0f;
    const float cz = x * sine + z * cosine;
    switch (facing) {
    case 0: return { cx + 8, -cy + 8, -cz + 8 };
    case 2: return { cx + 8, cz + 8, -cy + 8 };
    case 3: return { cx + 8, -cz + 8, cy + 8 };
    case 4: return { -cy + 8, cx + 8, cz + 8 };
    case 5: return { cy + 8, -cx + 8, cz + 8 };
    default: return { cx + 8, cy + 8, cz + 8 };
    }
}

}
