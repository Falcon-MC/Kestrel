#pragma once

#include "render/Renderer.h"

#include <array>
#include <cstdint>
#include <vector>

namespace kestrel {

namespace world { struct BiomeFog; }

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
SkyFrame foggedBy(const SkyFrame& frame, const world::BiomeFog& fog, float renderDistance,
    float submergedSeconds = 30.0f);

/**
 * The atmosphere seen from inside a block: medium 1 is water, 2 is lava and 3 is powder snow,
 * each with its own fog colour closing in from the camera; 0 leaves the frame
 * unchanged.
 */
SkyFrame submergedIn(const SkyFrame& frame, uint8_t medium, float submergedSeconds = 30.0f,
    uint32_t waterColor = 0x44AFF5, float waterStart = 0.0f, float waterEnd = 60.0f,
    const world::BiomeFog* lavaFog = nullptr, float renderDistance = 1.0f,
    const world::BiomeFog* powderSnowFog = nullptr);

std::vector<SkyVertex> buildSkyBackground(const SkyFrame& frame, uint32_t sunLayer, uint32_t moonLayer);

}
