#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace kestrel {

struct FirstPersonMotion {
    float distance = 0.0f;
    float oldDistance = 0.0f;
    float bob = 0.0f;
    float oldBob = 0.0f;
    float tilt = 0.0f;
    float oldTilt = 0.0f;
    std::array<float, 2> lastLook {};
    std::array<float, 2> angleVelocity {};
    std::array<float, 2> rotation {};
    std::array<float, 2> velocity {};
    double lastTime = 0.0;
    bool initialized = false;

    void tick(const std::array<float, 3>& movement, bool onGround, bool alive, bool swimming)
    {
        oldDistance = distance;
        oldBob = bob;
        oldTilt = tilt;
        float speed = std::hypot(movement[0], movement[2]);
        if (onGround) distance += speed * 0.6f;
        float targetBob = onGround && alive && !swimming ? std::min(speed, 0.1f) : 0.0f;
        float targetTilt = !onGround && alive ? std::atan(movement[1] * -0.2f) * 15.0f : 0.0f;
        bob += (targetBob - bob) * 0.08f;
        tilt += (targetTilt - tilt) * 0.4f;
    }

    void look(double now, float pitch, float yaw)
    {
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
        // The spring is integrated in bounded steps even when a render frame stalls.
        while (elapsed > 0.0f) {
            float step = std::min(elapsed, 0.008333334f);
            elapsed -= step;
            for (size_t axis = 0; axis < 2; ++axis) {
                velocity[axis] += (angleVelocity[axis] * 90.0f - rotation[axis] * 900.0f - velocity[axis] * 42.0f) * step;
                rotation[axis] += velocity[axis] * step;
            }
        }
        if (std::abs(velocity[0]) >= 1000.0f || std::abs(velocity[1]) >= 1000.0f) velocity = {};
        lastLook = current;
    }
};

}
