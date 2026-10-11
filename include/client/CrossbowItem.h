#pragma once

#include "Core/NBT/Tag.h"

namespace kestrel {
inline int chargedCrossbowFrame(const Tag& data)
{
    const Tag* charged = data.isCompound() ? data.get("chargedItem") : nullptr;
    if (!charged || !charged->isCompound()) {
        return 0;
    }
    const Tag* count = charged->get("Count");
    const Tag* name = charged->get("Name");
    if (!count || count->getType() != Tag::Type::Byte || count->asByte() <= 0
        || !name || name->getType() != Tag::Type::String) {
        return 0;
    }
    if (name->asString() == "minecraft:arrow") {
        return 4;
    }
    return name->asString() == "minecraft:fireworks" || name->asString() == "minecraft:firework_rocket" ? 5 : 0;
}
}
