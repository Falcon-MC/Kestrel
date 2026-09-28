#include "Protocol/InventoryCodec.h"
#include "Protocol/ItemCodec.h"
#include "Protocol/PacketCodecContext.h"
#include "Protocol/Packets/InventoryContentPacket.h"

#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    try {
        BlockDefinitionRegistry blocks;
        ItemDefinitionRegistry items;
        items.registerDefinition(std::make_shared<ItemDefinition>("minecraft:stone", 1, false, Tag {}));
        PacketCodecContext context(blocks, items);

        // A proxy destination can send a plain stack with a zero-length extra
        // data string. The following slots must still be read at their boundaries.
        BinaryStream wire;
        wire.putUnsignedVarInt(0);
        wire.putUnsignedVarInt(36);
        wire.putLShort(1);
        wire.putLShort(7);
        wire.putUnsignedVarInt(0);
        wire.putBool(true);
        wire.putVarInt(123);
        wire.putUnsignedVarInt(0);
        wire.putString("");
        for (int i = 1; i < 36; ++i) ItemCodec::writeNetworkItemStackDescriptor(wire, context, ItemStack::air());
        InventoryCodec::writeFullContainerName(wire, FullContainerName {});
        ItemCodec::writeNetworkItemStackDescriptor(wire, context, ItemStack::air());
        ReadOnlyBinaryStream input(wire.getBuffer());
        InventoryContentPacket packet;
        packet.read(input, context);
        require(packet.mContents.size() == 36, "lost inventory slots");
        require(packet.mContents[0].mCount == 7 && packet.mContents[0].mNetId == 123, "lost stack data");
        require(packet.mContents[0].mDefinition->getIdentifier() == "minecraft:stone", "wrong item");
        for (int i = 1; i < 36; ++i) require(packet.mContents[i].isAir(), "air slot misaligned");
        require(input.getRemainingLength() == 0, "unread inventory data");

        // Ordinary encoded NBT/list payloads must keep working as well.
        BinaryStream roundTrip;
        packet.write(roundTrip, context);
        ReadOnlyBinaryStream normal(roundTrip.getBuffer());
        InventoryContentPacket decoded;
        decoded.read(normal, context);
        require(decoded.mContents[0].mCount == 7 && normal.getRemainingLength() == 0, "normal payload regression");

        // A declared but truncated extra-data payload is still an error.
        BinaryStream truncated;
        truncated.putLShort(1);
        truncated.putLShort(1);
        truncated.putUnsignedVarInt(0);
        truncated.putBool(false);
        truncated.putUnsignedVarInt(0);
        truncated.putUnsignedVarInt(10);
        ReadOnlyBinaryStream bad(truncated.getBuffer());
        bool rejected = false;
        try { ItemCodec::readNetworkItemStackDescriptor(bad, context); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, "accepted a truncated item");
        std::cout << "Inventory decoding regressions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
