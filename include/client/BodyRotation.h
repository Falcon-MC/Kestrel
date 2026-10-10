#pragma once

#include <algorithm>
#include <cmath>

namespace kestrel::actor {

inline float wrapDegrees(float degrees)
{
    float wrapped = std::fmod(degrees + 180.0f, 360.0f);
    return (wrapped < 0.0f ? wrapped + 360.0f : wrapped) - 180.0f;
}

inline float trailBody(float body, float head, double stepX, double stepZ, float ticks)
{
    if (stepX * stepX + stepZ * stepZ > 0.0025) {
        float heading = static_cast<float>(std::atan2(-stepX, stepZ) * 180.0 / 3.14159265358979);
        if (std::abs(wrapDegrees(head - heading)) > 95.0f) {
            heading += 180.0f;
        }
        body += wrapDegrees(heading - body) * (1.0f - std::pow(0.7f, ticks));
    }
    float turn = std::clamp(wrapDegrees(head - body), -75.0f, 75.0f);
    body = head - turn;
    if (std::abs(turn) > 50.0f) {
        body += turn * (1.0f - std::pow(0.8f, ticks));
    }
    return wrapDegrees(body);
}

}
