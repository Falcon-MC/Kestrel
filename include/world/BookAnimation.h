#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace kestrel::world {

struct BookAnimation {
    float rotation = 0.0f;
    float openness = 0.0f;
    float flip = 0.0f;
    float previousRotation = 0.0f;
    float previousOpenness = 0.0f;
    float previousFlip = 0.0f;
    float targetRotation = 0.0f;
    float targetFlip = 0.0f;
    float flipSpeed = 0.0f;
    double ticks = 0.0;
    uint32_t random = 1;

    void advance(double seconds, std::optional<float> facing)
    {
        const int before = int(ticks + 1.0e-9);
        ticks += std::clamp(seconds, 0.0, 0.25) * 20.0;
        for (int tick = before; tick < int(ticks + 1.0e-9); ++tick) {
            previousRotation = rotation;
            previousOpenness = openness;
            previousFlip = flip;
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            if (facing) {
                targetRotation = *facing;
                openness += 0.1f;
                if (openness < 0.5f || random % 40 == 0) {
                    targetFlip += float(1 + (random >> 8) % 3) * (random & 1 ? 1.0f : -1.0f);
                }
            } else {
                targetRotation += 0.02f;
                openness -= 0.1f;
            }
            targetRotation = std::remainder(targetRotation, 6.2831853f);
            rotation += std::remainder(targetRotation - rotation, 6.2831853f) * 0.4f;
            openness = std::clamp(openness, 0.0f, 1.0f);
            flipSpeed += (std::clamp((targetFlip - flip) * 0.4f, -0.2f, 0.2f) - flipSpeed) * 0.9f;
            flip += flipSpeed;
        }
    }

    float fraction() const { return float(ticks - std::floor(ticks)); }
    float shownRotation() const { return previousRotation + (rotation - previousRotation) * fraction(); }
    float shownFlip() const { return previousFlip + (flip - previousFlip) * fraction(); }
    float shownOpenness() const { return previousOpenness + (openness - previousOpenness) * fraction(); }
};

inline std::array<float, 3> bookPartPosition(size_t part, const std::array<float, 3>& point, float time, float openness, float flip, float rotation, bool lectern = false)
{
    constexpr float Pi = 3.14159265f;
    const float spread = lectern ? 1.5f : (1.25f + std::sin(time * 0.02f) * 0.1f) * openness;
    float angle = part == 0 ? Pi + spread : part == 1 ? -spread : part == 2 ? Pi * 0.5f : part == 3 ? spread : -spread;
    if (part >= 5) {
        const float phase = flip + (part == 5 ? 0.25f : 0.75f);
        const float turn = lectern ? (part == 5 ? 0.1f : 0.9f)
            : std::clamp((phase - std::floor(phase)) * 1.6f - 0.3f, 0.0f, 1.0f);
        angle = spread * (1.0f - 2.0f * turn);
    }
    float x = point[0] * std::cos(angle) + point[2] * std::sin(angle);
    float z = -point[0] * std::sin(angle) + point[2] * std::cos(angle);
    if (part == 0) z -= 1.0f;
    else if (part == 1) z += 1.0f;
    else if (part >= 3) x += std::sin(spread);
    const float tilt = (lectern ? 67.5f : 80.0f) * Pi / 180.0f;
    const float localY = point[1] - (lectern ? 2.0f : 0.0f);
    const float y = x * std::sin(tilt) + localY * std::cos(tilt);
    x = x * std::cos(tilt) - localY * std::sin(tilt);
    if (lectern) rotation += Pi * 0.5f;
    return {
        8.0f + x * std::cos(rotation) - z * std::sin(rotation),
        (lectern ? 17.0f : 13.6f + std::sin(time * 0.1f) * 0.16f) + y,
        8.0f + x * std::sin(rotation) + z * std::cos(rotation),
    };
}

}
