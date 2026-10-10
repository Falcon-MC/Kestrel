#pragma once

#include <array>
#include <cmath>

namespace kestrel::first_person {

using Vec3 = std::array<float, 3>;
constexpr float Pi = 3.14159265f;

using Mat4 = std::array<float, 16>;

inline Mat4 identity()
{
    return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
}

/**
 * Row-major product a * b, so b applies to a point first.
 */
inline Mat4 operator*(const Mat4& a, const Mat4& b)
{
    Mat4 out {};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            for (size_t k = 0; k < 4; ++k) {
                out[row * 4 + column] += a[row * 4 + k] * b[k * 4 + column];
            }
        }
    }
    return out;
}

inline Mat4 translation(float x, float y, float z)
{
    Mat4 m = identity();
    m[3] = x;
    m[7] = y;
    m[11] = z;
    return m;
}

inline Mat4 uniformScale(float s)
{
    Mat4 m = identity();
    m[0] = s;
    m[5] = s;
    m[10] = s;
    return m;
}

inline Mat4 rotationX(float degrees)
{
    float c = std::cos(degrees * Pi / 180.0f);
    float s = std::sin(degrees * Pi / 180.0f);
    return { 1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0, 0, 0, 0, 1 };
}

inline Mat4 rotationY(float degrees)
{
    float c = std::cos(degrees * Pi / 180.0f);
    float s = std::sin(degrees * Pi / 180.0f);
    return { c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, 0, 0, 0, 1 };
}

inline Mat4 rotationZ(float degrees)
{
    float c = std::cos(degrees * Pi / 180.0f);
    float s = std::sin(degrees * Pi / 180.0f);
    return { c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
}

inline Vec3 transformed(const Mat4& m, const Vec3& p)
{
    return { m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3],
        m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
        m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11] };
}

inline Vec3 rigPoint(const Vec3& pixels, float scale, float eyeHeight)
{
    // The root lift follows model scale; the camera's eye offset does not.
    return { -pixels[0] * scale / 16.0f,
        pixels[1] * scale / 16.0f - eyeHeight + scale / 128.0f,
        -pixels[2] * scale / 16.0f };
}

inline Mat4 offhandPlacement(bool block, bool handEquipped)
{
    if (block) {
        return translation(-0.56f, -0.52f, -0.72f) * rotationY(135.0f) * uniformScale(0.4f);
    }
    Mat4 nativeFromHeld = translation(0.0f, 0.0f, 1.0f) * rotationY(180.0f) * rotationX(90.0f);
    Mat4 placement = handEquipped
        ? translation(-0.6875f, -0.125f, -1.53125f) * rotationY(-10.0f)
            * rotationX(70.0f) * rotationZ(80.0f)
        : rotationX(90.0f) * translation(-1.25f, -1.125f, 0.0f)
            * rotationZ(-80.0f) * rotationY(-20.0f) * translation(-0.3125f, 0.25f, -0.03125f);
    return placement * nativeFromHeld;
}

}
