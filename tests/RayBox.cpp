#include "client/RayBox.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

int main()
{
    using namespace kestrel;
    const std::array<double, 3> low { -0.8, 0, -0.8 }, high { 0.8, 2.7, 0.8 };
    auto inside = actorRayDistance({ 0, 1.62, 0 }, { 0, 0, 1 }, low, high, 3, 0.1);
    if (!inside || *inside != 0) { std::fprintf(stderr, "An overlapping actor must be hittable at zero distance\n"); return 1; }
    auto outside = actorRayDistance({ 0, 1.62, -2 }, { 0, 0, 1 }, low, high, 3, 0.1);
    if (!outside || std::abs(*outside - 1.2) > 1e-6) return 1;
    if (actorRayDistance({ 2, 1.62, -2 }, { 0, 0, 1 }, low, high, 3, 0.1)) return 1;
    if (actorRayDistance({ 0, 1.62, -4 }, { 0, 0, 1 }, low, high, 3, 0.1)) return 1;
    const std::array<double, 3> origin { 0, 1.62, -2 }, direction { 0, 0, 1 };
    for (size_t axis = 0; axis < 3; ++axis) {
        for (double invalid : { std::numeric_limits<double>::quiet_NaN(),
                 std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() }) {
            auto invalidOrigin = origin, invalidDirection = direction, invalidLow = low, invalidHigh = high;
            invalidOrigin[axis] = invalidDirection[axis] = invalidLow[axis] = invalidHigh[axis] = invalid;
            if (actorRayDistance(invalidOrigin, direction, low, high, 3, 0.1)
                || actorRayDistance(origin, invalidDirection, low, high, 3, 0.1)
                || actorRayDistance(origin, direction, invalidLow, high, 3, 0.1)
                || actorRayDistance(origin, direction, low, invalidHigh, 3, 0.1)) return 1;
            if (actorRayDistance(origin, direction, low, high, invalid, 0.1)
                || actorRayDistance(origin, direction, low, high, 3, invalid)) return 1;
        }
        auto reversedLow = low;
        reversedLow[axis] = high[axis] + 1;
        if (enterBox(origin, direction, reversedLow, high)) return 1;
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::array<double, 3> invalidBox { nan, nan, nan };
    for (const auto& ray : { std::array<double, 3> { 0, -0.9396926, 0.3420201 },
             std::array<double, 3> { -0.5735764, -0.819152, 0 },
             std::array<double, 3> { 0, -0.9396926, -0.3420201 },
             std::array<double, 3> { 0.5735764, -0.819152, 0 } }) {
        if (actorRayDistance({ 0.5814918279647827, 64.62, 33.480167388916016 }, ray, invalidBox, invalidBox, 3, 0.1)) {
            std::fprintf(stderr, "An actor with NaN coordinates must not intercept block actions\n");
            return 1;
        }
    }
}
