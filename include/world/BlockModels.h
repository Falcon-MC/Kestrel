#pragma once

#include "world/BlockAssets.h"

#include <array>
#include <cstdint>
#include <optional>
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
std::vector<ModelQuad> honey(const Materials& materials);
std::vector<ModelQuad> pistonBody(const Materials& materials, uint32_t inner, uint32_t facing);
std::vector<ModelQuad> pistonHead(const Materials& materials, uint32_t normal, uint32_t facing, bool extended);
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
std::vector<ModelQuad> lantern(uint32_t material, bool hanging);
std::vector<ModelQuad> candles(uint32_t material, uint32_t count);
std::vector<ModelQuad> turtleEggs(uint32_t material, uint32_t count);

/**
 * A cauldron built from the block's faces: north is the outer wall, south the
 * inner one, and the liquid surface sits at level out of six when above zero.
 */
std::vector<ModelQuad> cauldron(const Materials& materials, uint32_t liquid, uint32_t level);
std::vector<ModelQuad> bamboo(uint32_t stem, uint32_t leaves, bool thick);
/**
 * Turns quads built pointing up to point toward facing, one of the six sides,
 * keeping each face's shading and culling side in step.
 */
std::vector<ModelQuad> orient(std::vector<ModelQuad> quads, uint32_t facing);
std::vector<ModelQuad> orientedCross(uint32_t material, uint32_t facing);
std::vector<ModelQuad> orientedCross(uint32_t first, uint32_t second, uint32_t facing);
std::vector<ModelQuad> chain(uint32_t first, uint32_t second, uint32_t facing);
std::vector<ModelQuad> standingSign(uint32_t material, uint32_t rotation);
std::vector<ModelQuad> wallSign(uint32_t material, uint32_t facing);
std::vector<ModelQuad> hangingWallSign(uint32_t material, uint32_t facing);
std::vector<ModelQuad> hangingCeilingSign(uint32_t material, uint32_t rotation, bool attached);
std::vector<ModelQuad> flatPlane(uint32_t material, int16_t height);

struct ShapePart {
    Point min {};
    Point max {};
    Materials materials {};
    // Texture rects in pixels per side, for boxes cut from part of a texture; none projects
    // them from the box's place in the block.
    std::optional<std::array<std::array<uint16_t, 4>, 6>> uvs;
    // A bit per side left out, like the hidden bottom of an end rod's rod.
    uint8_t hidden = 0;
    uint16_t uvSize = 16;
};

std::vector<ModelQuad> azalea(const Materials& materials);
std::vector<ModelQuad> pottedPlant(std::string_view name, const Materials& materials, uint32_t leaves);
std::vector<ModelQuad> lectern(const Materials& materials, uint32_t turns);
std::vector<ModelQuad> candleCake(const Materials& materials, uint32_t candleMaterial);
std::vector<ModelQuad> sculkSensor(const Materials& materials, uint32_t tendril, std::optional<uint32_t> amethyst, uint32_t turns);
std::vector<ModelQuad> sculkShrieker(const Materials& materials, uint32_t inner);
std::vector<ModelQuad> seaPickles(uint32_t material, uint32_t count, bool dead);
std::vector<ModelQuad> chorus(const Materials& materials, uint32_t connections);
std::vector<ModelQuad> dripleaf(const Materials& materials, bool head, uint32_t tilt, uint32_t turns);
std::vector<ModelQuad> smallDripleaf(const Materials& materials, bool upper, uint32_t turns);
std::vector<ModelQuad> coralFan(uint32_t material, bool wall, uint32_t turns);
std::vector<ModelQuad> sporeBlossom(uint32_t base, uint32_t petals);
std::vector<ModelQuad> sunflower(uint32_t stem, uint32_t front, uint32_t back, bool upper);
std::vector<ModelQuad> cropStem(uint32_t material, uint32_t growth, int32_t facing);
std::vector<ModelQuad> pitcherCrop(const Materials& materials, uint32_t growth, bool upper);
std::vector<ModelQuad> driedGhast(const Materials& materials, uint32_t tentacles, uint32_t turns);
std::vector<ModelQuad> strawBed(uint32_t material, bool head, uint32_t turns);
std::vector<ModelQuad> redstoneWire(uint32_t cross, uint32_t line, uint32_t connections);
std::vector<ModelQuad> tripwire(uint32_t material, uint32_t connections, bool attached, bool suspended);
std::vector<ModelQuad> tripwireHook(const Materials& materials, bool attached, bool powered, uint32_t turns);

/**
 * Boxes plus any extra quads, all turned by quarter turns the way signs turn:
 * one turn takes a south facing model to west.
 */
std::vector<ModelQuad> shape(const std::vector<ShapePart>& parts, std::vector<ModelQuad> extra, uint32_t turns);
std::vector<ModelQuad> attachedPlanes(uint32_t material, uint32_t sides);

}
