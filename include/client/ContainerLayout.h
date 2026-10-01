#pragma once

#include "client/Inventory.h"

#include <string>
#include <vector>

namespace kestrel {

struct ContainerCollection {
    std::string name;
    std::vector<int> slots;
};

struct ContainerLayout {
    std::string screen;
    std::string title;
    std::vector<ContainerCollection> collections;
};

struct MountSlots {
    enum class Body { None, HorseArmor, Carpet, NautilusArmor };
    bool saddle = true;
    Body body = Body::HorseArmor;
};

MountSlots mountSlots(const std::string& identifier);

ContainerLayout containerLayout(const InventoryState& state);
int personalUiSlot(int slot, ContainerType type);
int personalUiIndex(int slot, ContainerType type);
ContainerSlotType personalSlotType(int index, ContainerType type);

}
