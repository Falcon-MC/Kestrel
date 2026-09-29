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

struct Sidebar {
    bool visible = false;
    std::string title;
    std::vector<std::pair<std::string, int32_t>> lines;
};

}
