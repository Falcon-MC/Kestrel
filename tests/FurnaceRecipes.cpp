#include "client/Inventory.h"
#include "client/ContainerLayout.h"

#include <cstdio>
#include <stdexcept>

namespace kestrel {
HudItem hudItemOf(const ItemStack& item)
{
    HudItem result;
    if (!InventoryModel::empty(item)) {
        result.identifier = item.mDefinition->getIdentifier();
        result.aux = item.mDamage;
        result.count = item.mCount;
    }
    return result;
}
}

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

ItemStack stack(const char* name, int count, int netId = 1)
{
    ItemStack item;
    item.mDefinition = std::make_shared<ItemDefinition>(name, 1, false, Tag {});
    item.mCount = count;
    item.mNetId = netId;
    return item;
}

kestrel::InventoryRecipe recipe(const char* station, const char* input, const char* output, int id)
{
    kestrel::InventoryRecipe result;
    result.recipe.mBlockName = station;
    result.recipe.mRecipeNetId = id;
    RecipeIngredientEntry ingredient;
    ingredient.mHasItem = true;
    ingredient.mType = RecipeIngredientType::Name;
    ingredient.mItemId = input;
    ingredient.mCount = 1;
    result.recipe.mInputs.push_back(ingredient);
    result.output = stack(output, 1);
    return result;
}
}

int main()
{
    try {
        using namespace kestrel;
        using namespace inventory;
        const ContainerType stations[] = { ContainerType::Furnace, ContainerType::BlastFurnace, ContainerType::Smoker };
        const char* names[] = { "furnace", "minecraft:blast_furnace", "smoker" };
        const ContainerSlotType roles[] = { ContainerSlotType::FurnaceIngredient, ContainerSlotType::BlastFurnaceIngredient, ContainerSlotType::SmokerIngredient };
        for (int index = 0; index < 3; ++index) {
            InventoryModel model;
            model.type = stations[index];
            model.containerSize = 3;
            InventoryState view;
            view.type = stations[index];
            const char* titles[] = { "container.furnace", "tile.blast_furnace.name", "tile.smoker.name" };
            require(containerLayout(view).title == titles[index], "station titles must use available localization keys");
            model.recipes = { recipe(names[index], "minecraft:oak_log", "minecraft:charcoal", 10),
                recipe("crafting_table", "minecraft:oak_log", "minecraft:planks", 11) };
            require(!model.canCraft(model.recipes[0]), "absent ingredients must be unavailable even in creative");
            model.creativeMode = true;
            model.slots[0] = stack("minecraft:oak_log", 4, 35);
            require(model.canCraft(model.recipes[0]), "each furnace must accept its own recipes");
            require(!model.canCraft(model.recipes[1]), "crafting recipes must not appear in a furnace");
            auto request = model.plan({ InventoryAction::SelectRecipe, -1, 10 }, -1);
            require(request.mActions.size() == 1, "selection must transfer an ingredient without crafting an output");
            require(request.mActions[0].mDestination.mContainer == roles[index], "combustible ingredients must enter the input, never the fuel slot");
            require(model.slots[Container].mCount == 4 && InventoryModel::empty(model.slots[0]), "selection must transfer the available ingredient stack");
            require(InventoryModel::empty(model.slots[Container + 1]), "recipe selection must not supply fuel");
            require(model.plan({ InventoryAction::SelectRecipe, -1, 10 }, -3).mActions.empty(), "a supplied input must not be duplicated");
            require(model.plan({ InventoryAction::SelectRecipe, -1, 11 }, -5).mActions.empty(), "a foreign station recipe must not modify the crafting grid");
        }

        InventoryModel model;
        model.type = ContainerType::Furnace;
        model.containerSize = 3;
        model.recipes = { recipe("furnace", "minecraft:iron_ore", "minecraft:iron_ingot", 20),
            recipe("furnace", "minecraft:raw_iron", "minecraft:iron_ingot", 21),
            recipe("smoker", "minecraft:beef", "minecraft:cooked_beef", 22),
            recipe("furnace", "minecraft:beef", "minecraft:cooked_beef", 23) };
        model.slots[5] = stack("minecraft:raw_iron", 8, 36);
        model.itemTags["minecraft:is_food"] = { "minecraft:cooked_beef" };
        auto catalog = model.furnaceCatalog();
        require(catalog.size() == 2 && catalog[0].networkId == 21, "duplicate outputs must prefer an ingredient the player owns");
        require(catalog[0].category == 1 && catalog[1].category == 0, "food and item categories must remain separate");
        auto blockRecipe = recipe("furnace", "minecraft:cobblestone", "minecraft:stone", 24);
        RecipeOutputEntry blockOutput;
        blockOutput.mBlockRuntimeId = 42;
        blockRecipe.recipe.mOutputs.push_back(blockOutput);
        model.recipes.push_back(std::move(blockRecipe));
        require(model.furnaceCatalog().back().category == 2, "raw block runtime IDs must classify blocks even without a decoded block definition");
        model.itemTags["minecraft:is_food"].push_back("minecraft:stone");
        require(model.furnaceCatalog().back().category == 0, "food classification must precede block placement capability");
        model.itemTags["minecraft:is_food"].pop_back();
        Tag food = Tag::ofCompound();
        food.put("minecraft:food", Tag::ofCompound());
        auto custom = recipe("furnace", "test:raw", "test:cooked", 25);
        custom.output.mDefinition = std::make_shared<ItemDefinition>("test:cooked", 2, false, std::move(food));
        model.recipes.push_back(std::move(custom));
        require(model.furnaceCatalog().back().category == 0, "server-defined food components must select the food category");
        model.slots[Container] = stack("minecraft:cobblestone", 12, 37);
        auto request = model.plan({ InventoryAction::SelectRecipe, -1, 21 }, -7);
        require(request.mActions.size() == 2, "switching recipes must return the previous input first");
        require(model.slots[Container].mDefinition->getIdentifier() == "minecraft:raw_iron", "the selected ingredient must replace the previous input");
        require(model.slots[0].mCount == 12, "replacing the input must preserve all previous items");

        for (int slot = 0; slot < 36; ++slot) model.slots[slot] = stack("minecraft:dirt", 64);
        model.slots[5] = stack("minecraft:raw_iron", 8, 36);
        model.slots[Container] = stack("minecraft:cobblestone", 12, 37);
        require(model.plan({ InventoryAction::SelectRecipe, -1, 21 }, -9).mActions.empty(), "a full inventory must leave the old input untouched");
        require(model.slots[Container].mCount == 12 && model.slots[5].mCount == 8, "failed replacement must not mutate any stack");

        model.slots = {};
        model.itemTags["test:logs"] = { "minecraft:oak_log", "minecraft:birch_log" };
        model.recipes[0].recipe.mInputs[0].mType = RecipeIngredientType::ItemTag;
        model.recipes[0].recipe.mInputs[0].mItemTag = "test:logs";
        model.recipes[0].recipe.mInputs[0].mCount = 2;
        model.slots[0] = stack("minecraft:oak_log", 1);
        model.slots[1] = stack("minecraft:birch_log", 1);
        require(!model.canCraft(model.recipes[0]), "different tagged items cannot occupy one input stack");
        model.slots[2] = stack("minecraft:birch_log", 1);
        require(model.canCraft(model.recipes[0]), "matching split stacks must satisfy a tagged ingredient");
        request = model.plan({ InventoryAction::SelectRecipe, -1, 20 }, -11);
        require(request.mActions.size() == 2 && model.slots[Container].mCount == 2, "split stacks must merge into the input");
        require(model.slots[0].mCount == 1, "unused tag alternatives must remain in inventory");

        std::array<HudItem, 9> ghosts;
        HudItem output;
        require(model.recipeGhost(20, ghosts, output) && !ghosts[0].empty(), "missing ingredients must still have a recipe preview");
        require(!model.recipeGhost(22, ghosts, output), "previews must reject recipes for other stations");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
