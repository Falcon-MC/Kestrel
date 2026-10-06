#include "client/Inventory.h"

#include <cstdio>
#include <stdexcept>

namespace {
ItemStack stack(const char* name, Tag components = {})
{
    ItemStack item;
    item.mDefinition = std::make_shared<ItemDefinition>(name, 1, false, std::move(components));
    item.mCount = 1;
    return item;
}

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
}

int main()
{
    try {
        using kestrel::InventoryModel;
        for (const char* name : { "minecraft:egg", "minecraft:blue_egg", "minecraft:brown_egg" }) {
            require(InventoryModel::maxStack(stack(name)) == 16, "chicken eggs must request at most 16 items");
        }
        for (const char* name : { "minecraft:turtle_egg", "minecraft:sniffer_egg", "minecraft:chicken_spawn_egg", "minecraft:end_portal_frame", "minecraft:flowering_azalea" }) {
            require(InventoryModel::maxStack(stack(name)) == 64, "other creative items retain their stack limit");
        }
        require(InventoryModel::maxStack(stack("minecraft:diamond_sword")) == 1, "durable items are unstackable");
        Tag properties = Tag::ofCompound();
        properties.putInt("max_stack_size", 7);
        Tag components = Tag::ofCompound();
        components.put("item_properties", std::move(properties));
        require(InventoryModel::maxStack(stack("minecraft:blue_egg", components)) == 7, "server component overrides the fallback");
        components.putInt("minecraft:max_stack_size", 0);
        require(InventoryModel::maxStack(stack("minecraft:brown_egg", components)) == 1, "invalid component limits are clamped");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
