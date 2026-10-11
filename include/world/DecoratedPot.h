#pragma once

#include "Core/NBT/Tag.h"

#include <array>
#include <cmath>
#include <string_view>

namespace kestrel::world {

inline constexpr const char* PotMovingKey = "KestrelPotMoving";
inline constexpr std::array<std::string_view, 23> PotPatterns {
    "angler", "archer", "arms_up", "blade", "brewer", "burn", "danger", "explorer", "friend", "heart", "heartbreak",
    "howl", "miner", "mourner", "plenty", "prize", "sheaf", "shelter", "skull", "snort", "flow", "guster", "scrape",
};

inline std::array<uint8_t, 4> potPatterns(const Tag& data)
{
    std::array<uint8_t, 4> patterns {};
    const Tag* sherds = data.get("sherds");
    if (!sherds || !sherds->isList()) return patterns;
    const auto& entries = sherds->getList();
    for (size_t side = 0; side < patterns.size() && side < entries.size(); ++side) {
        if (entries[side].getType() != Tag::Type::String) continue;
        std::string_view name = entries[side].asString();
        if (!name.starts_with("minecraft:") || !name.ends_with("_pottery_sherd")) continue;
        name.remove_prefix(10);
        name.remove_suffix(14);
        for (size_t pattern = 0; pattern < PotPatterns.size(); ++pattern) {
            if (name == PotPatterns[pattern]) {
                patterns[side] = uint8_t(pattern + 1);
                break;
            }
        }
    }
    return patterns;
}

inline std::array<float, 3> potPosition(std::array<float, 3> point, float yaw, int animation, double elapsed)
{
    float x = point[0] - 8;
    float y = point[1];
    float z = point[2] - 8;
    const double duration = animation == 2 ? 0.35 : 0.5;
    if (animation && elapsed >= 0 && elapsed < duration) {
        const float progress = float(elapsed / duration);
        if (animation == 2) {
            const float phase = progress * 6.2831853f;
            const float pitch = -1.5f * (std::cos(phase) + 0.5f) * std::sin(phase * 0.5f) / 64;
            const float roll = std::sin(phase) / 64;
            const float lifted = y * std::cos(pitch) - z * std::sin(pitch);
            z = y * std::sin(pitch) + z * std::cos(pitch);
            y = lifted;
            const float turned = x * std::cos(roll) - y * std::sin(roll);
            y = x * std::sin(roll) + y * std::cos(roll);
            x = turned;
        } else {
            yaw += std::sin(-progress * 9.424778f) * 0.125f * (1 - progress);
        }
    }
    return { 8 + x * std::cos(yaw) - z * std::sin(yaw), y, 8 + x * std::sin(yaw) + z * std::cos(yaw) };
}

}
