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
    float cloudOffset = 0.0f;
};

/**
 * Bedrock atmosphere from absolute world ticks: sun and moon directions, sky
 * gradient, distance fog and eastward cloud drift.
 */
SkyFrame atmosphereAt(double worldTicks, float renderDistance);

std::vector<SkyVertex> buildSkyBackground(const SkyFrame& frame, uint32_t sunLayer, uint32_t moonLayer);
std::vector<SkyVertex> buildCloudMesh(const std::vector<uint8_t>& mask);
std::vector<std::array<float, 3>> cloudTileOrigins(const SkyFrame& frame, double cameraX, double cameraY, double cameraZ);

}
