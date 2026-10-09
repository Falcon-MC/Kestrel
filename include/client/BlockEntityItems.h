#pragma once

#include "client/Inventory.h"

#include <algorithm>

namespace kestrel {

inline HudItem blockEntityItem(const Tag& entry)
{
    HudItem item;
    if (!entry.isCompound()) return item;
    const Tag* count = entry.get("Count");
    const std::string name = entry.getString("Name", "");
    if (!count || count->getType() != Tag::Type::Byte || count->asByte() <= 0 || name.empty() || name.size() > 256) return item;
    item.identifier = name;
    item.count = count->asByte();
    if (const Tag* damage = entry.get("Damage"); damage && damage->getType() == Tag::Type::Short) item.aux = damage->asShort();
    if (const Tag* tag = entry.get("tag"); tag && tag->isCompound()) {
        if (const Tag* map = tag->get("map_uuid"); map && map->getType() == Tag::Type::Long) item.mapId = map->asLong();
        if (const Tag* ench = tag->get("ench"); ench && ench->isList()) item.enchanted = !ench->getList().empty();
    }
    return item;
}

inline std::array<HudItem, 3> shelfItems(const Tag& data)
{
    std::array<HudItem, 3> items;
    const Tag* list = data.get("Items");
    if (!list || !list->isList()) {
        return items;
    }
    const auto& entries = list->getList();
    for (size_t slot = 0; slot < std::min(entries.size(), items.size()); ++slot) {
        items[slot] = blockEntityItem(entries[slot]);
    }
    return items;
}

}
