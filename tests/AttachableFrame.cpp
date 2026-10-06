#include "client/AttachableFrame.h"

#include <cmath>

namespace {
std::array<float, 3> transform(const std::array<float, 12>& frame, const std::array<float, 3>& point)
{
    std::array<float, 3> result {};
    for (std::size_t row = 0; row < 3; ++row) {
        result[row] = frame[row * 4 + 3];
        for (std::size_t axis = 0; axis < 3; ++axis) result[row] += frame[row * 4 + axis] * point[axis];
    }
    return result;
}
}

int main()
{
    const std::array<std::array<float, 12>, 3> poses {{
        { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 },
        { 0, -0.5f, 0, 2, 0.5f, 0, 0, 0.75f, 0, 0, 0.5f, -3 },
        { 0, 0, 2, -4, 0, -3, 0, 8, 0.25f, 0, 0, 9 },
    }};
    for (const auto& hand : poses) {
        for (float side : { -1.0f, 1.0f }) {
            const std::array<float, 3> pivot { side * 6, 12, 1 };
            const auto attached = kestrel::boundAttachableFrame(hand, pivot);
            const auto origin = transform(attached, { 0, 24, 0 });
            const auto fist = transform(hand, pivot);
            const auto tip = transform(attached, { 0, 16, 0 });
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (std::abs(origin[axis] - fist[axis]) > 1e-5f) return 1;
                if (std::abs(tip[axis] - origin[axis] + 8 * hand[axis * 4 + 1]) > 1e-5f) return 2;
                for (std::size_t column = 0; column < 3; ++column) {
                    if (attached[axis * 4 + column] != hand[axis * 4 + column]) return 3;
                }
            }
        }
    }
}
