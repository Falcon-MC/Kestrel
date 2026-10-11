#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace kestrel {

inline std::array<double, 3> projectileStep(const std::array<double, 3>& position,
    const std::array<double, 3>& target, const std::array<float, 3>& velocity,
    uint8_t correctionTicks, bool extrapolate)
{
    auto next = position;
    for (size_t axis = 0; axis < 3; ++axis) {
        if (correctionTicks) {
            next[axis] += (target[axis] - position[axis]) / correctionTicks;
        } else if (extrapolate && std::isfinite(velocity[axis])) {
            next[axis] += velocity[axis];
        }
    }
    return next;
}

}
