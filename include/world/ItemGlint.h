#pragma once

#include "world/Mesher.h"

#include <algorithm>
#include <cmath>

namespace kestrel::world {

inline void applyItemGlint(ModelQuadGpu& quad, uint32_t layer, double now, float strength, float speed)
{
    if (layer >= 8192 || quad.words[15] != 0 || !std::isfinite(now) || !std::isfinite(strength) || !std::isfinite(speed)) return;
    uint32_t opacity = static_cast<uint32_t>(std::lround(std::clamp(strength, 0.0f, 100.0f) * 31.0f / 100.0f));
    if (opacity == 0) return;
    uint32_t frame = static_cast<uint32_t>(std::fmod(std::fmod(std::max(now, 0.0), 3600.0) * std::max(speed, 0.0f) / 100.0 * 8.0, 32.0));
    quad.words[10] = (quad.words[10] & 0x1fffu) | (layer << 13) | (frame << 26) | 0x80000000u;
    quad.words[14] = (quad.words[14] & 0xe0ffffffu) | (opacity << 24);
}

}
