#pragma once

#include "Core/NBT/Tag.h"

#include <string>
#include <vector>

namespace kestrel::world {

inline std::vector<std::string> readBlockTags(const Tag& definition)
{
    std::vector<std::string> result;
    const Tag* tags = definition.isCompound() ? definition.get("blockTags") : nullptr;
    if (!tags || !tags->isList() || tags->getListType() != Tag::Type::String) return result;
    for (const Tag& tag : tags->getList()) {
        if (result.size() >= 256) break;
        if (tag.asString().size() <= 256) result.push_back(tag.asString());
    }
    return result;
}

}
