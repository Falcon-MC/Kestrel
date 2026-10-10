#pragma once

#include <array>
#include <cstdint>

namespace kestrel {

struct ActorMotion {
    uint64_t teleports = 0;
    std::array<double, 3> shown {};
    float bodyYaw = 0.0f;
    double lastFrame = 0.0;
    std::array<double, 3> lastShown {};
};

}
