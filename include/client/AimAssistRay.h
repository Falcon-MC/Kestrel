#pragma once

#include <array>
#include <cmath>
#include <optional>

namespace kestrel {

inline bool aimAssistDirection(const std::array<double, 3>& origin,
    const std::optional<std::array<double, 3>>& point, std::array<float, 3>& direction)
{
    if (!point) return false;
    std::array<double, 3> delta;
    double lengthSquared = 0.0;
    for (size_t axis = 0; axis < 3; ++axis) {
        delta[axis] = (*point)[axis] - origin[axis];
        lengthSquared += delta[axis] * delta[axis];
    }
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.000001 || lengthSquared > 17.0 * 17.0) return false;
    double length = std::sqrt(lengthSquared);
    for (size_t axis = 0; axis < 3; ++axis) direction[axis] = float(delta[axis] / length);
    return true;
}

}
