#include "client/ServerCamera.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

/**
 * How far along a camera ease of the given curve is at t from 0 to 1, with
 * the curves in the order the camera instruction numbers them.
 */
float cameraEase(int type, float t)
{
    constexpr float Pi = 3.14159265f;
    t = std::clamp(t, 0.0f, 1.0f);
    auto in = [](int power, float x) {
        return std::pow(x, float(power));
    };
    auto out = [&](int power, float x) {
        return 1.0f - std::pow(1.0f - x, float(power));
    };
    auto inOut = [&](int power, float x) {
        return x < 0.5f ? std::pow(2.0f, float(power - 1)) * std::pow(x, float(power)) : 1.0f - std::pow(-2.0f * x + 2.0f, float(power)) * 0.5f;
    };
    auto bounceOut = [](float x) {
        constexpr float n = 7.5625f;
        constexpr float d = 2.75f;
        if (x < 1.0f / d) {
            return n * x * x;
        }
        if (x < 2.0f / d) {
            x -= 1.5f / d;
            return n * x * x + 0.75f;
        }
        if (x < 2.5f / d) {
            x -= 2.25f / d;
            return n * x * x + 0.9375f;
        }
        x -= 2.625f / d;
        return n * x * x + 0.984375f;
    };
    constexpr float Back = 1.70158f;
    constexpr float BackInOut = Back * 1.525f;
    switch (type) {
    case 1:
        return 1.0f - std::cos(t * 4.5f * Pi) * std::exp(-t * 6.0f);
    case 2:
        return 1.0f - std::cos(t * Pi * 0.5f);
    case 3:
        return std::sin(t * Pi * 0.5f);
    case 4:
        return -(std::cos(Pi * t) - 1.0f) * 0.5f;
    case 5:
        return in(2, t);
    case 6:
        return out(2, t);
    case 7:
        return inOut(2, t);
    case 8:
        return in(3, t);
    case 9:
        return out(3, t);
    case 10:
        return inOut(3, t);
    case 11:
        return in(4, t);
    case 12:
        return out(4, t);
    case 13:
        return inOut(4, t);
    case 14:
        return in(5, t);
    case 15:
        return out(5, t);
    case 16:
        return inOut(5, t);
    case 17:
        return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
    case 18:
        return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
    case 19:
        if (t <= 0.0f || t >= 1.0f) {
            return t;
        }
        return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f;
    case 20:
        return 1.0f - std::sqrt(1.0f - t * t);
    case 21:
        return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f));
    case 22:
        return t < 0.5f ? (1.0f - std::sqrt(1.0f - 4.0f * t * t)) * 0.5f : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) * 0.5f;
    case 23:
        return (Back + 1.0f) * t * t * t - Back * t * t;
    case 24:
        return 1.0f + (Back + 1.0f) * std::pow(t - 1.0f, 3.0f) + Back * std::pow(t - 1.0f, 2.0f);
    case 25:
        return t < 0.5f ? (std::pow(2.0f * t, 2.0f) * ((BackInOut + 1.0f) * 2.0f * t - BackInOut)) * 0.5f
                        : (std::pow(2.0f * t - 2.0f, 2.0f) * ((BackInOut + 1.0f) * (t * 2.0f - 2.0f) + BackInOut) + 2.0f) * 0.5f;
    case 26:
        return t <= 0.0f || t >= 1.0f ? t : -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * (2.0f * Pi / 3.0f));
    case 27:
        return t <= 0.0f || t >= 1.0f ? t : std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * (2.0f * Pi / 3.0f)) + 1.0f;
    case 28:
        if (t <= 0.0f || t >= 1.0f) {
            return t;
        }
        return t < 0.5f ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * (2.0f * Pi / 4.5f))) * 0.5f
                        : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * (2.0f * Pi / 4.5f))) * 0.5f + 1.0f;
    case 29:
        return 1.0f - bounceOut(1.0f - t);
    case 30:
        return bounceOut(t);
    case 31:
        return t < 0.5f ? (1.0f - bounceOut(1.0f - 2.0f * t)) * 0.5f : (1.0f + bounceOut(2.0f * t - 1.0f)) * 0.5f;
    default:
        return t;
    }
}


}
