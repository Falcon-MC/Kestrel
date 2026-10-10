#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace kestrel {

inline bool validActorPose(const std::array<double, 3>& position, const std::array<float, 3>& turn)
{
    return std::all_of(position.begin(), position.end(), [](double value) { return std::isfinite(value); })
        && std::all_of(turn.begin(), turn.end(), [](float value) { return std::isfinite(value); });
}

}
