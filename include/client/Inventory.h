#pragma once

#include "Protocol/Types/ContainerType.h"
#include "Protocol/Types/CraftingRecipeEntry.h"
#include "Protocol/Types/ItemStackRequest.h"
#include "Protocol/Types/EnchantOptionData.h"

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
    std::vector<std::pair<std::string, int>> bannerPatterns;
    std::vector<std::pair<int, int>> enchantments;
    bool enchanted = false;
    bool handEquipped = false;

    /**
     * The map a filled map shows, from its map_uuid; 0 for any other item.
     */
    int64_t mapId = 0;
    int32_t useTicks = 0;
    bool empty() const { return identifier.empty() || count <= 0; }
    bool operator==(const HudItem&) const = default;
};

HudItem hudItemOf(const ItemStack& stack);

// Local slot addresses; protocol container names and UI offsets are translated at the boundary.
namespace inventory {
inline constexpr int Armor = 36, Offhand = 40, Craft = 41, Cursor = 50, Output = 51, Container = 52;
inline constexpr int Ui = Container + 54;
inline constexpr int SlotCount = Ui + 54;
// A creative pick that goes wherever the inventory has room, the way a shift click places it.
inline constexpr int AnyInventorySlot = -2;
}

enum class InventoryAction { Open, Close, Primary, Secondary, QuickMove, Drop, HotbarSwap, Collect, Distribute, Creative, Destroy, Craft, SelectRecipe, Enchant, Beacon, Rename, StationRecipe, NpcAction, BookPage, BookSign, ToggleCrafter };

struct InventoryCommand {
    InventoryAction action = InventoryAction::Primary;
    int slot = -1;
    int value = 0;
    bool all = false;
    std::vector<int> slots;
    std::string text;
};

struct InventoryCatalogItem {
    HudItem item;
    int networkId = 0;
    int category = 0;
    int group = -1;
    std::string groupName;
};

/**
 * One offer of a trader as the trade screen shows it: what it costs, what it
 * gives, the tier it belongs to, and whether it is sold out or still locked.
 */
struct TradeOfferView {
    HudItem buyA;
    HudItem buyB;
    HudItem sell;
    int countA = 0;
    int countB = 0;
    int originalCountA = 0;
    int originalCountB = 0;
    int tier = 0;
    bool soldOut = false;
    bool affordable = false;
};

struct InventoryState {
    std::array<HudItem, inventory::SlotCount> slots {};
    std::shared_ptr<const std::vector<InventoryCatalogItem>> creative;
    std::shared_ptr<const std::vector<InventoryCatalogItem>> recipes;
    std::vector<int> craftable;
    std::vector<InventoryCatalogItem> stationOptions;
    std::vector<TradeOfferView> trades;
    std::vector<int> tradeTierExperience;
    int tradeTier = 0;
    int traderExperience = 0;
    int selectedTrade = -1;
    std::vector<std::string> loomPatterns;
    int selectedStationRecipe = -1;
    std::string stationName;
    int stationCost = 0;
    std::array<HudItem, 9> recipeGhost {};
    HudItem recipeGhostOutput;
    ContainerType type = ContainerType::Inventory;
    int windowId = 0;
    int containerSize = 0;
    bool enderChest = false;
    std::string blockIdentifier;
    std::array<int, 3> blockPosition {};
    std::string mountIdentifier;
    uint64_t mountRuntimeId = 0;
    int disabledSlots = 0;
    int beaconLevel = 0;
    std::map<int, int> properties;
    std::vector<EnchantOptionData> enchantments;
    int experienceLevel = 0;
    std::string screen;
    std::string dialogue;
    std::string scene;
    uint64_t npcId = 0;
    std::vector<std::pair<int, std::string>> npcButtons;
    std::vector<std::string> pages;
    std::string author;
    std::string bookTitle;
    int bookSlot = -1;
    bool bookEditable = false;
    std::string customName;
    uint64_t openRevision = 0;
    uint64_t closeRevision = 0;
    uint64_t revision = 0;
    bool pending = false;
    float furnaceProgress = 0.0f;
    float furnaceFlame = 0.0f;
};

/**
 * A crafting recipe with its results; output is the first one, which the
 * grid shows, and extras are the rest, such as the buckets a cake gives back.
 */
struct InventoryRecipe {
    CraftingRecipeEntry recipe;
    ItemStack output;
    bool shaped = false;
    std::vector<ItemStack> extras;
    bool trim = false;
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
    std::string mountIdentifier;
    std::map<int, ItemStack> creative;
    std::map<std::string, std::vector<std::string>> itemTags;
    std::map<std::string, std::string> trimMaterials;
    std::map<std::string, std::string> trimPatterns;
    std::string stationName;
    bool stationNameEdited = false;
    std::shared_ptr<ItemDefinition> bookDefinition;
    std::string loomPattern;
    std::vector<std::string> availableLoomPatterns() const;
    int stationRecipe = -1;

    /**
     * A trader's offer: the network id the server knows it by, the items it
     * asks for at the current price and the one it gives, its tier and how
     * often it can still be used.
     */
    struct TradeOffer {
        int netId = 0;
        ItemStack buyA = ItemStack::air();
        ItemStack buyB = ItemStack::air();
        ItemStack sell = ItemStack::air();
        int countA = 0;
        int countB = 0;
        int tier = 0;
        int uses = 0;
        int maxUses = 0;
    };
    std::vector<TradeOffer> trades;
    int selectedTrade = -1;
    int tradeTier = 0;
    ItemStack tradePreview(std::vector<std::pair<int, int>>* consumption = nullptr) const;
    void selectTrade(ItemStackRequest& request, int index);
    void takeTrade(ItemStackRequest& request, bool toInventory);
    int repairRecipe = -1;
    int disabledSlots = 0;

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
    const InventoryRecipe* matchingStationRecipe(std::vector<std::pair<int, int>>* consumption = nullptr) const;
    ItemStack stationRecipeResult(const InventoryRecipe& recipe) const;
    ItemStack stationPreview(std::vector<std::pair<int, int>>* consumption = nullptr, int* cost = nullptr) const;
    ItemStack anvilPreview(std::vector<std::pair<int, int>>* consumption, int* cost) const;
    bool canCraft(const InventoryRecipe& recipe) const;

    /**
     * The grid cell, counted on a 3 wide row for the workbench and a 2 wide
     * one otherwise, that a recipe's input at index goes in.
     */
    int recipeCell(const InventoryRecipe& recipe, int index) const;
    bool fitsGrid(const InventoryRecipe& recipe) const;

    /**
     * What a recipe shows in the grid and the result slot when picked from
     * the recipe book, one example item per ingredient.
     */
    bool recipeGhost(int recipeNetId, std::array<HudItem, 9>& cells, HudItem& output) const;
    bool ingredientMatches(const RecipeIngredientEntry& ingredient, const ItemStack& item) const;

private:
    int move(ItemStackRequest& request, int from, int to, int count);
    void swap(ItemStackRequest& request, int from, int to);
    void remove(ItemStackRequest& request, int slot, int count, ItemStackRequestActionType type);
    void quickMove(ItemStackRequest& request, int slot);
    void returnItems(ItemStackRequest& request);
    void takeStationOutput(ItemStackRequest& request, bool toInventory);
    /**
     * Crafts the recipe in the grid once into the cursor, or as many times as
     * the grid and the inventory allow when toInventory, in a single craft
     * action the way the game sends it.
     */
    bool craft(ItemStackRequest& request, const InventoryRecipe& recipe, const std::vector<std::pair<int, int>>& consumption, bool toInventory);
    int inventoryRoom(const ItemStack& item) const;
    void placeInInventory(ItemStackRequest& request);

    static constexpr int MaxCraftRepetitions = 64;
};

}
