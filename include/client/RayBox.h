#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace kestrel {

/**
 * Where a ray first enters a box, as a distance along the ray, or nothing
 * when it misses or the box is behind. The axis it enters across is stored
 * in entryAxis, -1 when the ray starts inside.
 */
inline std::optional<double> enterBox(const std::array<double, 3>& origin, const std::array<double, 3>& direction, const std::array<double, 3>& low, const std::array<double, 3>& high, int* entryAxis = nullptr)
{
    double entry = 0.0;
    double exit = std::numeric_limits<double>::max();
    int axisOfEntry = -1;
    for (size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1.0e-12) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) {
                return std::nullopt;
            }
            continue;
        }
        double first = (low[axis] - origin[axis]) / direction[axis];
        double second = (high[axis] - origin[axis]) / direction[axis];
        if (std::min(first, second) > entry) {
            entry = std::min(first, second);
            axisOfEntry = int(axis);
        }
        exit = std::min(exit, std::max(first, second));
    }
    if (entry > exit) {
        return std::nullopt;
    }
    if (entryAxis) {
        *entryAxis = axisOfEntry;
    }
    return entry;
}

inline std::optional<double> actorRayDistance(const std::array<double, 3>& origin, const std::array<double, 3>& direction,
    const std::array<double, 3>& low, const std::array<double, 3>& high, double reach, double margin)
{
    auto entry = enterBox(origin, direction, low, high);
    return entry && *entry + margin < reach ? entry : std::nullopt;
}

}
