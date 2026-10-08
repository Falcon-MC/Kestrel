#include "client/Inventory.h"

#include <cstdio>
#include <stdexcept>

namespace kestrel {
HudItem hudItemOf(const ItemStack&)
{
    throw std::runtime_error("drops must not build recipe previews");
}
}

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

ItemStack stack(const char* name, int count, int netId)
{
    ItemStack item;
    item.mDefinition = std::make_shared<ItemDefinition>(name, 1, false, Tag {});
    item.mCount = count;
    item.mNetId = netId;
    return item;
}
}

/**
 * Without an open screen a vanilla server only accepts the hotbar container
 * name for the hotbar cells, and refuses the combined hotbar and inventory
 * name with FailedToValidateSrcSlot.
 */
int main()
{
    try {
        using namespace kestrel;
        using namespace inventory;
        InventoryModel model;
        model.slots[6] = stack("minecraft:dirt", 20, 152);
        model.slots[12] = stack("minecraft:stone", 5, 153);

        auto hotbar = model.plan({ InventoryAction::Drop, 6, 0, false }, -1);
        require(hotbar.mActions.size() == 1, "a hotbar drop must be one action");
        const auto& drop = hotbar.mActions[0];
        require(drop.mType == ItemStackRequestActionType::Drop && drop.mCount == 1, "the drop key must drop one item");
        require(drop.mSource.mContainer == ContainerSlotType::Hotbar, "hotbar cells must be named as the hotbar");
        require(drop.mSource.mSlot == 6 && drop.mSource.mStackNetworkId == 152, "the drop must name the hotbar cell and its stack");

        auto inventory = model.plan({ InventoryAction::Drop, 12, 0, true }, -3);
        require(inventory.mActions.size() == 1, "an inventory drop must be one action");
        require(inventory.mActions[0].mSource.mContainer == ContainerSlotType::Inventory, "cells above the hotbar must be named as the inventory");
        require(inventory.mActions[0].mSource.mSlot == 12 && inventory.mActions[0].mCount == 5, "the drop must name the inventory cell and the whole stack");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
