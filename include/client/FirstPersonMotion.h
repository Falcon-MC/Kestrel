#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace kestrel {

struct FirstPersonMotion {
    float distance = 0.0f;
    float bob = 0.0f;
    std::array<float, 2> lastLook {};
    std::array<float, 2> angleVelocity {};
    std::array<float, 2> rotation {};
    std::array<float, 2> velocity {};
    double lastTime = 0.0;
    bool initialized = false;

    std::array<double, 3> lastPosition {};
    bool positionInitialized = false;

    void advance(const std::array<double, 3>& position, bool onGround, bool alive, float deltaSeconds)
    {
        if (!std::all_of(position.begin(), position.end(), [](double value) { return std::isfinite(value); })) return;
        double moved = positionInitialized ? std::hypot(position[0] - lastPosition[0], position[2] - lastPosition[2]) : 0.0;
        lastPosition = position;
        positionInitialized = true;
        if (moved > 8.0) {
            bob = 0.0f;
            return;
        }
        distance += static_cast<float>(moved * 0.6);
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
        float ticks = deltaSeconds * 20.0f;
        float target = onGround && alive ? std::min(static_cast<float>(moved) / ticks, 0.1f) : 0.0f;
        bob += (target - bob) * (1.0f - std::pow(0.6f, std::min(ticks, 1000.0f)));
    }

    void look(double now, float pitch, float yaw)
    {
        if (!std::isfinite(pitch) || !std::isfinite(yaw) || !std::isfinite(now)) return;
        std::array<float, 2> current { pitch, yaw };
        if (!initialized) {
            initialized = true;
            lastTime = now;
            lastLook = current;
            return;
        }
        float elapsed = static_cast<float>(now - lastTime);
        lastTime = now;
        if (elapsed <= -0.00000011920929f || elapsed >= 0.2f) elapsed = 0.2f;
        if (elapsed <= 0.0f) return;
        for (size_t axis = 0; axis < 2; ++axis) {
            float delta = current[axis] - lastLook[axis];
            if (axis == 1) {
                delta = std::fmod(delta + 180.0f, 360.0f);
                if (delta < 0.0f) delta += 360.0f;
                delta -= 180.0f;
            }
            angleVelocity[axis] = std::clamp(angleVelocity[axis] * 0.8f + delta / elapsed * 0.2f, -50.0f, 50.0f);
        }
        // Equal bounded steps keep the turn spring stable through a stalled frame.
        int steps = std::max(1, static_cast<int>(std::ceil(elapsed * 120.0f)));
        float step = elapsed / steps;
        for (int index = 0; index < steps; ++index) {
            for (size_t axis = 0; axis < 2; ++axis) {
                velocity[axis] += (angleVelocity[axis] * 90.0f - rotation[axis] * 900.0f - velocity[axis] * 42.0f) * step;
                rotation[axis] += velocity[axis] * step;
                if (std::abs(velocity[axis]) >= 1000.0f) velocity[axis] = 0.0f;
            }
        }
        lastLook = current;
    }
};

}
