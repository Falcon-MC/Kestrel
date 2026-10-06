#pragma once

#include <array>

namespace kestrel {

inline std::array<float, 12> boundAttachableFrame(std::array<float, 12> hand, const std::array<float, 3>& pivot)
{
    // Bound models inherit the hand pose around the humanoid's 24-pixel origin.
    const std::array<float, 3> offset { pivot[0], pivot[1] - 24.0f, pivot[2] };
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            hand[row * 4 + 3] += hand[row * 4 + axis] * offset[axis];
        }
    }
    return hand;
}

}
