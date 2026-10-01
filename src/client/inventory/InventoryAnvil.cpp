#include "client/Inventory.h"
#include "world/ItemInfo.h"

#include <algorithm>
#include <map>

namespace kestrel {
namespace {

std::map<int, int> enchantments(const Tag& tag)
{
    std::map<int, int> result;
    const Tag* list = tag.isCompound() ? tag.get("ench") : nullptr;
    if (list && list->isList()) {
        for (const auto& entry : list->getList()) {
            if (entry.isCompound()) {
                int id = entry.getShort("id", -1);
                int level = entry.getShort("lvl", 0);
                if (id >= 0 && level > 0) {
                    result[id] = level;
                }
            }
        }
    }
    return result;
}

bool compatible(int first, int second)
{
    if (first == second) {
        return true;
    }
    auto protection = [](int id) {
        return id == 0 || id == 1 || id == 3 || id == 4;
    };
    auto damage = [](int id) {
        return id == 9 || id == 10 || id == 11 || id == 39 || id == 40;
    };
    if ((protection(first) && protection(second)) || (damage(first) && damage(second))) {
        return false;
    }
    const std::pair<int, int> conflicts[] = { { 8, 25 }, { 16, 18 }, { 22, 26 }, { 30, 31 }, { 30, 32 }, { 33, 34 } };
    for (auto [a, b] : conflicts) {
        if ((first == a && second == b) || (first == b && second == a)) {
            return false;
        }
    }
    return true;
}

bool applicable(int id, const ItemStack& item)
{
    const std::string& name = item.mDefinition->getIdentifier();
    if (name == "minecraft:enchanted_book") {
        return true;
    }
    int armor = InventoryModel::armorSlot(item) - inventory::Armor;
    if (id <= 7 || id == 8 || id == 25 || id == 27 || id == 36 || id == 37) {
        if (id == 27) {
            return armor >= 0 && armor < 4;
        }
        if (id == 6 || id == 7) {
            return armor == 0;
        }
        if (id == 2 || id == 8 || id == 25 || id == 36) {
            return armor == 3;
        }
        if (id == 37) {
            return armor == 2;
        }
        return armor >= 0 && armor < 4;
    }
    if (id == 17 || id == 26 || id == 28) {
        return world::itemMaxDurability(name) > 0;
    }
    if (id >= 9 && id <= 14) {
        return name.ends_with("_sword") || (id <= 11 && (name.ends_with("_axe") || name == "minecraft:mace"));
    }
    if (id == 15 || id == 16 || id == 18) {
        return name.ends_with("_pickaxe") || name.ends_with("_axe") || name.ends_with("_shovel") || name.ends_with("_hoe") || (id == 15 && name == "minecraft:shears");
    }
    if (id >= 19 && id <= 22) {
        return name == "minecraft:bow";
    }
    if (id == 23 || id == 24) {
        return name == "minecraft:fishing_rod";
    }
    if (id >= 29 && id <= 32) {
        return name == "minecraft:trident";
    }
    if (id >= 33 && id <= 35) {
        return name == "minecraft:crossbow";
    }
    return id >= 38 && id <= 40 && name == "minecraft:mace";
}

bool repairMaterial(const std::string& item, const std::string& material)
{
    const std::pair<const char*, const char*> materials[] = {
        { "iron_", "iron_ingot" }, { "chainmail_", "iron_ingot" }, { "golden_", "gold_ingot" },
        { "diamond_", "diamond" }, { "netherite_", "netherite_ingot" }, { "leather_", "leather" }, { "copper_", "copper_ingot" }
    };
    for (const auto& [prefix, ingredient] : materials) {
        if (item.starts_with(std::string("minecraft:") + prefix) && material == std::string("minecraft:") + ingredient) {
            return true;
        }
    }
    if (item.starts_with("minecraft:wooden_") || item == "minecraft:shield") {
        return material.ends_with("_planks") || material == "minecraft:planks";
    }
    if (item.starts_with("minecraft:stone_")) {
        return material == "minecraft:cobblestone" || material == "minecraft:blackstone" || material == "minecraft:cobbled_deepslate";
    }
    return (item == "minecraft:elytra" && material == "minecraft:phantom_membrane")
        || (item == "minecraft:turtle_helmet" && material == "minecraft:turtle_scute")
        || (item == "minecraft:mace" && material == "minecraft:breeze_rod")
        || (item == "minecraft:wolf_armor" && material == "minecraft:armadillo_scute");
}

}

ItemStack InventoryModel::anvilPreview(std::vector<std::pair<int, int>>* consumption, int* cost) const
{
    const auto& base = slots[inventory::Ui + 1];
    const auto& material = slots[inventory::Ui + 2];
    if (cost) {
        *cost = 0;
    }
    if (empty(base)) {
        return ItemStack::air();
    }
    ItemStack result = base;
    if (!result.mTag.isCompound()) {
        result.mTag = Tag::ofCompound();
    }
    const std::string& name = base.mDefinition->getIdentifier();
    int maximum = world::itemMaxDurability(name);
    int price = 0;
    int used = 0;
    int previous = std::clamp(result.mTag.getInt("RepairCost", 0), 0, 1000000);
    int otherPrevious = !empty(material) && material.mTag.isCompound() ? std::clamp(material.mTag.getInt("RepairCost", 0), 0, 1000000) : 0;
    int rename = 0;
    if (!empty(material)) {
        const std::string& otherName = material.mDefinition->getIdentifier();
        if (maximum > 0 && repairMaterial(name, otherName)) {
            int damage = result.mTag.getInt("Damage", base.mDamage);
            int step = std::max(1, maximum / 4);
            while (damage > 0 && used < material.mCount) {
                damage = std::max(0, damage - step);
                ++used;
                ++price;
            }
            if (!used) {
                return ItemStack::air();
            }
            result.mTag.putInt("Damage", damage);
        } else {
            bool book = otherName == "minecraft:enchanted_book";
            if (!book && (otherName != name || maximum <= 0)) {
                return ItemStack::air();
            }
            if (!book) {
                int damage = result.mTag.getInt("Damage", base.mDamage);
                int otherDamage = material.mTag.isCompound() ? material.mTag.getInt("Damage", material.mDamage) : material.mDamage;
                int repaired = std::max(0, damage + otherDamage - maximum - maximum * 12 / 100);
                if (repaired < damage) {
                    result.mTag.putInt("Damage", repaired);
                    price += 2;
                }
            }
            auto merged = enchantments(base.mTag);
            const int maxima[] = { 4,4,4,4,4,3,3,1,3,5,5,5,2,2,3,5,1,3,3,5,2,1,1,3,3,2,1,1,1,5,3,3,1,1,4,3,3,3,3,5,4 };
            const int weights[] = { 1,2,2,4,2,8,4,4,4,1,2,2,2,4,4,1,8,2,4,1,4,4,8,4,4,4,4,8,8,4,4,1,8,4,1,2,8,8,8,2,4 };
            bool accepted = false;
            bool rejected = false;
            for (auto [id, level] : enchantments(material.mTag)) {
                if (id < 0 || id >= int(std::size(maxima))) {
                    rejected = true;
                    continue;
                }
                bool allowed = creativeMode || applicable(id, result);
                for (auto [existing, existingLevel] : merged) {
                    if (!compatible(id, existing)) {
                        allowed = false;
                        ++price;
                    }
                }
                if (!allowed) {
                    rejected = true;
                    continue;
                }
                int old = merged.contains(id) ? merged.at(id) : 0;
                level = std::min(maxima[id], old == level ? level + 1 : std::max(old, level));
                merged[id] = level;
                price += (book ? std::max(1, weights[id] / 2) : weights[id]) * level;
                accepted = true;
            }
            if (rejected && !accepted) {
                return ItemStack::air();
            }
            Tag list = Tag::ofList(Tag::Type::Compound);
            for (auto [id, level] : merged) {
                Tag entry = Tag::ofCompound();
                entry.putShort("id", int16_t(id));
                entry.putShort("lvl", int16_t(level));
                list.addToList(std::move(entry));
            }
            if (!merged.empty()) {
                result.mTag.put("ench", std::move(list));
            }
            used = 1;
        }
    }
    if (stationNameEdited && hudItemOf(base).customName != stationName) {
        const Tag* previousDisplay = result.mTag.get("display");
        Tag display = previousDisplay && previousDisplay->isCompound() ? *previousDisplay : Tag::ofCompound();
        if (stationName.empty()) {
            display.remove("Name");
        } else {
            display.putString("Name", stationName);
        }
        result.mTag.put("display", std::move(display));
        rename = 1;
        ++price;
    }
    if (price <= 0) {
        return ItemStack::air();
    }
    int total = price + previous + otherPrevious;
    if (rename == price) {
        total = std::min(total, 39);
    } else {
        result.mTag.putInt("RepairCost", std::max(previous, otherPrevious) * 2 + 1);
    }
    if (cost) {
        *cost = total;
    }
    if (!creativeMode && total >= 40) {
        return ItemStack::air();
    }
    if (consumption) {
        *consumption = { { inventory::Ui + 1, base.mCount } };
        if (used) {
            consumption->emplace_back(inventory::Ui + 2, used);
        }
    }
    result.mUserData.clear();
    return result;
}

}
