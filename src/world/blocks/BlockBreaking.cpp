#include "world/BlockBreaking.h"

#include "BlockBreakingTable.h"

#include <algorithm>
#include <charconv>

namespace kestrel::world {

namespace {

constexpr std::string_view Namespace = "minecraft:";

struct ToolTier {
    std::string_view prefix;
    uint8_t level;
    float speed;
};

constexpr ToolTier Tiers[] = {
    { "wooden_", 1, 2.0f },
    { "golden_", 1, 12.0f },
    { "stone_", 2, 4.0f },
    { "copper_", 2, 5.0f },
    { "iron_", 3, 6.0f },
    { "diamond_", 4, 8.0f },
    { "netherite_", 5, 9.0f },
};

struct HeldTool {
    ToolKind kind = ToolKind::None;
    uint8_t level = 0;
    float speed = 1.0f;
};

std::string_view withoutNamespace(std::string_view name)
{
    return name.starts_with(Namespace) ? name.substr(Namespace.size()) : name;
}

HeldTool heldTool(std::string_view identifier)
{
    std::string_view name = withoutNamespace(identifier);
    if (name == "shears") {
        return { ToolKind::Shears, 0, 1.0f };
    }
    static constexpr std::pair<std::string_view, ToolKind> Kinds[] = {
        { "sword", ToolKind::Sword },
        { "shovel", ToolKind::Shovel },
        { "pickaxe", ToolKind::Pickaxe },
        { "axe", ToolKind::Axe },
        { "hoe", ToolKind::Hoe },
        { "spear", ToolKind::Spear },
    };
    for (const ToolTier& tier : Tiers) {
        if (!name.starts_with(tier.prefix)) {
            continue;
        }
        std::string_view kind = name.substr(tier.prefix.size());
        for (const auto& [suffix, tool] : Kinds) {
            if (kind == suffix) {
                return { tool, tier.level, tier.speed };
            }
        }
    }
    return {};
}

ToolKind toolNamed(std::string_view word)
{
    static constexpr std::pair<std::string_view, ToolKind> Names[] = {
        { "sword", ToolKind::Sword },
        { "shovel", ToolKind::Shovel },
        { "pickaxe", ToolKind::Pickaxe },
        { "axe", ToolKind::Axe },
        { "shears", ToolKind::Shears },
        { "hoe", ToolKind::Hoe },
        { "spear", ToolKind::Spear },
    };
    for (const auto& [name, tool] : Names) {
        if (word == name) {
            return tool;
        }
    }
    return ToolKind::None;
}

/**
 * The speed a sword or shears bring to a block, which unlike other tools
 * depends on the block itself rather than on a tier.
 */
float specialToolSpeed(ToolKind tool, std::string_view block)
{
    if (tool == ToolKind::Sword) {
        return block == "web" ? 15.0f : 1.5f;
    }
    if (tool == ToolKind::Shears) {
        if (block == "web" || block.find("leaves") != std::string_view::npos) {
            return 15.0f;
        }
        if (block.ends_with("_wool")) {
            return 5.0f;
        }
        if (block == "vine" || block == "glow_lichen") {
            return 2.0f;
        }
    }
    return 1.0f;
}

float fatigueFactor(int32_t level)
{
    switch (level) {
    case 0:
        return 1.0f;
    case 1:
        return 0.3f;
    case 2:
        return 0.09f;
    case 3:
        return 0.0027f;
    default:
        return 8.1e-4f;
    }
}

}

const BlockHardnessTable& BlockHardnessTable::shared()
{
    static const BlockHardnessTable instance;
    return instance;
}

BlockHardnessTable::BlockHardnessTable()
{
    std::string_view text(reinterpret_cast<const char*>(KestrelBlockBreakingData::kBlockBreaking), KestrelBlockBreakingData::kBlockBreakingSize);
    while (!text.empty()) {
        size_t end = std::min(text.find('\n'), text.size());
        std::string_view line = text.substr(0, end);
        text.remove_prefix(std::min(end + 1, text.size()));
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        std::string_view fields[5];
        size_t count = 0;
        while (!line.empty() && count < 5) {
            size_t space = std::min(line.find(' '), line.size());
            fields[count++] = line.substr(0, space);
            line.remove_prefix(std::min(space + 1, line.size()));
        }
        if (count < 5) {
            continue;
        }
        BlockHardness entry;
        std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), entry.hardness);
        entry.tool = toolNamed(fields[2]);
        int level = 0;
        std::from_chars(fields[3].data(), fields[3].data() + fields[3].size(), level);
        entry.level = static_cast<uint8_t>(level);
        entry.handHarvest = fields[4] == "1";
        byName.emplace(std::string(fields[0]), entry);
    }
}

const BlockHardness& BlockHardnessTable::find(std::string_view name) const
{
    auto found = byName.find(std::string(withoutNamespace(name)));
    return found == byName.end() ? fallback : found->second;
}

/**
 * The game's dig speed: the tool's speed when it is the right kind for the
 * block, raised by efficiency and haste, cut by mining fatigue, water and
 * leaving the ground, then spread over the block's hardness, three times
 * slower when the block would not drop anything.
 */
float destroyProgressPerTick(std::string_view blockName, const MiningConditions& conditions)
{
    std::string_view name = withoutNamespace(blockName);
    const BlockHardness& block = BlockHardnessTable::shared().find(name);
    if (block.hardness < 0.0f) {
        return 0.0f;
    }
    HeldTool tool = heldTool(conditions.heldItem);
    if (block.hardness == 0.0f || (tool.kind == ToolKind::Sword && name == "bamboo")) {
        return 1.0f;
    }
    bool rightKind = tool.kind != ToolKind::None && tool.kind == block.tool;
    float speed = 1.0f;
    if (tool.kind == ToolKind::Sword || tool.kind == ToolKind::Shears) {
        speed = specialToolSpeed(tool.kind, name);
    } else if (rightKind) {
        speed = tool.speed;
    }
    bool canHarvest = block.tool == ToolKind::None || block.handHarvest || (rightKind && tool.level >= block.level);
    if (speed > 1.0f && conditions.efficiency > 0) {
        speed += float(conditions.efficiency * conditions.efficiency + 1);
    }
    if (int32_t haste = std::max(conditions.haste, conditions.conduitPower); haste > 0) {
        speed *= 1.0f + 0.2f * float(haste);
    }
    speed *= fatigueFactor(conditions.miningFatigue);
    if (conditions.underwater && !conditions.aquaAffinity) {
        speed /= 5.0f;
    }
    if (!conditions.onGround && !conditions.flying) {
        speed /= 5.0f;
    }
    return speed / block.hardness / (canHarvest ? 30.0f : 100.0f);
}

bool preventsCreativeBreaking(std::string_view heldItem)
{
    std::string_view name = withoutNamespace(heldItem);
    return name.ends_with("_sword") || name == "trident" || name == "mace" || name == "debug_stick";
}

}
