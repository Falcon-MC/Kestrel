#pragma once

#include "client/Inventory.h"
#include "Protocol/Packets/InventoryTransactionPacket.h"

#include <limits>

namespace kestrel {

inline std::vector<ItemStackRequest> planInventoryRequests(InventoryModel& model, const InventoryCommand& command, int32_t& nextId)
{
    bool bulk = (command.action == InventoryAction::QuickMove && command.slot == inventory::Output)
        || (command.action == InventoryAction::Craft && command.all);
    bulk = bulk && (model.type == ContainerType::Inventory || model.type == ContainerType::Workbench);
    if (nextId < std::numeric_limits<int32_t>::min() + 128) nextId = -1;
    std::vector<ItemStackRequest> requests;
    // A gesture can need several output stacks, but BDS permits only one craft action per request.
    for (int i = 0; i < (bulk ? 64 : 1); ++i) {
        auto request = model.plan(command, nextId);
        nextId -= 2;
        if (request.mActions.empty()) break;
        requests.push_back(std::move(request));
    }
    return requests;
}

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
