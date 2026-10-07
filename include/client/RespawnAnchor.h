#pragma once

#include "Protocol/Types/ItemStack.h"

namespace kestrel {

inline bool usesRespawnAnchor(const Tag* states, const ItemStack& item, bool sneaking)
{
    if (sneaking && !item.isAir()) return false;
    if (!item.isAir()) {
        const std::string& block = item.mBlockDefinition ? item.mBlockDefinition->getIdentifier() : item.mDefinition->getIdentifier();
        if (block == "minecraft:glowstone" || block == "glowstone") return true;
    }
    const Tag* charge = states && states->isCompound() ? states->get("respawn_anchor_charge") : nullptr;
    return charge && charge->getType() == Tag::Type::Int && charge->asInt() > 0 && charge->asInt() <= 4;
}

}
