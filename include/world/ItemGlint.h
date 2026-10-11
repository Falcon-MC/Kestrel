#pragma once

#include "world/Mesher.h"
#include "world/Glint.h"

#include <algorithm>
#include <cmath>

namespace kestrel::world {

inline void applyItemGlint(ModelQuadGpu& quad, uint32_t layer, double now, float strength, float speed,
    std::array<float, 4> texture = { 0.5f, 0.5f, 128.0f, 128.0f })
{
    auto parameters = itemGlintParameters(now, strength, speed);
    if (layer >= 8192 || parameters[2] == 0.0f) return;
    parameters[3] = float(layer);
    quad.glint = parameters;
    quad.glintTexture = texture;
}

}
