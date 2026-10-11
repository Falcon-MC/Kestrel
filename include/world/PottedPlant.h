#pragma once

#include "Core/NBT/Tag.h"
#include "Protocol/BlockStateHasher.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace kestrel::world {

inline bool pottablePlant(std::string_view name)
{
    if (!name.starts_with("minecraft:")) return false;
    name.remove_prefix(10);
    constexpr std::array Names {
        "acacia_sapling", "allium", "azalea", "azure_bluet", "bamboo", "bamboo_sapling", "birch_sapling",
        "blue_orchid", "brown_mushroom", "cactus", "cherry_sapling", "closed_eyeblossom", "cornflower",
        "crimson_fungus", "crimson_roots", "dandelion", "dark_oak_sapling", "deadbush", "fern",
        "flowering_azalea", "golden_dandelion", "jungle_sapling", "lily_of_the_valley", "mangrove_propagule", "oak_sapling",
        "open_eyeblossom", "orange_tulip", "oxeye_daisy", "pale_oak_sapling", "pink_tulip", "poppy",
        "poplar_sapling", "red_mushroom", "red_tulip", "spruce_sapling", "torchflower", "warped_fungus", "warped_roots",
        "white_tulip", "wither_rose",
    };
    return std::find(Names.begin(), Names.end(), name) != Names.end();
}

inline std::optional<uint32_t> pottedPlantHash(const Tag* data)
{
    const Tag* plant = data ? data->get("PlantBlock") : nullptr;
    if (!plant || !plant->isCompound()) return std::nullopt;
    const Tag* name = plant->get("name");
    const Tag* states = plant->get("states");
    if (!name || name->getType() != Tag::Type::String || !pottablePlant(name->asString())
        || !states || !states->isCompound() || states->getKeys().size() > 16) return std::nullopt;
    for (const auto& key : states->getKeys()) {
        const Tag* value = states->get(key);
        if (key.size() > 64 || !value) return std::nullopt;
        if (value->getType() == Tag::Type::String) {
            if (value->asString().size() > 64) return std::nullopt;
        } else if (value->getType() != Tag::Type::Byte && value->getType() != Tag::Type::Int) {
            return std::nullopt;
        }
    }
    return uint32_t(BlockStateHasher::hash(name->asString(), *states));
}

}
