#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace kestrel::world {

inline std::array<float, 4> itemGlintParameters(double now, float strength, float speed)
{
    if (!std::isfinite(now) || !std::isfinite(strength) || !std::isfinite(speed)) return {};
    // The standard foil clock has independent 1750 ms and 3000 ms periods.
    double factor = std::clamp(speed, 0.0f, 400.0f) / 100.0;
    double seconds = factor > 0.0 ? std::fmod(std::max(now, 0.0), 21.0 / factor) * factor : 0.0;
    double milliseconds = std::floor(seconds * 1000.0);
    return { float(-std::fmod(milliseconds, 1750.0) / 1750.0), float(std::fmod(milliseconds, 3000.0) / 3000.0), std::clamp(strength, 0.0f, 100.0f) / 100.0f, 0.0f };
}

inline std::array<float, 4> itemGlintUv(float u, float v, const std::array<float, 4>& parameters, float scaleU = 0.5f, float scaleV = 0.5f)
{
    std::array<float, 4> result;
    constexpr float angles[] { -20.0f * 0.017453292519943295f, 80.0f * 0.017453292519943295f };
    for (size_t pass = 0; pass < 2; ++pass) {
        float c = std::cos(angles[pass]), s = std::sin(angles[pass]);
        result[pass * 2] = ((u - 0.5f) * c + (v - 0.5f) * s + 0.5f) * scaleU + parameters[pass];
        result[pass * 2 + 1] = (-(u - 0.5f) * s + (v - 0.5f) * c + 0.5f) * scaleV;
    }
    return result;
}

}
