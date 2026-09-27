#pragma once

#include "render/Renderer.h"

#include <array>
#include <cstdint>
#include <vector>

namespace kestrel {

struct SkyFrame {
    std::array<float, 3> zenith {};
    std::array<float, 3> horizon {};
    std::array<float, 3> fogColor {};
    float fogStart = 192.0f;
    float fogEnd = 256.0f;
    float daylight = 1.0f;
    std::array<float, 3> sunDirection {};
    uint32_t moonPhase = 0;
};

/**
 * Bedrock atmosphere from absolute world ticks and the rain and thunder
 * levels: sun and moon directions, a sky gradient darkened toward storm grey,
 * and distance fog that closes in as the weather worsens.
 */
SkyFrame atmosphereAt(double worldTicks, float renderDistance, float rainLevel, float thunderLevel);

std::vector<SkyVertex> buildSkyBackground(const SkyFrame& frame, uint32_t sunLayer, uint32_t moonLayer);

}
