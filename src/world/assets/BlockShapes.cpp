#include "BlockRules.h"

#include "util/Text.h"

#include <algorithm>

namespace kestrel::world::rules {

using util::contains;
using util::endsWith;

namespace {

constexpr int West = 0;
constexpr int East = 1;
constexpr int Down = 2;
constexpr int Up = 3;
constexpr int North = 4;
constexpr int South = 5;

using Rects = std::array<std::array<uint16_t, 4>, 6>;

ShapeBox cut(std::array<int16_t, 3> min, std::array<int16_t, 3> max, int side, Rects uvs, uint8_t hidden = 0)
{
    ShapeBox box { min, max, side, nullptr };
    box.uvs = uvs;
    box.hidden = hidden;
    return box;
}

// Bedrock's facing_direction order: down, up, north, south, west, east.
int sideOfFacing(int32_t facing)
{
    static constexpr int Sides[6] = { Down, Up, North, South, West, East };
    return Sides[std::clamp(facing, 0, 5)];
}

ShapeBox part(std::array<int16_t, 3> min, std::array<int16_t, 3> max, int side = -1, const char* texture = nullptr)
{
    return { min, max, side, texture };
}

uint32_t cardinalTurns(const Tag& states)
{
    std::string cardinal = stateString(states, "minecraft:cardinal_direction");
    if (!cardinal.empty()) {
        return facingRotation(cardinal);
    }
    return static_cast<uint32_t>(stateInt(states, "direction").value_or(0) & 3);
}

}

bool isShapeName(const std::string& name)
{
    return name == "enchanting_table" || name == "end_portal" || name == "hopper" || name == "brewing_stand" || name == "campfire" || name == "soul_campfire"
        || name == "lectern" || name == "frame" || name == "glow_frame" || name == "lever" || name == "cocoa" || name == "dragon_egg"
        || name == "decorated_pot" || contains(name, "copper_golem_statue") || isShelfName(name) || name == "leaf_litter"
        || name == "pink_petals" || name == "wildflowers" || name == "frog_spawn" || name == "flower_pot" || name == "end_rod"
        || name == "grindstone" || name == "bell" || name == "anvil" || name == "chipped_anvil" || name == "damaged_anvil";
}

BlockShape blockShape(const std::string& name, const Tag& states)
{
    BlockShape shape;
    if (name == "enchanting_table" || name == "end_portal") {
        shape.boxes = { part({ 0, 0, 0 }, { 16, 12, 16 }) };
    } else if (name == "hopper") {
        shape.boxes = {
            part({ 0, 10, 0 }, { 16, 16, 16 }),
            part({ 4, 4, 4 }, { 12, 10, 12 }, North),
            part({ 6, 0, 6 }, { 10, 4, 10 }, North),
        };
    } else if (name == "brewing_stand") {
        shape.boxes = {
            part({ 7, 0, 7 }, { 9, 14, 9 }, Up),
            part({ 9, 0, 5 }, { 15, 2, 11 }, Down),
            part({ 1, 0, 1 }, { 7, 2, 7 }, Down),
            part({ 1, 0, 9 }, { 7, 2, 15 }, Down),
        };
        shape.crossSide = Up;
    } else if (name == "campfire" || name == "soul_campfire") {
        bool lit = stateInt(states, "extinguished").value_or(0) == 0;
        int log = lit ? North : Down;
        shape.boxes = {
            part({ 1, 0, 0 }, { 5, 4, 16 }, log),
            part({ 11, 0, 0 }, { 15, 4, 16 }, log),
            part({ 0, 3, 1 }, { 16, 7, 5 }, log),
            part({ 0, 3, 11 }, { 16, 7, 15 }, log),
            part({ 5, 0, 0 }, { 11, 1, 16 }, Down),
        };
        shape.crossSide = lit ? Up : -1;
        shape.turns = cardinalTurns(states);
    } else if (name == "lectern") {
        shape.boxes = {
            part({ 0, 0, 0 }, { 16, 2, 16 }),
            part({ 4, 2, 4 }, { 12, 13, 12 }),
            part({ 0, 12, 1 }, { 16, 16, 15 }),
        };
        shape.turns = cardinalTurns(states);
    } else if (name == "frame" || name == "glow_frame") {
        int32_t facing = stateInt(states, "facing_direction").value_or(3);
        if (facing == 0) {
            shape.boxes = { part({ 2, 15, 2 }, { 14, 16, 14 }, Up) };
        } else if (facing == 1) {
            shape.boxes = { part({ 2, 0, 2 }, { 14, 1, 14 }, Up) };
        } else {
            shape.boxes = { part({ 2, 2, 0 }, { 14, 14, 1 }, Up) };
            shape.turns = facingDirectionRotation(facing);
        }
    } else if (name == "lever") {
        std::string direction = stateString(states, "lever_direction");
        if (direction.rfind("up_", 0) == 0) {
            shape.boxes = { part({ 5, 0, 4 }, { 11, 3, 12 }, -1, "cobblestone"), part({ 7, 3, 7 }, { 9, 11, 9 }, Up) };
            shape.turns = direction == "up_east_west" ? 1 : 0;
        } else if (direction.rfind("down_", 0) == 0) {
            shape.boxes = { part({ 5, 13, 4 }, { 11, 16, 12 }, -1, "cobblestone"), part({ 7, 5, 7 }, { 9, 13, 9 }, Up) };
            shape.turns = direction == "down_east_west" ? 1 : 0;
        } else {
            shape.boxes = { part({ 5, 4, 0 }, { 11, 12, 3 }, -1, "cobblestone"), part({ 7, 7, 3 }, { 9, 9, 11 }, Up) };
            shape.turns = facingRotation(direction);
        }
    } else if (name == "cocoa") {
        int16_t age = static_cast<int16_t>(std::clamp(stateInt(states, "age").value_or(0), 0, 2));
        int16_t width = static_cast<int16_t>(4 + age * 2);
        int16_t height = static_cast<int16_t>(5 + age * 2);
        int16_t left = static_cast<int16_t>(8 - width / 2);
        shape.boxes = { part({ left, static_cast<int16_t>(12 - height), static_cast<int16_t>(15 - width) }, { static_cast<int16_t>(left + width), 12, 15 }) };
        shape.turns = static_cast<uint32_t>(stateInt(states, "direction").value_or(0) & 3);
    } else if (name == "dragon_egg") {
        static constexpr int16_t Layers[8][3] = { { 0, 1, 3 }, { 1, 3, 2 }, { 3, 8, 1 }, { 8, 11, 2 }, { 11, 13, 3 }, { 13, 14, 4 }, { 14, 15, 5 }, { 15, 16, 6 } };
        for (const auto& [bottom, top, inset] : Layers) {
            shape.boxes.push_back(part({ inset, bottom, inset }, { static_cast<int16_t>(16 - inset), top, static_cast<int16_t>(16 - inset) }));
        }
    } else if (name == "decorated_pot") {
        shape.boxes = { part({ 1, 0, 1 }, { 15, 13, 15 }), part({ 4, 13, 4 }, { 12, 16, 12 }) };
        shape.turns = cardinalTurns(states);
    } else if (contains(name, "copper_golem_statue")) {
        shape.boxes = { part({ 4, 0, 5 }, { 12, 6, 11 }), part({ 3, 6, 4 }, { 13, 12, 12 }), part({ 7, 12, 7 }, { 9, 16, 9 }) };
        shape.turns = cardinalTurns(states);
    } else if (isShelfName(name)) {
        shape.boxes = { part({ 0, 0, 0 }, { 16, 16, 8 }) };
        shape.turns = cardinalTurns(states);
    } else if (name == "leaf_litter" || name == "pink_petals" || name == "wildflowers" || name == "frog_spawn") {
        shape.planeSide = Up;
    } else if (name == "flower_pot") {
        shape.boxes = {
            part({ 5, 0, 5 }, { 6, 6, 11 }),
            part({ 10, 0, 5 }, { 11, 6, 11 }),
            part({ 6, 0, 5 }, { 10, 6, 6 }),
            part({ 6, 0, 10 }, { 10, 6, 11 }),
            part({ 6, 0, 6 }, { 10, 4, 10 }, -1, "dirt"),
        };
    } else if (name == "end_rod") {
        // Cut from end_rod.png the way Java's end_rod model is: a 4x4 base and a 2x2 rod.
        shape.boxes = {
            cut({ 6, 0, 6 }, { 10, 1, 10 }, -1, { { { 2, 6, 6, 7 }, { 2, 6, 6, 7 }, { 2, 2, 6, 6 }, { 2, 2, 6, 6 }, { 2, 6, 6, 7 }, { 2, 6, 6, 7 } } }),
            cut({ 7, 1, 7 }, { 9, 16, 9 }, -1, { { { 0, 0, 2, 15 }, { 0, 0, 2, 15 }, { 2, 0, 4, 2 }, { 2, 0, 4, 2 }, { 0, 0, 2, 15 }, { 0, 0, 2, 15 } } }, 1u << Down),
        };
        // Bedrock stores sideways rods pointing the other way round.
        int32_t facing = stateInt(states, "facing_direction").value_or(1);
        shape.facing = sideOfFacing(facing >= 2 ? facing ^ 1 : facing);
    } else if (name == "grindstone") {
        // blocks.json gives the wheel round on top, its sides on east, the pivots north and the legs below.
        ShapeBox wheel = cut({ 4, 4, 2 }, { 12, 16, 14 }, -1, { { { 0, 0, 12, 12 }, { 0, 0, 12, 12 }, { 0, 0, 8, 12 }, { 0, 0, 8, 12 }, { 0, 0, 8, 12 }, { 0, 0, 8, 12 } } });
        wheel.faceSides = { East, East, Up, Up, Up, Up };
        Rects pivot { { { 0, 0, 6, 6 }, { 0, 0, 6, 6 }, { 6, 0, 8, 6 }, { 6, 0, 8, 6 }, { 6, 0, 8, 6 }, { 6, 0, 8, 6 } } };
        shape.boxes = {
            wheel,
            cut({ 12, 7, 5 }, { 14, 13, 11 }, North, pivot),
            cut({ 2, 7, 5 }, { 4, 13, 11 }, North, pivot),
            part({ 12, 0, 6 }, { 14, 7, 10 }, Down),
            part({ 2, 0, 6 }, { 4, 7, 10 }, Down),
        };
        std::string attachment = stateString(states, "attachment");
        shape.facing = attachment == "hanging" ? Down : attachment == "side" ? South : -1;
        shape.turns = static_cast<uint32_t>(stateInt(states, "direction").value_or(0) & 3);
    } else if (name == "anvil" || name == "chipped_anvil" || name == "damaged_anvil") {
        shape.boxes = {
            part({ 2, 0, 2 }, { 14, 4, 14 }, Down),
            part({ 4, 4, 3 }, { 12, 5, 13 }, Down),
            part({ 6, 5, 4 }, { 10, 10, 12 }, Down),
            part({ 3, 10, 0 }, { 13, 16, 16 }),
        };
        // The horn runs across the way the player faced when placing it.
        shape.turns = cardinalTurns(states) + 1;
    } else if (name == "bell") {
        // blocks.json: the bell's top, bottom and sides, dark oak planks for the bar and stone for the posts.
        ShapeBox body = cut({ 5, 6, 5 }, { 11, 13, 11 }, North, { { { 1, 0, 7, 7 }, { 1, 0, 7, 7 }, { 1, 1, 7, 7 }, { 1, 1, 7, 7 }, { 1, 0, 7, 7 }, { 1, 0, 7, 7 } } }, 1u << Down);
        body.faceSides = { North, North, Down, Up, North, North };
        ShapeBox lip = cut({ 4, 4, 4 }, { 12, 6, 12 }, North, { { { 0, 7, 8, 9 }, { 0, 7, 8, 9 }, { 0, 0, 8, 8 }, { 0, 0, 8, 8 }, { 0, 7, 8, 9 }, { 0, 7, 8, 9 } } });
        lip.faceSides = { North, North, Down, Up, North, North };
        shape.boxes = { body, lip };
        std::string attachment = stateString(states, "attachment");
        if (attachment == "hanging") {
            shape.boxes.push_back(part({ 7, 13, 7 }, { 9, 16, 9 }, East));
        } else if (attachment == "side") {
            shape.boxes.push_back(part({ 7, 13, 3 }, { 9, 15, 16 }, East));
        } else if (attachment == "multiple") {
            shape.boxes.push_back(part({ 7, 13, 0 }, { 9, 15, 16 }, East));
        } else {
            shape.boxes.push_back(part({ 2, 0, 6 }, { 4, 16, 10 }, West));
            shape.boxes.push_back(part({ 12, 0, 6 }, { 14, 16, 10 }, West));
            shape.boxes.push_back(part({ 4, 12, 7 }, { 12, 14, 9 }, East));
        }
        shape.turns = static_cast<uint32_t>(stateInt(states, "direction").value_or(0) & 3);
    }
    return shape;
}

}
