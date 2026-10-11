#pragma once

#include "Core/NBT/Tag.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace kestrel::world {

inline constexpr const char* PistonMovingKey = "KestrelPistonMoving";

inline std::array<int, 3> pistonDirection(int facing)
{
    static constexpr std::array<std::array<int, 3>, 6> Directions { {
        { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 },
    } };
    return Directions[std::clamp(facing, 0, 5)];
}

inline float pistonProgress(const Tag& data)
{
    const Tag* progress = data.get("Progress");
    return progress && progress->getType() == Tag::Type::Float && std::isfinite(progress->asFloat())
        ? std::clamp(progress->asFloat(), 0.0f, 1.0f) : 0.0f;
}

struct PistonAnimation {
    float from = 0;
    float to = 0;
    double start = 0;

    float progress(double now) const
    {
        return from + (to - from) * float(std::clamp((now - start) * 10.0, 0.0, 1.0));
    }
};

}
