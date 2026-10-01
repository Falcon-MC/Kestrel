#include "client/Inventory.h"
#include "world/ItemInfo.h"

#include <algorithm>

namespace kestrel {

ItemStack InventoryModel::stationRecipeResult(const InventoryRecipe& recipe) const
{
    if (type != ContainerType::SmithingTable) {
        return recipe.output;
    }
    const auto& base = slots[inventory::Ui + 51];
    if (empty(base)) {
        return ItemStack::air();
    }
    ItemStack result = recipe.trim ? base : recipe.output;
    result.mTag = base.mTag.isCompound() ? base.mTag : Tag::ofCompound();
    result.mUserData.clear();
    result.mCount = 1;
    if (recipe.trim) {
        const auto& material = slots[inventory::Ui + 52];
        const auto& pattern = slots[inventory::Ui + 53];
        if (empty(material) || empty(pattern)) {
            return ItemStack::air();
        }
        auto materialId = trimMaterials.find(material.mDefinition->getIdentifier());
        auto patternId = trimPatterns.find(pattern.mDefinition->getIdentifier());
        if (materialId == trimMaterials.end() || patternId == trimPatterns.end()) {
            return ItemStack::air();
        }
        Tag trim = Tag::ofCompound();
        trim.putString("Material", materialId->second);
        trim.putString("Pattern", patternId->second);
        if (const Tag* previous = result.mTag.get("Trim"); previous && *previous == trim) {
            return ItemStack::air();
        }
        result.mTag.put("Trim", std::move(trim));
    }
    return result;
}

std::vector<std::string> InventoryModel::availableLoomPatterns() const
{
    const auto& banner = slots[inventory::Ui + 9];
    const auto& dye = slots[inventory::Ui + 10];
    const auto& pattern = slots[inventory::Ui + 11];
    if (empty(banner) || empty(dye) || !accepts(inventory::Ui + 9, banner) || !accepts(inventory::Ui + 10, dye)) {
        return {};
    }
    if (banner.mTag.isCompound()) {
        const Tag* patterns = banner.mTag.get("Patterns");
        if (patterns && patterns->isList() && patterns->size() >= 6) {
            return {};
        }
    }
    if (!empty(pattern)) {
        const std::pair<const char*, const char*> special[] = {
            { "flower", "flo" }, { "creeper", "cre" }, { "skull", "sku" }, { "mojang", "moj" },
            { "globe", "glb" }, { "piglin", "pig" }, { "flow", "flw" }, { "guster", "gus" }
        };
        for (const auto& [name, id] : special) {
            if (pattern.mDefinition->getIdentifier() == std::string("minecraft:") + name + "_banner_pattern") {
                return { id };
            }
        }
        return {};
    }
    return { "bs", "ts", "ls", "rs", "cs", "ms", "drs", "dls", "ss", "cr", "sc", "ld", "rud", "lud", "rd",
        "vh", "vhr", "hh", "hhb", "bl", "br", "tl", "tr", "bt", "tt", "bts", "tts", "mc", "mr", "bo", "gra", "gru", "cbo", "bri" };
}

ItemStack InventoryModel::stationPreview(std::vector<std::pair<int, int>>* consumption, int* cost) const
{
    if (cost) {
        *cost = 0;
    }
    if (type == ContainerType::Anvil) {
        return anvilPreview(consumption, cost);
    }
    if (type == ContainerType::Loom) {
        auto choices = availableLoomPatterns();
        if (std::find(choices.begin(), choices.end(), loomPattern) == choices.end()) {
            return ItemStack::air();
        }
        ItemStack result = slots[inventory::Ui + 9];
        result.mCount = 1;
        if (!result.mTag.isCompound()) {
            result.mTag = Tag::ofCompound();
        }
        const auto& dye = slots[inventory::Ui + 10];
        int color = dye.mDamage;
        const char* colors[] = { "black", "red", "green", "brown", "blue", "purple", "cyan", "light_gray", "gray", "pink", "lime", "yellow", "light_blue", "magenta", "orange", "white" };
        for (int index = 0; index < 16; ++index) {
            if (dye.mDefinition->getIdentifier() == std::string("minecraft:") + colors[index] + "_dye") {
                color = index;
                break;
            }
        }
        const Tag* existing = result.mTag.get("Patterns");
        Tag patterns = existing && existing->isList() ? *existing : Tag::ofList(Tag::Type::Compound);
        Tag applied = Tag::ofCompound();
        applied.putString("Pattern", loomPattern);
        applied.putInt("Color", color);
        patterns.addToList(std::move(applied));
        result.mTag.put("Patterns", std::move(patterns));
        result.mUserData.clear();
        if (consumption) {
            *consumption = { { inventory::Ui + 9, 1 }, { inventory::Ui + 10, 1 } };
        }
        return result;
    }
    int first = inventory::Ui + 16;
    int second = first + 1;
    if (type != ContainerType::Grindstone) {
        return ItemStack::air();
    }
    if (empty(slots[first])) {
        if (empty(slots[second])) {
            return ItemStack::air();
        }
        std::swap(first, second);
    }
    ItemStack result = slots[first];
    if (!result.mTag.isCompound()) {
        result.mTag = Tag::ofCompound();
    }
    const auto& material = slots[second];
    const std::string& name = result.mDefinition->getIdentifier();
    int maximum = world::itemMaxDurability(name);
    bool changed = false;
    int used = 0;
    if (type == ContainerType::Grindstone) {
        if (!empty(material)) {
            if (material.mDefinition != result.mDefinition || result.mCount != 1 || material.mCount != 1 || maximum <= 0) {
                return ItemStack::air();
            }
            int damage = result.mTag.getInt("Damage", result.mDamage);
            int other = material.mTag.isCompound() ? material.mTag.getInt("Damage", material.mDamage) : material.mDamage;
            result.mTag.putInt("Damage", std::max(0, damage + other - maximum - maximum / 20));
            used = 1;
            changed = true;
        }
        if (const Tag* enchantments = result.mTag.get("ench"); enchantments && enchantments->isList()) {
            Tag curses = Tag::ofList(Tag::Type::Compound);
            for (const auto& enchantment : enchantments->getList()) {
                if (!enchantment.isCompound()) {
                    continue;
                }
                int id = enchantment.getShort("id", -1);
                if (id == 27 || id == 28) {
                    curses.addToList(enchantment);
                } else {
                    changed = true;
                }
            }
            result.mTag.put("ench", std::move(curses));
            if (name == "minecraft:enchanted_book" && result.mTag.get("ench")->getList().empty()) {
                if (!bookDefinition) {
                    return ItemStack::air();
                }
                result.mDefinition = bookDefinition;
            }
        }
        result.mTag.remove("RepairCost");
    }
    if (!changed) {
        return ItemStack::air();
    }
    result.mUserData.clear();
    if (consumption) {
        consumption->clear();
        consumption->emplace_back(first, slots[first].mCount);
        if (used) {
            consumption->emplace_back(second, used);
        }
    }
    return result;
}

void InventoryModel::takeStationOutput(ItemStackRequest& request, bool toInventory)
{
    std::vector<std::pair<int, int>> consumption;
    int cost = 0;
    ItemStack output = stationPreview(&consumption, &cost);
    if (empty(output) || (!toInventory && !empty(slots[inventory::Cursor])
        && (!same(slots[inventory::Cursor], output) || slots[inventory::Cursor].mCount + output.mCount > maxStack(output)))) {
        return;
    }
    if (toInventory && inventoryRoom(output) < output.mCount) {
        return;
    }
    ItemStackRequestAction action;
    if (type == ContainerType::Anvil) {
        if (repairRecipe < 0) {
            return;
        }
        action.mType = ItemStackRequestActionType::CraftRecipeOptional;
        action.mRecipeNetworkId = repairRecipe;
        action.mFilteredStringIndex = stationNameEdited ? 0 : -1;
        if (stationNameEdited) {
            request.mFilterStrings.push_back(stationName);
            request.mHasTextProcessingEventOrigin = true;
            request.mTextProcessingEventOrigin = 3;
        }
    } else if (type == ContainerType::Loom) {
        action.mType = ItemStackRequestActionType::CraftLoom;
        action.mPatternId = loomPattern;
        action.mTimesCrafted = 1;
    } else {
        action.mType = ItemStackRequestActionType::CraftRepairAndDisenchant;
        action.mRecipeNetworkId = 0;
        action.mRepairCost = cost;
        action.mNumberOfRequestedCrafts = 1;
    }
    request.mActions.push_back(action);
    ItemStackRequestAction results;
    results.mType = ItemStackRequestActionType::CraftResultsDeprecated;
    results.mResultItems = { output };
    results.mTimesCrafted = 1;
    request.mActions.push_back(results);
    for (auto [slot, count] : consumption) {
        remove(request, slot, count, ItemStackRequestActionType::Consume);
    }
    slots[inventory::Output] = output;
    slots[inventory::Output].mNetId = request.mRequestId;
    if (toInventory) {
        placeInInventory(request);
    } else {
        move(request, inventory::Output, inventory::Cursor, output.mCount);
    }
}

const InventoryRecipe* InventoryModel::matchingStationRecipe(std::vector<std::pair<int, int>>* consumption) const
{
    std::vector<int> inputs;
    std::string block;
    if (type == ContainerType::Stonecutter) {
        block = "stonecutter";
        inputs = { inventory::Ui + 3 };
    } else if (type == ContainerType::SmithingTable) {
        block = "smithing_table";
        inputs = { inventory::Ui + 53, inventory::Ui + 51, inventory::Ui + 52 };
    } else if (type == ContainerType::Cartography) {
        block = "cartography_table";
        inputs = { inventory::Ui + 12, inventory::Ui + 13 };
    } else {
        return nullptr;
    }
    for (const auto& recipe : recipes) {
        if (recipe.recipe.mBlockName != block && recipe.recipe.mBlockName != "minecraft:" + block) {
            continue;
        }
        if (type == ContainerType::Stonecutter && recipe.recipe.mRecipeNetId != stationRecipe) {
            continue;
        }
        if (recipe.recipe.mInputs.size() != inputs.size()) {
            continue;
        }
        bool valid = true;
        std::vector<std::pair<int, int>> used;
        for (size_t index = 0; index < inputs.size(); ++index) {
            const auto& ingredient = recipe.recipe.mInputs[index];
            if (!ingredientMatches(ingredient, slots[inputs[index]])) {
                valid = false;
                break;
            }
            if (ingredient.mHasItem) {
                used.emplace_back(inputs[index], ingredient.mCount);
            }
        }
        if (valid) {
            if (consumption) {
                *consumption = std::move(used);
            }
            return &recipe;
        }
    }
    return nullptr;
}

}
