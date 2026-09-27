#include "BlockRules.h"

#include "util/Text.h"

#include <algorithm>

namespace kestrel::world::rules {

using util::contains;
using util::endsWith;

namespace {

constexpr int Down = 2;
constexpr int Up = 3;
constexpr int North = 4;

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
    return name == "enchanting_table" || name == "hopper" || name == "brewing_stand" || name == "campfire" || name == "soul_campfire"
        || name == "lectern" || name == "frame" || name == "glow_frame" || name == "lever" || name == "cocoa" || name == "dragon_egg"
        || name == "decorated_pot" || contains(name, "copper_golem_statue") || isShelfName(name) || name == "leaf_litter"
        || name == "pink_petals" || name == "wildflowers";
}

BlockShape blockShape(const std::string& name, const Tag& states)
{
    BlockShape shape;
    if (name == "enchanting_table") {
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
    } else if (name == "leaf_litter" || name == "pink_petals" || name == "wildflowers") {
        shape.planeSide = Up;
    }
    return shape;
}

}
