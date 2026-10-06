#include "client/InventoryTransaction.h"
#include "Protocol/PacketCodecContext.h"

#include <cstdio>
#include <stdexcept>

namespace kestrel {
HudItem hudItemOf(const ItemStack&)
{
    throw std::runtime_error("transaction application must not build recipe previews");
}
}

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
}

int main()
{
    try {
        // Captured native BDS pickup: inventory slot 11 grows from one item to two.
        const std::string hex = "00000002000100000b650001000000b7d6d9f4010a000000000000000000006500020000018a03b7d6d9f4010a0000000000000000000003000001650001000000b7d6d9f4010a000000000000000000000000000000000000";
        std::string bytes;
        for (size_t i = 0; i < hex.size(); i += 2) bytes.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
        BlockDefinitionRegistry blocks;
        ItemDefinitionRegistry items;
        items.registerDefinition(std::make_shared<ItemDefinition>("minecraft:iron_bars", 101, false, Tag {}));
        PacketCodecContext context(blocks, items);
        ReadOnlyBinaryStream wire(bytes);
        InventoryTransactionPacket packet;
        packet.read(wire, context);
        require(packet.mActions.size() == 2, "native pickup contains inventory and world actions");
        kestrel::InventoryModel model;
        auto rollback = model.slots;
        require(kestrel::applyServerInventoryTransaction(model, packet, &rollback), "native pickup must update inventory");
        require(model.slots[11].mCount == 2 && model.slots[11].mDefinition->getIdentifier() == "minecraft:iron_bars", "native new stack is authoritative");
        require(rollback[11].mCount == 2, "later rejected prediction must retain the server pickup");
        require(kestrel::InventoryModel::empty(model.slots[1]), "world action must not overwrite inventory slots");
        auto invalid = packet;
        invalid.mActions.resize(1);
        invalid.mActions[0].mSlot = 999;
        require(!kestrel::applyServerInventoryTransaction(model, invalid), "invalid slots must be ignored");
        invalid.mActions[0].mSlot = 0;
        invalid.mActions[0].mSource.mContainerId = 999;
        require(!kestrel::applyServerInventoryTransaction(model, invalid), "unknown containers must be ignored");
        packet.mTransactionType = InventoryTransactionType::ItemUse;
        require(!kestrel::applyServerInventoryTransaction(model, packet), "item use is not an inventory replacement");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
