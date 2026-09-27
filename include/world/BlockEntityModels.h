#pragma once

#include "world/BlockAssets.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace kestrel::world {

/**
 * One face of an entity box: a region of the entity texture in pixels, how it
 * is flipped and turned (quarter turns clockwise) onto the face, and whether
 * it takes the model's tint.
 */
struct EntityFace {
    float u = 0.0f;
    float v = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    uint8_t turns = 0;
    bool flipU = false;
    bool flipV = false;
    bool tinted = false;
    bool present = true;
};

/**
 * An axis-aligned box in block pixels (0..16 spans one block). Faces are
 * indexed like Face: west, east, down, up, north, south.
 */
struct EntityBox {
    std::array<float, 3> min {};
    std::array<float, 3> max {};
    std::array<EntityFace, 6> faces {};
};

struct EntityImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

std::vector<EntityBox> chestBoxes(bool twoBlocks, float offsetX);
std::vector<EntityBox> bedBoxes(bool head);
std::vector<EntityBox> skullBoxes(bool wall);
std::vector<EntityBox> bannerBoxes(bool wall);

/**
 * Samples one face region into a 16x16 RGBA layer, applying its flips and
 * turns, and multiplies tinted faces by the RGB tint.
 */
std::vector<uint8_t> sliceEntityFace(const EntityImage& image, const EntityFace& face, uint32_t tint);

/**
 * Builds model quads for the boxes turned by yaw degrees around the block
 * center; yaw follows block rotations, so 90 turns a south-facing model west.
 */
std::vector<ModelQuad> buildEntityQuads(const std::vector<EntityBox>& boxes, float yawDegrees, const std::function<uint32_t(const EntityFace&)>& material);

}
