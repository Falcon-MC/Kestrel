#pragma once

#include "Protocol/Types/ContainerType.h"
#include "Protocol/Types/CraftingRecipeEntry.h"
#include "Protocol/Types/ItemStackRequest.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kestrel {

struct HudItem {
    std::string identifier;
    int32_t count = 0;
    int32_t aux = 0;
    int32_t damage = 0;
    std::string customName;
    std::string icon;
    std::vector<std::string> lore;
    bool enchanted = false;
    bool handEquipped = false;
    bool empty() const { return identifier.empty() || count <= 0; }
    bool operator==(const HudItem&) const = default;
};

HudItem hudItemOf(const ItemStack& stack);

// Local slot addresses; protocol container names and UI offsets are translated at the boundary.
namespace inventory {
inline constexpr int Armor = 36, Offhand = 40, Craft = 41, Cursor = 50, Output = 51, Container = 52;
inline constexpr int SlotCount = Container + 54;
}

enum class InventoryAction { Open, Close, Primary, Secondary, QuickMove, Drop, HotbarSwap, Collect, Distribute, Creative, Craft, SelectRecipe };

struct InventoryCommand {
    InventoryAction action = InventoryAction::Primary;
    int slot = -1;
    int value = 0;
    bool all = false;
    std::vector<int> slots;
};

struct InventoryCatalogItem {
    HudItem item;
    int networkId = 0;
    int category = 0;
    int group = -1;
    std::string groupName;
};

struct InventoryState {
    std::array<HudItem, inventory::SlotCount> slots {};
    std::shared_ptr<const std::vector<InventoryCatalogItem>> creative;
    std::shared_ptr<const std::vector<InventoryCatalogItem>> recipes;
    std::vector<int> craftable;
    std::array<HudItem, 9> recipeGhost {};
    ContainerType type = ContainerType::Inventory;
    int windowId = 0;
    int containerSize = 0;
    uint64_t openRevision = 0;
    uint64_t closeRevision = 0;
    uint64_t revision = 0;
    bool pending = false;
    float furnaceProgress = 0.0f;
    float furnaceFlame = 0.0f;
};

struct InventoryRecipe {
    CraftingRecipeEntry recipe;
    ItemStack output;
    bool shaped = false;
};

/** Pure inventory rules and request planning, independent of rendering and networking. */
class InventoryModel {
public:
    std::array<ItemStack, inventory::SlotCount> slots {};
    ContainerType type = ContainerType::Inventory;
    int windowId = 0;
    int containerSize = 0;
    bool creativeMode = false;
    std::vector<InventoryRecipe> recipes;
    std::map<int, ItemStack> creative;
    std::map<std::string, std::vector<std::string>> itemTags;

    static bool empty(const ItemStack& item);
    static bool same(const ItemStack& a, const ItemStack& b);
    static int maxStack(const ItemStack& item);
    static int armorSlot(const ItemStack& item);
    bool accepts(int slot, const ItemStack& item) const;
    int gridSize() const { return type == ContainerType::Workbench ? 3 : 2; }
    int packetSlot(int container, int slot) const;
    int responseSlot(ContainerSlotType container, int slot) const;
    ItemStackRequestSlotData networkSlot(int slot) const;
    ItemStackRequest plan(const InventoryCommand& command, int requestId);
    const InventoryRecipe* matchingRecipe(std::vector<std::pair<int, int>>* consumption = nullptr) const;
    bool canCraft(const InventoryRecipe& recipe) const;
    bool ingredientMatches(const RecipeIngredientEntry& ingredient, const ItemStack& item) const;

private:
    int move(ItemStackRequest& request, int from, int to, int count);
    void swap(ItemStackRequest& request, int from, int to);
    void remove(ItemStackRequest& request, int slot, int count, ItemStackRequestActionType type);
    void quickMove(ItemStackRequest& request, int slot);
    void returnItems(ItemStackRequest& request);
    bool craft(ItemStackRequest& request, const InventoryRecipe& recipe, const std::vector<std::pair<int, int>>& consumption, bool toInventory);
};

}
