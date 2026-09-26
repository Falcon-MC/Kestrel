#pragma once

#include "world/BlockAssets.h"

#include <array>
#include <cstdint>
#include <vector>

namespace kestrel::world::models {

enum Side : uint32_t {
    West = 0,
    East = 1,
    Down = 2,
    Up = 3,
    North = 4,
    South = 5,
};

using Point = std::array<int16_t, 3>;
using Materials = std::array<uint32_t, 6>;

uint32_t faceId(uint32_t side);

std::array<ModelQuad, 6> cuboid(const Materials& materials, Point min, Point max);
std::vector<ModelQuad> slab(const Materials& materials, uint32_t half);
std::vector<ModelQuad> stair(const Materials& materials, bool upsideDown, uint32_t shape);
std::vector<ModelQuad> cross(uint32_t first, uint32_t second);
std::vector<ModelQuad> fencePost(uint32_t material);
std::vector<ModelQuad> fenceArms(uint32_t material, uint32_t mask);
std::vector<ModelQuad> pane(uint32_t body, uint32_t edge, uint32_t mask);
std::vector<ModelQuad> wall(const Materials& materials, uint32_t connections);
std::pair<Point, Point> doorBounds(uint32_t orientation, uint32_t open, uint32_t hinge);
std::pair<Point, Point> trapdoorBounds(uint32_t orientation, uint32_t open, uint32_t half);
std::vector<ModelQuad> gate(const Materials& materials, uint32_t orientation, bool open, bool inWall, bool bamboo);
std::vector<ModelQuad> button(const Materials& materials, uint32_t orientation, bool pressed);
std::vector<ModelQuad> pressurePlate(const Materials& materials, bool pressed);
std::vector<ModelQuad> torch(uint32_t material, uint32_t facing);
std::vector<ModelQuad> flatPlane(uint32_t material, int16_t height);
std::vector<ModelQuad> attachedPlanes(uint32_t material, uint32_t sides);

}
