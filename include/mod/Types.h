#pragma once

#include "platform/Keys.h"
#include "ui/Types.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kestrel::mod {

using Key = kestrel::Key;
using Color = ui::Color;
using Rect = ui::Rect;

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    bool operator==(const Vec3&) const = default;
};

struct BlockPos {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    bool operator==(const BlockPos&) const = default;
};

/**
 * Minecraft angles in degrees: yaw 0 looks south and grows clockwise, pitch
 * is negative looking up.
 */
struct Rotation {
    float yaw = 0.0f;
    float pitch = 0.0f;
};

/**
 * What a mod says about itself. The id names its data folder and prefixes
 * its log lines, so keep it short and stable.
 */
struct ModInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
};

struct ItemStack {
    std::string identifier;
    int32_t count = 0;
    int32_t aux = 0;
    int32_t damage = 0;
    std::string customName;
    std::vector<std::string> lore;
    bool enchanted = false;

    bool empty() const
    {
        return identifier.empty() || count <= 0;
    }
};

enum class ConnectionState {
    Idle,
    Resolving,
    Connecting,
    Joined,
    Disconnected,
    Failed,
};

/**
 * A status effect on the local player. secondsLeft is negative for effects
 * that never run out.
 */
struct StatusEffect {
    int32_t id = 0;
    int32_t amplifier = 0;
    double secondsLeft = -1.0;
    bool ambient = false;
};

struct Entity {
    uint64_t runtimeId = 0;
    std::string identifier;
    std::string name;
    Vec3 position;
    Rotation rotation;
    float headYaw = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float scale = 1.0f;
    bool onGround = true;
    // Helmet, chestplate, leggings and boots, empty when bare.
    std::array<std::string, 4> armor {};
    // Only set for dropped items.
    ItemStack item;

    bool isPlayer() const
    {
        return identifier == "minecraft:player";
    }
};

struct TargetBlock {
    BlockPos position;
    std::string name;
    // Each state as "name: value".
    std::vector<std::string> states;
};

/**
 * A block of a loaded chunk: its name and its states, each as "name: value".
 */
struct BlockInfo {
    BlockPos position;
    std::string name;
    std::vector<std::string> states;
};

/**
 * A block World::findBlocks found: where it is and its full name.
 */
struct FoundBlock {
    BlockPos position;
    std::string name;
};

/**
 * What a ray met first. For a block, face is the side it entered through:
 * 0 down, 1 up, 2 north, 3 south, 4 west, 5 east. For an entity, entity is
 * its runtime id.
 */
struct RaycastHit {
    enum class Kind {
        Block,
        Entity,
    };

    Kind kind = Kind::Block;
    Vec3 point;
    double distance = 0.0;
    BlockPos block;
    int face = 0;
    uint64_t entity = 0;
    std::string name;
};

/**
 * What the sky and the camera look like this frame, for shaders that light
 * or fog the world. sunDirection points at the sun (the moon is opposite),
 * daylight runs from 0 at night to 1 at noon, medium is 0 in air, 1 in water
 * 2 in lava and 3 in powder snow.
 */
struct Environment {
    Vec3 camera;
    std::array<float, 3> sunDirection { 0.0f, 1.0f, 0.0f };
    float daylight = 1.0f;
    std::array<float, 3> fogColor { 0.6f, 0.75f, 1.0f };
    float fogStart = 192.0f;
    float fogEnd = 256.0f;
    float rain = 0.0f;
    float thunder = 0.0f;
    int medium = 0;
    float nightVision = 0.0f;
    uint32_t moonPhase = 0;
};

/**
 * An axis aligned box in world coordinates.
 */
struct Box {
    Vec3 min;
    Vec3 max;
};

/**
 * What a block is. solid means it has collision boxes, fullCube that they
 * make exactly one whole block. liquidLevel is the liquid's depth, 0 for a
 * source, or -1 without a liquid. hazard marks blocks that hurt or trap the
 * player; hardness is negative for unbreakable blocks, and friction is how
 * slippery the block is underfoot (0.6 for most).
 */
struct BlockProps {
    bool air = true;
    bool solid = false;
    bool fullCube = false;
    bool liquid = false;
    bool water = false;
    bool lava = false;
    bool climbable = false;
    bool hazard = false;
    bool replaceable = true;
    bool gravity = false;
    int liquidLevel = -1;
    float hardness = 0.0f;
    float friction = 0.6f;
};

struct Sidebar {
    bool visible = false;
    std::string title;
    std::vector<std::pair<std::string, int32_t>> lines;
};

/**
 * A boss bar at the top of the screen. color is the game's index: 0 pink,
 * 1 blue, 2 red, 3 green, 4 yellow, 5 purple, 6 white.
 */
struct BossBar {
    std::string title;
    float progress = 1.0f;
    int32_t color = 0;
};

}
