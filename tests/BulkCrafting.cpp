#include "client/InventoryTransaction.h"

#include <cstdio>
#include <stdexcept>

namespace kestrel {
HudItem hudItemOf(const ItemStack&)
{
    throw std::runtime_error("craft planning must not build previews");
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

kestrel::InventoryModel workbench(int logs)
{
    kestrel::InventoryModel model;
    model.type = ContainerType::Workbench;
    model.windowId = 2;
    model.slots[kestrel::inventory::Craft] = stack("minecraft:birch_log", logs, 100);
    kestrel::InventoryRecipe recipe;
    recipe.recipe.mBlockName = "crafting_table";
    recipe.recipe.mRecipeNetId = 42;
    RecipeIngredientEntry input;
    input.mHasItem = true;
    input.mItemId = "minecraft:birch_log";
    recipe.recipe.mInputs.push_back(input);
    recipe.output = stack("minecraft:birch_planks", 4, 0);
    model.recipes.push_back(std::move(recipe));
    return model;
}

int total(const kestrel::InventoryModel& model)
{
    int count = 0;
    for (int i = 0; i < 36; ++i) {
        const auto& item = model.slots[i];
        if (!item.isAir() && item.mDefinition->getIdentifier() == "minecraft:birch_planks") count += item.mCount;
    }
    return count;
}
}

int main()
{
    try {
        using namespace kestrel;
        using namespace inventory;
        for (int logs : { 16, 63, 64 }) {
            auto model = workbench(logs);
            int32_t id = -1;
            auto requests = planInventoryRequests(model, { InventoryAction::QuickMove, Output }, id);
            require(requests.size() == static_cast<size_t>((logs + 15) / 16), "a gesture must plan every output stack immediately");
            int consumed = 0;
            for (size_t i = 0; i < requests.size(); ++i) {
                const auto& request = requests[i];
                require(request.mRequestId == -1 - static_cast<int>(2 * i), "each batch needs its own prediction id");
                int crafts = 0;
                for (const auto& action : request.mActions) {
                    if (action.mType == ItemStackRequestActionType::CraftRecipe) {
                        ++crafts;
                        require(action.mNumberOfRequestedCrafts > 0 && action.mNumberOfRequestedCrafts <= 16, "created output must fit a stack");
                    }
                    if (action.mType == ItemStackRequestActionType::Consume) {
                        consumed += action.mCount;
                        require(action.mSource.mStackNetworkId == (i == 0 ? 100 : requests[i - 1].mRequestId), "dependent batches must reference the previous prediction");
                    }
                }
                require(crafts == 1, "BDS permits one craft action per request");
            }
            require(consumed == logs && total(model) == logs * 4, "bulk craft must conserve ingredients and results");
            require(model.slots[Craft].isAir() && model.slots[Cursor].isAir() && model.slots[Output].isAir(), "bulk craft must finish in the inventory");
            require(planInventoryRequests(model, { InventoryAction::QuickMove, Output }, id).empty(), "exhausted grid must not craft again");
        }
        auto limited = workbench(64);
        for (int i = 0; i < 36; ++i) limited.slots[i] = stack("minecraft:stone", 64, 200 + i);
        limited.slots[9] = stack("minecraft:birch_planks", 59, 209);
        int32_t id = -1;
        auto requests = planInventoryRequests(limited, { InventoryAction::QuickMove, Output }, id);
        require(requests.size() == 1 && total(limited) == 63 && limited.slots[Craft].mCount == 63, "only whole crafts fitting the inventory may run");
        require(planInventoryRequests(limited, { InventoryAction::QuickMove, Output }, id).empty(), "insufficient room must preserve the ingredients");

        auto single = workbench(64);
        requests = planInventoryRequests(single, { InventoryAction::Primary, Output }, id);
        require(requests.size() == 1 && single.slots[Cursor].mCount == 4 && single.slots[Craft].mCount == 63, "ordinary click must still craft once into the cursor");
        auto all = workbench(64);
        requests = planInventoryRequests(all, { InventoryAction::Craft, Output, 0, true }, id);
        require(requests.size() == 4 && total(all) == 256, "craft-all must use the same complete batch");

        auto tools = workbench(40);
        tools.recipes.front().output = stack("minecraft:iron_pickaxe", 1, 0);
        requests = planInventoryRequests(tools, { InventoryAction::QuickMove, Output }, id);
        require(requests.size() == 36 && tools.slots[Craft].mCount == 4, "unstackable results must stop at inventory capacity");
        for (int i = 0; i < 36; ++i) require(tools.slots[i].mCount == 1, "unstackable results must occupy separate slots");

        auto wrapped = workbench(64);
        id = std::numeric_limits<int32_t>::min() + 3;
        requests = planInventoryRequests(wrapped, { InventoryAction::QuickMove, Output }, id);
        require(requests.front().mRequestId == -1 && requests.back().mRequestId == -7, "request ids must not wrap inside a dependent batch");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
