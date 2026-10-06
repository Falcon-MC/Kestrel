#include "client/Inventory.h"

#include <cstdio>
#include <stdexcept>

namespace kestrel {
HudItem hudItemOf(const ItemStack&)
{
    throw std::runtime_error("creative drops must not build recipe previews");
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
}

int main()
{
    try {
        using namespace kestrel;
        using namespace inventory;
        InventoryModel model;
        model.creativeMode = true;
        model.creative.emplace(42, stack("minecraft:iron_bars", 1, 0));
        model.slots[Cursor] = stack("minecraft:stone", 3, 17);
        model.slots[0] = stack("minecraft:dirt", 12, 18);
        auto before = model.slots;
        for (bool all : { false, true }) {
            auto request = model.plan({ InventoryAction::CreativeDrop, -1, 42, all }, -5);
            require(request.mActions.size() == (all ? 2u : 3u), "single drops must discard unused generated items");
            require(request.mActions[0].mType == ItemStackRequestActionType::CraftCreative
                && request.mActions[0].mCreativeItemNetworkId == 42, "drop must create the selected catalog entry");
            const auto& drop = request.mActions[1];
            require(drop.mType == ItemStackRequestActionType::Drop && drop.mCount == (all ? 64 : 1), "drop count must follow Ctrl");
            require(drop.mSource.mContainer == ContainerSlotType::CreatedOutput, "drop must use generated output rather than cursor");
            if (!all) require(request.mActions[2].mType == ItemStackRequestActionType::Destroy
                && request.mActions[2].mCount == 63, "remaining generated stack must be discarded");
            for (int slot : { Cursor, 0 }) {
                require(model.slots[slot].mDefinition == before[slot].mDefinition
                    && model.slots[slot].mCount == before[slot].mCount && model.slots[slot].mNetId == before[slot].mNetId
                    && model.slots[slot].mTag == before[slot].mTag, "drop must preserve existing cursor and inventory");
            }
            require(InventoryModel::empty(model.slots[Output]), "generated output must be empty afterward");
        }
        require(model.plan({ InventoryAction::CreativeDrop, -1, 999, true }, -9).mActions.empty(), "unknown catalog IDs must be rejected");
        model.creativeMode = false;
        require(model.plan({ InventoryAction::CreativeDrop, -1, 42, true }, -9).mActions.empty(), "survival cannot create catalog drops");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
