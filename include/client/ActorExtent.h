#pragma once

#include <cmath>

namespace kestrel {

inline float actorExtent(float extent, float fallback, float scale)
{
    float base = std::isfinite(extent) && extent > 0.0f ? extent : fallback;
    float factor = std::isfinite(scale) && scale >= 0.0f ? scale : 1.0f;
    return base * factor;
}

}
