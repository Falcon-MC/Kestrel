#pragma once

#include <string_view>

namespace kestrel {

constexpr bool actorEquipmentIsOffhand(std::string_view identifier, unsigned container, unsigned inventorySlot)
{
    return container == 119 || (container == 0 && inventorySlot == 1 && identifier != "minecraft:player");
}

}
