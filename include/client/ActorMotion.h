#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace kestrel {

struct ActorMotion {
    uint64_t moves = 0;
    uint64_t teleports = 0;
    uint64_t launchTurns = 0;
    std::array<double, 3> from {};
    std::array<double, 3> to {};
    std::array<double, 3> shown {};
    std::array<float, 3> turnFrom {};
    std::array<float, 3> turnTo {};
    std::array<float, 3> turnShown {};
    double start = 0.0;
    double duration = 0.0;
    double lastSample = 0.0;
    float bodyYaw = 0.0f;
    double lastFrame = 0.0;
    std::array<double, 3> lastShown {};

    void advance(double now)
    {
        double t = duration > 0.0 ? std::clamp((now - start) / duration, 0.0, 1.0) : 1.0;
        for (size_t axis = 0; axis < 3; ++axis) {
            shown[axis] = from[axis] + (to[axis] - from[axis]) * t;
            float delta = wrapDegrees(turnTo[axis] - turnFrom[axis]);
            turnShown[axis] = wrapDegrees(turnFrom[axis] + delta * static_cast<float>(t));
        }
    }

    void retarget(const std::array<double, 3>& position, const std::array<float, 3>& turn, double now)
    {
        // The previous frame's pose would hold still on every network update.
        advance(now);
        from = shown;
        to = position;
        turnFrom = turnShown;
        turnTo = turn;
        duration = std::clamp(now - lastSample, 0.05, 0.15);
        start = now;
        lastSample = now;
    }

private:
    static float wrapDegrees(float degrees)
    {
        float wrapped = std::fmod(degrees + 180.0f, 360.0f);
        return (wrapped < 0.0f ? wrapped + 360.0f : wrapped) - 180.0f;
    }
};

}
