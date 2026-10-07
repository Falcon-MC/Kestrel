#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace kestrel::world {

struct CrystalBeamQuad {
    std::array<std::array<float, 3>, 4> positions;
    std::array<std::array<float, 2>, 4> uvs;
    std::array<float, 4> brightness;
};

template<class Emit>
void crystalBeamQuads(const std::array<double, 3>& crystal, const std::array<int32_t, 3>& block, Emit emit)
{
    if (block == std::array<int32_t, 3>{}) return;
    std::array<double, 3> target { double(block[0]), double(block[1]) + 1.0, double(block[2]) };
    std::array<double, 3> direction;
    double length = 0.0;
    for (size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(crystal[axis])) return;
        direction[axis] = crystal[axis] - target[axis];
        length += direction[axis] * direction[axis];
    }
    length = std::sqrt(length);
    // Bound both tessellation work and the range of packed entity positions.
    if (length < 0.0001 || length > 4096.0) return;
    for (double& component : direction) component /= length;
    std::array<double, 3> tangent { -direction[0] * direction[1], direction[0] * direction[0] + direction[2] * direction[2], -direction[2] * direction[1] };
    double tangentLength = std::hypot(tangent[0], tangent[1], tangent[2]);
    if (tangentLength < 0.0001) tangent = { 1.0, 0.0, 0.0 };
    else for (double& component : tangent) component /= tangentLength;
    std::array<double, 3> normal {
        direction[1] * tangent[2] - direction[2] * tangent[1],
        direction[2] * tangent[0] - direction[0] * tangent[2],
        direction[0] * tangent[1] - direction[1] * tangent[0]
    };
    int segments = std::max(1, int(std::ceil(length / 32.0)));
    for (int segment = 0; segment < segments; ++segment) {
        float low = float(segment) / segments, high = float(segment + 1) / segments;
        auto point = [&](int side, float along) {
            double angle = side * (6.283185307179586 / 8.0);
            double radius = 0.15 + 0.6 * along;
            std::array<float, 3> result;
            for (size_t axis = 0; axis < 3; ++axis) {
                result[axis] = float(target[axis] + direction[axis] * length * along
                    + radius * (tangent[axis] * std::sin(angle) + normal[axis] * std::cos(angle)) - crystal[axis]);
            }
            return result;
        };
        for (int side = 0; side < 8; ++side) {
            CrystalBeamQuad quad;
            quad.positions = { point(side, low), point(side + 1, low), point(side + 1, high), point(side, high) };
            quad.uvs = {{{ float(side) / 8, 0 }, { float(side + 1) / 8, 0 }, { float(side + 1) / 8, high - low }, { float(side) / 8, high - low }}};
            quad.brightness = { low, low, high, high };
            emit(quad, low);
        }
    }
}

}
