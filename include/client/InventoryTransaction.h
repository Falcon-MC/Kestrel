#pragma once

#include "client/Inventory.h"
#include "Protocol/Packets/InventoryTransactionPacket.h"

namespace kestrel {

inline bool applyServerInventoryTransaction(InventoryModel& model, const InventoryTransactionPacket& packet,
    std::array<ItemStack, inventory::SlotCount>* rollback = nullptr)
{
    if (packet.mTransactionType != InventoryTransactionType::Normal) return false;
    bool changed = false;
    for (const auto& action : packet.mActions) {
        if (action.mSource.mType != InventorySourceType::Container) continue;
        int slot = model.packetSlot(action.mSource.mContainerId, action.mSlot);
        if (slot < 0 || slot >= inventory::SlotCount) continue;
        model.slots[slot] = action.mToItem;
        if (rollback) (*rollback)[slot] = action.mToItem;
        changed = true;
    }
    return changed;
}

}
