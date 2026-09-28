#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kestrel::world {

enum class ToolKind : uint8_t {
    None,
    Sword,
    Shovel,
    Pickaxe,
    Axe,
    Shears,
    Hoe,
    Spear,
};

/**
 * How hard a vanilla block is to break: its hardness (negative for
 * unbreakable blocks), the tool that digs it, the harvest level that tool
 * needs (0 none, 1 wood or gold, 2 stone or copper, 3 iron, 4 diamond,
 * 5 netherite) and whether it drops when broken without that tool.
 */
struct BlockHardness {
    float hardness = 1.0f;
    ToolKind tool = ToolKind::None;
    uint8_t level = 0;
    bool handHarvest = true;
};

/**
 * Everything about the player that changes how fast a block breaks. Effect
 * levels are amplifier + 1, 0 when the effect is missing.
 */
struct MiningConditions {
    std::string heldItem;
    int32_t efficiency = 0;
    int32_t haste = 0;
    int32_t conduitPower = 0;
    int32_t miningFatigue = 0;
    bool onGround = true;
    bool flying = false;
    bool underwater = false;
    bool aquaAffinity = false;
};

class BlockHardnessTable {
public:
    static const BlockHardnessTable& shared();

    /**
     * The hardness of a block by identifier, with or without the minecraft
     * namespace; blocks the table does not know break like a plain block.
     */
    const BlockHardness& find(std::string_view name) const;

private:
    BlockHardnessTable();

    std::unordered_map<std::string, BlockHardness> byName;
    BlockHardness fallback;
};

/**
 * The share of a block one tick of mining breaks: 0 for blocks that never
 * break, 1 or more for blocks that break the moment they are hit.
 */
float destroyProgressPerTick(std::string_view blockName, const MiningConditions& conditions);

/**
 * Whether holding this item keeps a creative player from breaking blocks.
 */
bool preventsCreativeBreaking(std::string_view heldItem);

}
