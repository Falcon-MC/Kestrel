#pragma once

#include "Core/NBT/Tag.h"

#include <string_view>

namespace kestrel {

inline bool itemHasIntrinsicGlint(std::string_view identifier)
{
    return identifier == "minecraft:enchanted_book" || identifier == "minecraft:enchanted_golden_apple"
        || identifier == "minecraft:lodestone_compass";
}

inline bool itemComponentHasGlint(const Tag& components, int depth = 0)
{
    if (!components.isCompound() || depth > 6) return false;
    for (const char* name : { "minecraft:glint", "minecraft:foil", "foil" }) {
        const Tag* glint = components.get(name);
        if (glint && glint->isCompound()) glint = glint->get("value");
        if (glint && glint->getType() == Tag::Type::Byte) return glint->asByte() != 0;
    }
    for (const char* name : { "components", "item_properties" }) {
        if (const Tag* nested = components.get(name); nested && itemComponentHasGlint(*nested, depth + 1)) return true;
    }
    return false;
}

inline bool itemHasGlint(std::string_view identifier, const Tag& tag)
{
    if (itemHasIntrinsicGlint(identifier)) return true;
    const Tag* ench = tag.isCompound() ? tag.get("ench") : nullptr;
    return ench && ench->isList();
}

}
