#include "client/Inventory.h"
#include "world/ItemInfo.h"

#include <algorithm>

namespace kestrel {
namespace {
using namespace inventory;

const Tag* component(const Tag& tag, const std::string& name, int depth = 0)
{
    if (depth > 6 || tag.getType() != Tag::Type::Compound) return nullptr;
    if (const Tag* value = tag.get(name)) return value;
    for (const char* child : { "components", "item_properties" }) {
        if (const Tag* value = tag.get(child)) {
            if (const Tag* found = component(*value, name, depth + 1)) return found;
        }
    }
    return nullptr;
}

}

bool InventoryModel::empty(const ItemStack& item) { return item.isAir() || item.mCount <= 0; }

int InventoryModel::armorSlot(const ItemStack& item)
{
    if (empty(item)) return -1;
    const std::string& name = item.mDefinition->getIdentifier();
    if (name.ends_with("_helmet") || name.ends_with("_skull") || name.ends_with("_head") || name == "minecraft:carved_pumpkin") return Armor;
    if (name.ends_with("_chestplate") || name == "minecraft:elytra") return Armor + 1;
    if (name.ends_with("_leggings")) return Armor + 2;
    if (name.ends_with("_boots")) return Armor + 3;
    return -1;
}

int InventoryModel::maxStack(const ItemStack& item)
{
    if (empty(item)) return 64;
    const Tag& components = item.mDefinition->getComponentData();
    const Tag* limit = component(components, "minecraft:max_stack_size");
    if (!limit) limit = component(components, "max_stack_size");
    if (limit && limit->getType() == Tag::Type::Compound) limit = limit->get("value");
    if (limit && limit->getType() == Tag::Type::Int) return std::clamp(limit->asInt(), 1, 255);
    if (limit && limit->getType() == Tag::Type::Byte) return std::clamp(int(limit->asByte()), 1, 127);
    const std::string& name = item.mDefinition->getIdentifier();
    if (world::itemMaxDurability(name) > 0 || armorSlot(item) >= 0 || name.ends_with("shulker_box")
        || name.ends_with("_bed") || name.ends_with("_boat") || name.ends_with("_raft") || name.ends_with("minecart")
        || (name.ends_with("_bucket") && name != "minecraft:bucket") || name.ends_with("_stew") || name.ends_with("_soup")
        || name == "minecraft:potion" || name == "minecraft:splash_potion" || name == "minecraft:lingering_potion"
        || name == "minecraft:enchanted_book" || name == "minecraft:writable_book" || name == "minecraft:totem_of_undying"
        || name == "minecraft:saddle" || name.ends_with("_horse_armor") || name.ends_with("bundle") || name.starts_with("minecraft:music_disc_")) return 1;
    if (name == "minecraft:ender_pearl" || name == "minecraft:egg" || name == "minecraft:blue_egg"
        || name == "minecraft:brown_egg" || name == "minecraft:snowball"
        || name == "minecraft:bucket" || name == "minecraft:armor_stand" || name.ends_with("_sign") || name == "minecraft:written_book") return 16;
    return 64;
}

}
