#include "MotionMath.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>

namespace kestrel::motion {

namespace {

float floatSin(float value)
{
    static constexpr float SinCoefficients[] = { 1.5896230e-10f, -2.5050748e-8f, 2.7557314e-6f, -1.9841270e-4f, 8.333334e-3f, -1.6666667e-1f };
    static constexpr float CosCoefficients[] = { -1.1358537e-11f, 2.0875701e-9f, -2.7557314e-7f, 2.4801588e-5f, -1.3888889e-3f, 4.1666668e-2f };
    if (value == 0.0f || std::isnan(value)) {
        return value;
    }
    if (std::isinf(value)) {
        return NAN;
    }
    bool negative = false;
    if (value < 0.0f) {
        value = -value;
        negative = true;
    }
    int64_t octant = static_cast<int64_t>(value * (4.0f / Pi));
    float octantValue = static_cast<float>(octant);
    if ((octant & 1) == 1) {
        octant++;
        octantValue++;
    }
    octant &= 7;
    float r = value - octantValue * 0.7853981f;
    r = r - octantValue * 3.7748947e-8f;
    r = r - octantValue * 2.6951514e-15f;
    if (octant > 3) {
        negative = !negative;
        octant -= 4;
    }
    float squared = r * r;
    float result;
    if (octant == 1 || octant == 2) {
        float series = CosCoefficients[0] * squared + CosCoefficients[1];
        series = series * squared + CosCoefficients[2];
        series = series * squared + CosCoefficients[3];
        series = series * squared + CosCoefficients[4];
        series = series * squared + CosCoefficients[5];
        float half = 0.5f * squared;
        float fourth = squared * squared;
        result = 1.0f - half + fourth * series;
    } else {
        float series = SinCoefficients[0] * squared + SinCoefficients[1];
        series = series * squared + SinCoefficients[2];
        series = series * squared + SinCoefficients[3];
        series = series * squared + SinCoefficients[4];
        series = series * squared + SinCoefficients[5];
        float cube = r * squared;
        result = r + cube * series;
    }
    return negative ? -result : result;
}

const std::array<float, 65536>& sineTable()
{
    static const std::array<float, 65536> table = [] {
        std::array<float, 65536> values {};
        for (size_t index = 0; index < values.size(); ++index) {
            float angle = static_cast<float>(index) * Pi;
            angle = angle * 2.0f;
            angle = angle / 65536.0f;
            values[index] = floatSin(angle);
        }
        return values;
    }();
    return table;
}

struct ClipResult {
    MotionVector movement;
    int axis = 0;
    float penetration = 0.0f;
};

ClipResult clip(const world::CollisionBox& stationary, const world::CollisionBox& moving, const MotionVector& movement, bool oneWay)
{
    if (stationary.minX == stationary.maxX && stationary.minY == stationary.maxY && stationary.minZ == stationary.maxZ) {
        return { movement, 0, 0.0f };
    }
    float stationaryMinimum[3] = { stationary.minX, stationary.minY, stationary.minZ };
    float stationaryMaximum[3] = { stationary.maxX, stationary.maxY, stationary.maxZ };
    float movingMinimum[3] = { moving.minX, moving.minY, moving.minZ };
    float movingMaximum[3] = { moving.maxX, moving.maxY, moving.maxZ };
    float requested[3] = { movement.x, movement.y, movement.z };
    float penetration[3] = {};
    float signedPenetration[3] = {};
    float normal[3] = {};
    int separatingAxes = 0;
    int separatingAxis = 0;
    float minimumPenetration = FLT_MAX - 1.0f;

    for (int axis = 0; axis < 3; ++axis) {
        float minimum = movingMaximum[axis] - stationaryMinimum[axis];
        float maximum = stationaryMaximum[axis] - movingMinimum[axis];
        if (std::abs(minimum) <= 1.0E-7f) {
            minimum = 0.0f;
        }
        if (std::abs(maximum) <= 1.0E-7f) {
            maximum = 0.0f;
        }
        float positiveMinimum = std::max(0.0f, minimum);
        float positiveMaximum = std::max(0.0f, maximum);
        if (positiveMinimum == 0.0f) {
            signedPenetration[axis] = minimum;
            normal[axis] = -1.0f;
            separatingAxes++;
            separatingAxis = axis;
        } else if (positiveMaximum == 0.0f) {
            signedPenetration[axis] = maximum;
            normal[axis] = 1.0f;
            separatingAxes++;
            separatingAxis = axis;
        } else if (positiveMinimum < positiveMaximum) {
            penetration[axis] = positiveMinimum;
            signedPenetration[axis] = positiveMinimum;
            normal[axis] = -1.0f;
        } else {
            penetration[axis] = positiveMaximum;
            signedPenetration[axis] = positiveMaximum;
            normal[axis] = 1.0f;
        }
        if (separatingAxes > 1) {
            return { movement, 0, 0.0f };
        }
        minimumPenetration = std::min(minimumPenetration, penetration[axis]);
    }

    if (separatingAxes == 0) {
        int axis = 0;
        if (penetration[1] < penetration[axis]) {
            axis = 1;
        }
        if (penetration[2] < penetration[axis]) {
            axis = 2;
        }
        if (!oneWay) {
            float desired = penetration[axis] * normal[axis];
            requested[axis] = desired > 0.0f ? std::max(desired, requested[axis]) : std::min(desired, requested[axis]);
        }
        return { { requested[0], requested[1], requested[2] }, axis, minimumPenetration };
    }

    float product = normal[separatingAxis] * requested[separatingAxis];
    float sweptPenetration = signedPenetration[separatingAxis] - product;
    if (sweptPenetration <= 0.0f) {
        return { movement, 0, 0.0f };
    }
    requested[separatingAxis] = signedPenetration[separatingAxis] * normal[separatingAxis];
    return { { requested[0], requested[1], requested[2] }, 0, 0.0f };
}

}

void prepareSineTable()
{
    sineTable();
}

float sine(float value)
{
    return sineTable()[static_cast<size_t>(static_cast<int32_t>(value * 10430.378f) & 65535)];
}

float cosine(float value)
{
    return sineTable()[static_cast<size_t>(static_cast<int32_t>(value * 10430.378f + 16384.0f) & 65535)];
}

int32_t floorInt(float value)
{
    return static_cast<int32_t>(std::floor(value));
}

world::CollisionBox offset(const world::CollisionBox& box, const MotionVector& vector)
{
    return { box.minX + vector.x, box.minY + vector.y, box.minZ + vector.z, box.maxX + vector.x, box.maxY + vector.y, box.maxZ + vector.z };
}

world::CollisionBox grow(const world::CollisionBox& box, float x, float y, float z)
{
    return { box.minX - x, box.minY - y, box.minZ - z, box.maxX + x, box.maxY + y, box.maxZ + z };
}

world::CollisionBox extend(const world::CollisionBox& box, const MotionVector& vector)
{
    return {
        vector.x < 0.0f ? box.minX + vector.x : box.minX,
        vector.y < 0.0f ? box.minY + vector.y : box.minY,
        vector.z < 0.0f ? box.minZ + vector.z : box.minZ,
        vector.x > 0.0f ? box.maxX + vector.x : box.maxX,
        vector.y > 0.0f ? box.maxY + vector.y : box.maxY,
        vector.z > 0.0f ? box.maxZ + vector.z : box.maxZ,
    };
}

MotionVector feetOf(const world::CollisionBox& box)
{
    return { (box.minX + box.maxX) * 0.5f, box.minY, (box.minZ + box.maxZ) * 0.5f };
}

MotionVector clipAll(const std::vector<world::CollisionBox>& collisions, const world::CollisionBox& moving, MotionVector movement, bool oneWay, float* penetration)
{
    for (size_t index = collisions.size(); index-- > 0;) {
        ClipResult clipped = clip(collisions[index], moving, movement, oneWay);
        movement = clipped.movement;
        if (penetration && penetration[clipped.axis] < clipped.penetration) {
            penetration[clipped.axis] = clipped.penetration;
        }
    }
    return movement;
}

}
