#include "world/assets/BlockRules.h"

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

ShapeBox cut(std::array<int16_t, 3> min, std::array<int16_t, 3> max, int side, Rects uvs, uint8_t hidden = 0, uint16_t uvSize = 16)
{
    ShapeBox box { min, max, side, nullptr };
    box.uvs = uvs;
    box.hidden = hidden;
    box.uvSize = uvSize;
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
    return name == "pale_moss_carpet" || name == "sniffer_egg" || name == "beacon" || name == "stonecutter_block" || name == "daylight_detector" || name == "daylight_detector_inverted"
        || name == "portal" || name == "powered_repeater" || name == "unpowered_repeater"
        || name == "powered_comparator" || name == "unpowered_comparator"
        || name == "end_portal_frame" || name == "heavy_core" || name == "lightning_rod" || endsWith(name, "_lightning_rod")
        || name == "enchanting_table" || name == "end_portal" || name == "hopper" || name == "brewing_stand" || name == "campfire" || name == "soul_campfire"
        || name == "lectern" || name == "frame" || name == "glow_frame" || name == "lever" || name == "cocoa" || name == "dragon_egg"
        || name == "decorated_pot" || isShelfName(name) || name == "leaf_litter"
        || name == "pink_petals" || name == "wildflowers" || name == "frog_spawn" || name == "flower_pot" || name == "end_rod"
        || name == "grindstone" || name == "bell" || name == "anvil" || name == "chipped_anvil" || name == "damaged_anvil";
}

BlockShape blockShape(const std::string& name, const Tag& states)
{
    BlockShape shape;
    if (name == "pale_moss_carpet") {
        const bool upper = stateInt(states, "upper_block_bit").value_or(0) != 0;
        if (!upper) shape.boxes.push_back(part({ 0, 0, 0 }, { 16, 1, 16 }));
        static constexpr const char* Keys[4] = { "pale_moss_carpet_side_north", "pale_moss_carpet_side_east", "pale_moss_carpet_side_south", "pale_moss_carpet_side_west" };
        static constexpr int Faces[4] = { South, West, North, East };
        for (size_t side = 0; side < 4; ++side) {
            const std::string height = stateString(states, Keys[side]);
            if (height != "short" && height != "tall") continue;
            ShapeBox panel = part({ 0, 0, 0 }, { 16, 16, 16 }, -1, "pale_moss_carpet_side");
            panel.textureVariant = height == "short" ? 1 : 0;
            panel.hidden = uint8_t(0x3F & ~(1u << Faces[side]));
            const size_t axis = side % 2 == 0 ? 2 : 0;
            if (side == 0 || side == 3) panel.max[axis] = 0;
            else panel.min[axis] = 16;
            panel.offset[axis] = side == 0 || side == 3 ? 2 : -2;
            panel.uvs = Rects { { { 0, 0, 16, 16 }, { 0, 0, 16, 16 }, {}, {}, { 0, 0, 16, 16 }, { 0, 0, 16, 16 } } };
            shape.boxes.push_back(panel);
        }
    } else if (name == "sniffer_egg") {
        shape.boxes = { cut({ 1, 0, 2 }, { 15, 16, 14 }, -1,
            { { { 0, 0, 12, 16 }, { 0, 0, 12, 16 }, { 0, 0, 14, 12 }, { 0, 0, 14, 12 }, { 0, 0, 14, 16 }, { 0, 0, 14, 16 } } }) };
    } else if (name == "stonecutter_block") {
        auto base = cut({ 0, 0, 0 }, { 16, 9, 16 }, -1,
            { { { 0, 7, 16, 16 }, { 0, 7, 16, 16 }, { 0, 0, 16, 16 }, { 0, 0, 16, 16 }, { 0, 7, 16, 16 }, { 0, 7, 16, 16 } } });
        for (int face : { West, East, North, South }) base.faceSides[face] = North;
        auto saw = cut({ 1, 9, 8 }, { 15, 16, 8 }, West,
            { { { 1, 9, 15, 16 }, { 1, 9, 15, 16 }, { 1, 9, 15, 16 }, { 1, 9, 15, 16 }, { 1, 9, 15, 16 }, { 15, 9, 1, 16 } } },
            uint8_t((1u << West) | (1u << East) | (1u << Up) | (1u << Down)));
        shape.boxes = { base, saw };
        shape.turns = cardinalTurns(states);
    } else if (name == "portal") {
        const bool alongX = stateString(states, "portal_axis") != "z";
        auto surface = part(alongX ? std::array<int16_t, 3> { 0, 0, 6 } : std::array<int16_t, 3> { 6, 0, 0 },
            alongX ? std::array<int16_t, 3> { 16, 16, 10 } : std::array<int16_t, 3> { 10, 16, 16 });
        surface.hidden = uint8_t(0x3F & ~(alongX ? (1u << North) | (1u << South) : (1u << West) | (1u << East)));
        shape.boxes = { surface };
    } else if (name == "powered_repeater" || name == "unpowered_repeater"
        || name == "powered_comparator" || name == "unpowered_comparator") {
        auto base = cut({ 0, 0, 0 }, { 16, 2, 16 }, -1,
            { { { 0, 14, 16, 16 }, { 0, 14, 16, 16 }, { 0, 0, 16, 16 }, { 0, 0, 16, 16 }, { 0, 14, 16, 16 }, { 0, 14, 16, 16 } } });
        shape.boxes.push_back(base);
        const bool powered = name == "powered_repeater" || name == "powered_comparator"
            || stateInt(states, "output_lit_bit").value_or(0) != 0;
        auto torch = [&](int16_t x, int16_t z, int16_t top, bool lit) {
            auto box = cut({ x, 2, z }, { int16_t(x + 2), top, int16_t(z + 2) }, -1,
                { { { 7, 6, 9, uint16_t(top + 4) }, { 7, 6, 9, uint16_t(top + 4) }, { 0, 0, 0, 0 }, { 7, 6, 9, 8 }, { 7, 6, 9, uint16_t(top + 4) }, { 7, 6, 9, uint16_t(top + 4) } } }, 1u << Down);
            box.texture = lit ? "redstone_torch_on" : "redstone_torch_off";
            shape.boxes.push_back(box);
        };
        if (name == "powered_repeater" || name == "unpowered_repeater") {
            torch(7, 2, 7, powered);
            const int16_t delay = int16_t(std::clamp(stateInt(states, "repeater_delay").value_or(0), 0, 3));
            torch(7, int16_t(6 + delay * 2), 7, powered);
        } else {
            torch(4, 11, 7, powered);
            torch(10, 11, 7, powered);
            torch(7, 2, 5, stateInt(states, "output_subtract_bit").value_or(0) != 0);
        }
        shape.turns = cardinalTurns(states) + 2;
    } else if (name == "heavy_core") {
        shape.boxes = {
            cut({ 4, 0, 4 }, { 12, 8, 12 }, -1,
                { { { 0, 8, 8, 16 }, { 0, 8, 8, 16 }, { 8, 0, 16, 8 }, { 0, 0, 8, 8 }, { 0, 8, 8, 16 }, { 0, 8, 8, 16 } } }),
        };
    } else if (name == "lightning_rod" || endsWith(name, "_lightning_rod")) {
        shape.boxes = {
            cut({ 6, 12, 6 }, { 10, 16, 10 }, -1,
                { { { 0, 0, 4, 4 }, { 0, 0, 4, 4 }, { 0, 0, 4, 4 }, { 4, 4, 0, 0 }, { 0, 0, 4, 4 }, { 0, 0, 4, 4 } } }),
            cut({ 7, 0, 7 }, { 9, 12, 9 }, -1,
                { { { 0, 4, 2, 16 }, { 0, 4, 2, 16 }, { 0, 4, 2, 6 }, { 0, 0, 0, 0 }, { 0, 4, 2, 16 }, { 0, 4, 2, 16 } } }, 1u << Up),
        };
        shape.facing = sideOfFacing(stateInt(states, "facing_direction").value_or(0));
    } else if (name == "end_portal") {
        shape.planeSide = Up;
        shape.planeHeight = 192;
    } else if (name == "end_portal_frame") {
        shape.boxes = { part({ 0, 0, 0 }, { 16, 13, 16 }) };
        if (stateInt(states, "end_portal_eye_bit").value_or(0) != 0) {
            auto eye = cut({ 4, 13, 4 }, { 12, 16, 12 }, -1,
                { { { 4, 0, 12, 4 }, { 4, 0, 12, 4 }, { 4, 4, 12, 12 }, { 4, 4, 12, 12 }, { 4, 0, 12, 4 }, { 4, 0, 12, 4 } } }, 1u << Down);
            eye.texture = "endframe_eye";
            shape.boxes.push_back(eye);
        }
        shape.turns = cardinalTurns(states);
    } else if (name == "daylight_detector" || name == "daylight_detector_inverted") {
        shape.boxes = { part({ 0, 0, 0 }, { 16, 6, 16 }) };
    } else if (name == "beacon") {
        shape.boxes = {
            part({ 1, 0, 1 }, { 15, 3, 15 }, Down),
            part({ 3, 3, 3 }, { 13, 13, 13 }, Up),
            part({ 0, 0, 0 }, { 16, 16, 16 }, North),
        };
    } else if (name == "enchanting_table") {
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
    } else if (isShelfName(name)) {
        // The shelf packs its front, back and edges into a 32px atlas.
        shape.boxes = {
            cut({ 0, 0, 13 }, { 16, 16, 16 }, -1,
                { { { 29, 0, 32, 16 }, { 16, 0, 19, 16 }, { 32, 12, 16, 9 }, { 32, 10, 16, 7 }, { 0, 0, 0, 0 }, { 16, 0, 32, 16 } } }, 1u << North, 32),
            cut({ 0, 0, 11 }, { 16, 4, 13 }, -1,
                { { { 11, 12, 13, 16 }, { 3, 12, 5, 16 }, { 32, 9, 16, 7 }, { 16, 7, 32, 9 }, { 0, 12, 16, 16 }, { 0, 0, 0, 0 } } }, 1u << South, 32),
            cut({ 0, 12, 11 }, { 16, 16, 13 }, -1,
                { { { 11, 0, 13, 4 }, { 3, 0, 5, 4 }, { 16, 10, 32, 12 }, { 32, 12, 16, 10 }, { 0, 0, 16, 4 }, { 0, 0, 0, 0 } } }, 1u << South, 32),
            cut({ 0, 4, 13 }, { 16, 12, 13 }, -1,
                { { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 4, 16, 12 }, { 0, 0, 0, 0 } } }, uint8_t(0x3F & ~(1u << North)), 32),
        };
        if (stateInt(states, "powered_bit").value_or(0) != 0) {
            static constexpr std::array<uint16_t, 4> Front[4] = {
                { 16, 24, 32, 32 }, { 0, 16, 16, 24 }, { 0, 24, 16, 32 }, { 16, 16, 32, 24 },
            };
            (*shape.boxes.back().uvs)[North] = Front[std::clamp(stateInt(states, "powered_shelf_type").value_or(0), 0, 3)];
        }
        shape.turns = cardinalTurns(states) + 2;
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
        shape.turns = cardinalTurns(states);
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
