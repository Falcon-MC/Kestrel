#include "client/Inventory.h"
#include "client/ContainerLayout.h"
#include "world/ItemInfo.h"

#include <algorithm>
#include <functional>
#include <numeric>

namespace kestrel {
namespace {
using namespace inventory;

bool furnace(ContainerType type)
{
    return type == ContainerType::Furnace || type == ContainerType::BlastFurnace || type == ContainerType::Smoker;
}
}

bool InventoryModel::same(const ItemStack& a, const ItemStack& b)
{
    return !empty(a) && !empty(b) && a.mDefinition->getIdentifier() == b.mDefinition->getIdentifier()
        && a.mDamage == b.mDamage && a.mTag == b.mTag && a.mCanPlace == b.mCanPlace && a.mCanBreak == b.mCanBreak;
}

bool InventoryModel::accepts(int slot, const ItemStack& item) const
{
    if (slot < 0 || slot >= SlotCount || slot == Output) return false;
    if (empty(item)) return true;
    if (slot >= Armor && slot < Offhand) return armorSlot(item) == slot;
    if (slot == Offhand) {
        const std::string& name = item.mDefinition->getIdentifier();
        return name == "minecraft:shield" || name == "minecraft:totem_of_undying" || name == "minecraft:arrow"
            || name == "minecraft:firework_rocket" || name == "minecraft:map" || name == "minecraft:filled_map"
            || name == "minecraft:nautilus_shell";
    }
    if (slot >= Craft && slot < Cursor) {
        return (type == ContainerType::Inventory || type == ContainerType::Workbench) && slot < Craft + gridSize() * gridSize();
    }
    if (slot >= Ui) {
        InventoryState view;
        view.type = type;
        for (const auto& collection : containerLayout(view).collections) {
            if (std::find(collection.slots.begin(), collection.slots.end(), slot) != collection.slots.end()) {
                const auto& name = item.mDefinition->getIdentifier();
                switch (slot - Ui) {
                case 9: return name == "minecraft:banner" || name.ends_with("_banner");
                case 10: return name == "minecraft:dye" || name.ends_with("_dye");
                case 11: return name.ends_with("_banner_pattern");
                case 15: return name == "minecraft:lapis_lazuli" || (name == "minecraft:dye" && item.mDamage == 4);
                case 27: return name == "minecraft:iron_ingot" || name == "minecraft:gold_ingot" || name == "minecraft:emerald" || name == "minecraft:diamond" || name == "minecraft:netherite_ingot";
                case 53: return name.ends_with("_smithing_template");
                default: return personalSlotType(slot - Ui, type) != ContainerSlotType::Unknown;
                }
            }
        }
        return false;
    }
    if (slot >= Container) {
        const std::string& name = item.mDefinition->getIdentifier();
        int index = slot - Container;
        if (type == ContainerType::BrewingStand) {
            if (index == 4) {
                return name == "minecraft:blaze_powder";
            }
            if (index >= 1 && index <= 3) {
                return name == "minecraft:potion" || name == "minecraft:splash_potion" || name == "minecraft:lingering_potion" || name == "minecraft:glass_bottle";
            }
        }
        if (type == ContainerType::Horse && index < 2) {
            auto equipment = mountSlots(mountIdentifier);
            if (index == 0) {
                return equipment.saddle && name == "minecraft:saddle";
            }
            return (equipment.body == MountSlots::Body::HorseArmor && name.ends_with("_horse_armor"))
                || (equipment.body == MountSlots::Body::Carpet && (name == "minecraft:carpet" || name.ends_with("_carpet")))
                || (equipment.body == MountSlots::Body::NautilusArmor && name.ends_with("_nautilus_armor"));
        }
        return slot < Container + containerSize && !(furnace(type) && slot == Container + 2)
            && !(type == ContainerType::Crafter && (slot - Container >= 9 || (disabledSlots & (1 << (slot - Container))) != 0));
    }
    return true;
}

int InventoryModel::packetSlot(int container, int slot) const
{
    if (slot < 0) return -1;
    if (container == 0 && slot < 36) return slot;
    if (container == 120 && slot < 4) return Armor + slot;
    if (container == 119 && slot == 0) return Offhand;
    if (container == 124) {
        return personalUiSlot(slot, type);
    }
    if (windowId != 0 && container == windowId && type == ContainerType::Trade) {
        return slot == 4 || slot == 5 ? Ui + slot : -1;
    }
    if (windowId != 0 && container == windowId && slot < 54) return Container + slot;
    return -1;
}

int InventoryModel::responseSlot(ContainerSlotType container, int slot) const
{
    for (int index = 1; index < 54; ++index) {
        if (container != ContainerSlotType::Unknown && personalSlotType(index, type) == container) {
            return personalUiSlot(index, type);
        }
    }
    switch (container) {
    case ContainerSlotType::AnvilResult:
    case ContainerSlotType::SmithingTableResult:
    case ContainerSlotType::GrindstoneResult:
    case ContainerSlotType::LoomResult:
    case ContainerSlotType::StonecutterResult:
    case ContainerSlotType::CartographyResult:
    case ContainerSlotType::Trade2Result: return Output;
    case ContainerSlotType::BrewingInput: return Container;
    case ContainerSlotType::BrewingResult: return slot >= 1 && slot <= 3 ? Container + slot : -1;
    case ContainerSlotType::BrewingFuel: return Container + 4;
    case ContainerSlotType::HorseEquip: return slot >= 0 && slot < 2 ? Container + slot : -1;
    case ContainerSlotType::CrafterBlockContainer: return slot >= 0 && slot < 9 ? Container + slot : -1;
    case ContainerSlotType::HotbarAndInventory:
    case ContainerSlotType::Hotbar:
    case ContainerSlotType::Inventory: return slot >= 0 && slot < 36 ? slot : -1;
    case ContainerSlotType::Armor: return slot >= 0 && slot < 4 ? Armor + slot : -1;
    case ContainerSlotType::Offhand: return slot == 0 ? Offhand : -1;
    case ContainerSlotType::Cursor: return slot == 0 ? Cursor : -1;
    case ContainerSlotType::CraftingInput: return packetSlot(124, slot);
    case ContainerSlotType::CraftingOutput:
    case ContainerSlotType::CreatedOutput: return Output;
    case ContainerSlotType::LevelEntity:
    case ContainerSlotType::Barrel:
    case ContainerSlotType::ShulkerBox: return slot >= 0 && slot < containerSize ? Container + slot : -1;
    case ContainerSlotType::FurnaceIngredient:
    case ContainerSlotType::BlastFurnaceIngredient:
    case ContainerSlotType::SmokerIngredient: return Container;
    case ContainerSlotType::FurnaceFuel: return Container + 1;
    case ContainerSlotType::FurnaceResult: return Container + 2;
    default: return -1;
    }
}

ItemStackRequestSlotData InventoryModel::networkSlot(int slot) const
{
    ItemStackRequestSlotData data;
    // The game names player cells by row; without an open screen the server only accepts the hotbar name.
    data.mContainer = slot < 9 ? ContainerSlotType::Hotbar : ContainerSlotType::Inventory;
    data.mSlot = slot;
    if (slot >= Armor && slot < Offhand) { data.mContainer = ContainerSlotType::Armor; data.mSlot -= Armor; }
    else if (slot == Offhand) { data.mContainer = ContainerSlotType::Offhand; data.mSlot = 0; }
    else if (slot >= Craft && slot < Cursor) { data.mContainer = ContainerSlotType::CraftingInput; data.mSlot = slot - Craft + (gridSize() == 3 ? 32 : 28); }
    else if (slot == Cursor) { data.mContainer = ContainerSlotType::Cursor; data.mSlot = 0; }
    else if (slot == Output) { data.mContainer = ContainerSlotType::CreatedOutput; data.mSlot = 50; }
    else if (slot >= Ui) {
        data.mContainer = personalSlotType(slot - Ui, type);
        data.mSlot = slot - Ui;
    }
    else if (slot >= Container) {
        data.mContainer = ContainerSlotType::LevelEntity;
        data.mSlot -= Container;
        if (type == ContainerType::BrewingStand) {
            data.mContainer = data.mSlot == 0 ? ContainerSlotType::BrewingInput : data.mSlot == 4 ? ContainerSlotType::BrewingFuel : ContainerSlotType::BrewingResult;
        } else if (type == ContainerType::Horse && data.mSlot < 2) {
            data.mContainer = ContainerSlotType::HorseEquip;
        } else if (type == ContainerType::Crafter) {
            data.mContainer = ContainerSlotType::CrafterBlockContainer;
        }
        if (furnace(type)) {
            data.mContainer = data.mSlot == 1 ? ContainerSlotType::FurnaceFuel : data.mSlot == 2 ? ContainerSlotType::FurnaceResult
                : type == ContainerType::BlastFurnace ? ContainerSlotType::BlastFurnaceIngredient
                : type == ContainerType::Smoker ? ContainerSlotType::SmokerIngredient : ContainerSlotType::FurnaceIngredient;
        }
    }
    data.mContainerName.mContainer = data.mContainer;
    data.mStackNetworkId = slot >= 0 && slot < SlotCount && !empty(slots[slot]) ? slots[slot].mNetId : 0;
    return data;
}

int InventoryModel::move(ItemStackRequest& request, int from, int to, int count)
{
    if (from == to || from < 0 || from >= SlotCount || to < 0 || to >= SlotCount || !accepts(to, slots[from])) return 0;
    ItemStack& source = slots[from];
    ItemStack& dest = slots[to];
    if (empty(source) || (!empty(dest) && !same(source, dest))) return 0;
    int limit = to >= Armor && to <= Offhand ? 1 : maxStack(source);
    if ((type == ContainerType::Horse && to >= Container && to < Container + 2)
        || (type == ContainerType::BrewingStand && to >= Container + 1 && to <= Container + 3)) {
        limit = 1;
    }
    count = std::min({ count, source.mCount, limit - (empty(dest) ? 0 : dest.mCount) });
    if (count <= 0) return 0;
    ItemStackRequestAction action;
    action.mType = to == Cursor ? ItemStackRequestActionType::Take : ItemStackRequestActionType::Place;
    action.mCount = count;
    action.mSource = networkSlot(from);
    action.mDestination = networkSlot(to);
    request.mActions.push_back(action);
    if (empty(dest)) { dest = source; dest.mCount = 0; }
    dest.mCount += count;
    dest.mNetId = request.mRequestId;
    source.mCount -= count;
    if (source.mCount <= 0) source = ItemStack::air();
    else source.mNetId = request.mRequestId;
    return count;
}

void InventoryModel::swap(ItemStackRequest& request, int from, int to)
{
    if (from == to || from < 0 || from >= SlotCount || to < 0 || to >= SlotCount
        || !accepts(from, slots[to]) || !accepts(to, slots[from])) return;
    if (empty(slots[from]) && empty(slots[to])) return;
    if ((from >= Armor && from <= Offhand && slots[to].mCount > 1) || (to >= Armor && to <= Offhand && slots[from].mCount > 1)) return;
    auto single = [&](int slot) {
        return (type == ContainerType::Horse && slot >= Container && slot < Container + 2)
            || (type == ContainerType::BrewingStand && slot >= Container + 1 && slot <= Container + 3);
    };
    if ((single(from) && slots[to].mCount > 1) || (single(to) && slots[from].mCount > 1)) {
        return;
    }
    ItemStackRequestAction action;
    action.mType = ItemStackRequestActionType::Swap;
    action.mSource = networkSlot(from);
    action.mDestination = networkSlot(to);
    request.mActions.push_back(action);
    std::swap(slots[from], slots[to]);
    for (int slot : { from, to }) if (!empty(slots[slot])) slots[slot].mNetId = request.mRequestId;
}

void InventoryModel::remove(ItemStackRequest& request, int slot, int count, ItemStackRequestActionType actionType)
{
    if (slot < 0 || slot >= SlotCount || empty(slots[slot])) return;
    count = std::min(count, slots[slot].mCount);
    if (count <= 0) return;
    ItemStackRequestAction action;
    action.mType = actionType;
    action.mSource = networkSlot(slot);
    action.mCount = count;
    request.mActions.push_back(action);
    slots[slot].mCount -= count;
    if (slots[slot].mCount <= 0) slots[slot] = ItemStack::air();
    else slots[slot].mNetId = request.mRequestId;
}

void InventoryModel::quickMove(ItemStackRequest& request, int from)
{
    if (from < 0 || from >= SlotCount || empty(slots[from])) return;
    std::vector<int> destinations;
    if (from < 36 && containerSize > 0) {
        for (int i = 0; i < containerSize; ++i) destinations.push_back(Container + i);
    } else if (from < 36) {
        if (type != ContainerType::Inventory && type != ContainerType::Workbench) {
            InventoryState view;
            view.type = type;
            for (const auto& collection : containerLayout(view).collections) {
                for (int target : collection.slots) {
                    if (target >= Ui && accepts(target, slots[from])) {
                        destinations.push_back(target);
                    }
                }
            }
        }
        int armor = armorSlot(slots[from]);
        if (armor >= 0 && empty(slots[armor])) destinations.push_back(armor);
        if (slots[from].mDefinition->getIdentifier() == "minecraft:shield" && empty(slots[Offhand])) destinations.push_back(Offhand);
        int begin = from < 9 ? 9 : 0, end = from < 9 ? 36 : 9;
        for (int i = begin; i < end; ++i) destinations.push_back(i);
    } else {
        for (int i = 0; i < 36; ++i) destinations.push_back(i);
    }
    for (bool merging : { true, false }) {
        for (int to : destinations) {
            if (empty(slots[from])) return;
            if (empty(slots[to]) == merging) continue;
            move(request, from, to, slots[from].mCount);
        }
    }
}

void InventoryModel::returnItems(ItemStackRequest& request)
{
    for (int from = Craft; from <= Cursor; ++from) {
        quickMove(request, from);
        if (!empty(slots[from])) remove(request, from, slots[from].mCount, ItemStackRequestActionType::Drop);
    }
    InventoryState view;
    view.type = type;
    for (const auto& collection : containerLayout(view).collections) {
        for (int slot : collection.slots) {
            if (slot >= Ui) {
                quickMove(request, slot);
                if (!empty(slots[slot])) {
                    remove(request, slot, slots[slot].mCount, ItemStackRequestActionType::Drop);
                }
            }
        }
    }
}

bool InventoryModel::ingredientMatches(const RecipeIngredientEntry& ingredient, const ItemStack& item) const
{
    if (!ingredient.mHasItem) return empty(item);
    if (empty(item) || item.mCount < std::max(1, ingredient.mCount)) return false;
    const std::string& name = item.mDefinition->getIdentifier();
    if (ingredient.mType == RecipeIngredientType::Name) {
        std::string expected = ingredient.mItemId.find(':') == std::string::npos ? "minecraft:" + ingredient.mItemId : ingredient.mItemId;
        return name == expected && (ingredient.mAuxValue < 0 || ingredient.mAuxValue == 32767 || ingredient.mAuxValue == item.mDamage);
    }
    if (ingredient.mType == RecipeIngredientType::ItemTag) {
        auto found = itemTags.find(ingredient.mItemTag);
        return found != itemTags.end() && std::find(found->second.begin(), found->second.end(), name) != found->second.end();
    }
    return false;
}

const InventoryRecipe* InventoryModel::matchingRecipe(std::vector<std::pair<int, int>>* consumption) const
{
    int size = gridSize();
    for (const auto& entry : recipes) {
        const auto& recipe = entry.recipe;
        if (recipe.mBlockName != "crafting_table" && recipe.mBlockName != "minecraft:crafting_table") {
            continue;
        }
        std::vector<std::pair<int, int>> used;
        if (entry.shaped) {
            if (recipe.mWidth < 1 || recipe.mHeight < 1 || recipe.mWidth > size || recipe.mHeight > size
                || recipe.mInputs.size() != size_t(recipe.mWidth * recipe.mHeight)) continue;
            for (int oy = 0; oy <= size - recipe.mHeight; ++oy) for (int ox = 0; ox <= size - recipe.mWidth; ++ox) for (bool mirror : { false, true }) {
                bool match = true;
                used.clear();
                for (int y = 0; y < size && match; ++y) for (int x = 0; x < size && match; ++x) {
                    int slot = Craft + y * size + x;
                    if (x < ox || x >= ox + recipe.mWidth || y < oy || y >= oy + recipe.mHeight) { match = empty(slots[slot]); continue; }
                    int rx = mirror ? recipe.mWidth - 1 - (x - ox) : x - ox;
                    const auto& ingredient = recipe.mInputs[(y - oy) * recipe.mWidth + rx];
                    match = ingredientMatches(ingredient, slots[slot]);
                    if (match && ingredient.mHasItem) used.emplace_back(slot, std::max(1, ingredient.mCount));
                }
                if (match && !used.empty()) { if (consumption) *consumption = used; return &entry; }
            }
        } else {
            std::vector<int> occupied;
            for (int i = 0; i < size * size; ++i) if (!empty(slots[Craft + i])) occupied.push_back(Craft + i);
            std::vector<const RecipeIngredientEntry*> ingredients;
            for (const auto& ingredient : recipe.mInputs) if (ingredient.mHasItem) ingredients.push_back(&ingredient);
            if (occupied.size() != ingredients.size() || occupied.empty()) continue;
            std::vector<bool> taken(occupied.size());
            std::function<bool(size_t)> match = [&](size_t index) {
                if (index == ingredients.size()) return true;
                for (size_t j = 0; j < occupied.size(); ++j) if (!taken[j] && ingredientMatches(*ingredients[index], slots[occupied[j]])) {
                    taken[j] = true;
                    used.emplace_back(occupied[j], std::max(1, ingredients[index]->mCount));
                    if (match(index + 1)) return true;
                    used.pop_back(); taken[j] = false;
                }
                return false;
            };
            if (match(0)) { if (consumption) *consumption = used; return &entry; }
        }
    }
    return nullptr;
}

bool InventoryModel::fitsGrid(const InventoryRecipe& entry) const
{
    if (entry.recipe.mBlockName != "crafting_table" && entry.recipe.mBlockName != "minecraft:crafting_table") {
        return false;
    }
    if (entry.shaped) return entry.recipe.mWidth <= gridSize() && entry.recipe.mHeight <= gridSize();
    int ingredients = int(std::count_if(entry.recipe.mInputs.begin(), entry.recipe.mInputs.end(), [](const auto& input) { return input.mHasItem; }));
    return ingredients <= gridSize() * gridSize();
}

int InventoryModel::recipeCell(const InventoryRecipe& entry, int index) const
{
    return entry.shaped ? (index / entry.recipe.mWidth) * gridSize() + index % entry.recipe.mWidth : index;
}

bool InventoryModel::recipeGhost(int recipeNetId, std::array<HudItem, 9>& cells, HudItem& output) const
{
    auto found = std::find_if(recipes.begin(), recipes.end(), [&](const auto& r) { return r.recipe.mRecipeNetId == recipeNetId; });
    if (found == recipes.end() || !fitsGrid(*found)) return false;
    cells = {};
    int index = 0;
    for (const auto& ingredient : found->recipe.mInputs) {
        int cell = recipeCell(*found, index++);
        if (!ingredient.mHasItem || cell < 0 || cell >= 9) continue;
        HudItem& ghost = cells[cell];
        if (ingredient.mType == RecipeIngredientType::ItemTag) {
            auto tagged = itemTags.find(ingredient.mItemTag);
            if (tagged == itemTags.end() || tagged->second.empty()) continue;
            ghost.identifier = tagged->second.front();
        } else if (ingredient.mType == RecipeIngredientType::Name) {
            ghost.identifier = ingredient.mItemId.find(':') == std::string::npos ? "minecraft:" + ingredient.mItemId : ingredient.mItemId;
            ghost.aux = ingredient.mAuxValue < 0 || ingredient.mAuxValue == 32767 ? 0 : ingredient.mAuxValue;
        } else {
            continue;
        }
        ghost.count = std::max(1, ingredient.mCount);
    }
    output = hudItemOf(found->output);
    return true;
}

bool InventoryModel::canCraft(const InventoryRecipe& entry) const
{
    if (!fitsGrid(entry)) {
        return false;
    }
    if (entry.shaped && (entry.recipe.mWidth > gridSize() || entry.recipe.mHeight > gridSize())) return false;
    std::array<int, 36> remaining {};
    for (int i = 0; i < 36; ++i) remaining[i] = empty(slots[i]) ? 0 : slots[i].mCount;
    for (const auto& ingredient : entry.recipe.mInputs) {
        if (!ingredient.mHasItem) continue;
        int needed = std::max(1, ingredient.mCount);
        auto one = ingredient; one.mCount = 1;
        for (int i = 0; i < 36 && needed > 0; ++i) if (remaining[i] && ingredientMatches(one, slots[i])) {
            int count = std::min(needed, remaining[i]); needed -= count; remaining[i] -= count;
        }
        if (needed) return false;
    }
    return true;
}

int InventoryModel::inventoryRoom(const ItemStack& item) const
{
    int room = 0;
    int limit = maxStack(item);
    for (int i = 0; i < 36; ++i) {
        if (empty(slots[i])) room += limit;
        else if (same(slots[i], item)) room += std::max(0, limit - slots[i].mCount);
    }
    return room;
}

/**
 * Moves what is in the created output into the inventory, topping up
 * matching stacks before filling empty slots, hotbar first.
 */
void InventoryModel::placeInInventory(ItemStackRequest& request)
{
    for (bool merging : { true, false }) {
        for (int i = 0; i < 9; ++i) if (!empty(slots[Output]) && empty(slots[i]) != merging) move(request, Output, i, slots[Output].mCount);
        for (int i = 35; i >= 9; --i) if (!empty(slots[Output]) && empty(slots[i]) != merging) move(request, Output, i, slots[Output].mCount);
    }
}

bool InventoryModel::craft(ItemStackRequest& request, const InventoryRecipe& entry, const std::vector<std::pair<int, int>>& consumption, bool toInventory)
{
    int perCraft = std::max(1, entry.output.mCount);
    int repetitions = 1;
    if (!entry.extras.empty()) {
        auto before = slots;
        bool cursorFree = empty(slots[Cursor]) || (same(slots[Cursor], entry.output) && slots[Cursor].mCount + perCraft <= maxStack(entry.output));
        if (!toInventory && !cursorFree) return false;
        if (toInventory && inventoryRoom(entry.output) < perCraft) return false;

        ItemStackRequestAction action;
        action.mType = ItemStackRequestActionType::CraftRecipe;
        action.mRecipeNetworkId = entry.recipe.mRecipeNetId;
        action.mNumberOfRequestedCrafts = 1;
        request.mActions.push_back(action);
        ItemStackRequestAction results;
        results.mType = ItemStackRequestActionType::CraftResultsDeprecated;
        results.mResultItems.push_back(entry.output);
        results.mResultItems.insert(results.mResultItems.end(), entry.extras.begin(), entry.extras.end());
        results.mTimesCrafted = 1;
        request.mActions.push_back(results);
        for (auto [slot, count] : consumption) remove(request, slot, count, ItemStackRequestActionType::Consume);

        for (size_t index = 0; index <= entry.extras.size(); ++index) {
            const ItemStack& result = index == 0 ? entry.output : entry.extras[index - 1];
            ItemStackRequestAction create;
            create.mType = ItemStackRequestActionType::Create;
            create.mSlot = static_cast<int32_t>(index);
            request.mActions.push_back(create);
            slots[Output] = result;
            slots[Output].mCount = std::max(1, result.mCount);
            slots[Output].mNetId = request.mRequestId;
            if (index == 0 && !toInventory) move(request, Output, Cursor, slots[Output].mCount);
            else placeInInventory(request);
            if (!empty(slots[Output])) {
                slots = before;
                request.mActions.clear();
                return false;
            }
        }
        return true;
    }
    if (toInventory) {
        repetitions = std::min(MaxCraftRepetitions, maxStack(entry.output) / perCraft);
        for (auto [slot, count] : consumption) repetitions = std::min(repetitions, slots[slot].mCount / std::max(1, count));
        repetitions = std::min(repetitions, inventoryRoom(entry.output) / perCraft);
        if (repetitions < 1) return false;
    } else if (!empty(slots[Cursor]) && (!same(slots[Cursor], entry.output) || slots[Cursor].mCount + perCraft > maxStack(entry.output))) {
        return false;
    }

    ItemStackRequestAction action;
    action.mType = ItemStackRequestActionType::CraftRecipe;
    action.mRecipeNetworkId = entry.recipe.mRecipeNetId;
    action.mNumberOfRequestedCrafts = repetitions;
    request.mActions.push_back(action);

    ItemStackRequestAction results;
    results.mType = ItemStackRequestActionType::CraftResultsDeprecated;
    results.mResultItems.push_back(entry.output);
    results.mTimesCrafted = repetitions;
    request.mActions.push_back(results);

    for (auto [slot, count] : consumption) remove(request, slot, count * repetitions, ItemStackRequestActionType::Consume);
    slots[Output] = entry.output;
    slots[Output].mCount = perCraft * repetitions;
    slots[Output].mNetId = request.mRequestId;
    if (!toInventory) {
        move(request, Output, Cursor, slots[Output].mCount);
        return true;
    }
    placeInInventory(request);
    slots[Output] = ItemStack::air();
    return true;
}

ItemStackRequest InventoryModel::plan(const InventoryCommand& command, int requestId)
{
    ItemStackRequest request;
    request.mRequestId = requestId;
    int slot = command.slot;
    if (command.action == InventoryAction::StationRecipe && type == ContainerType::Trade) {
        selectTrade(request, command.value);
        return request;
    }
    if (command.action == InventoryAction::StationRecipe) {
        if (type == ContainerType::Loom) {
            auto patterns = availableLoomPatterns();
            if (command.value >= 0 && command.value < int(patterns.size())) {
                loomPattern = patterns[command.value];
            }
        } else {
            stationRecipe = command.value;
        }
        return request;
    }
    if (command.action == InventoryAction::Rename && type == ContainerType::Anvil) {
        stationName = command.text;
        stationNameEdited = true;
        return request;
    }
    if (command.action == InventoryAction::Enchant && type == ContainerType::Enchantment) {
        ItemStackRequestAction enchant;
        enchant.mType = ItemStackRequestActionType::CraftRecipe;
        enchant.mRecipeNetworkId = command.value;
        enchant.mNumberOfRequestedCrafts = 1;
        request.mActions.push_back(enchant);
        ItemStackRequestAction results;
        results.mType = ItemStackRequestActionType::CraftResultsDeprecated;
        results.mTimesCrafted = 1;
        request.mActions.push_back(results);
        return request;
    }
    if (command.action == InventoryAction::Beacon && type == ContainerType::Beacon) {
        const int powers[] = { 1, 3, 11, 8, 5 };
        if (std::find(std::begin(powers), std::end(powers), command.slot) == std::end(powers)
            || (command.value != 0 && command.value != 10 && command.value != command.slot) || empty(slots[Ui + 27])) {
            return request;
        }
        ItemStackRequestAction payment;
        payment.mType = ItemStackRequestActionType::BeaconPayment;
        payment.mPrimaryEffect = command.slot;
        payment.mSecondaryEffect = command.value;
        request.mActions.push_back(payment);
        remove(request, Ui + 27, 1, ItemStackRequestActionType::Destroy);
        return request;
    }
    if (command.action == InventoryAction::Close) { returnItems(request); return request; }
    if (command.action == InventoryAction::Destroy) {
        if (creativeMode && slot >= 0 && slot <= Cursor && !empty(slots[slot])) {
            remove(request, slot, slots[slot].mCount, ItemStackRequestActionType::Destroy);
        }
        return request;
    }
    if (command.action == InventoryAction::Creative || command.action == InventoryAction::CreativeDrop) {
        auto found = creative.find(command.value);
        if (!creativeMode || found == creative.end()) return request;
        bool dropping = command.action == InventoryAction::CreativeDrop;
        bool anywhere = !dropping && command.slot == AnyInventorySlot;
        if (anywhere && inventoryRoom(found->second) <= 0) return request;
        int target = command.slot >= 0 && command.slot < 9 ? command.slot : Cursor;
        if (!dropping && !anywhere && !empty(slots[target])) remove(request, target, slots[target].mCount, ItemStackRequestActionType::Destroy);
        ItemStackRequestAction action;
        action.mType = ItemStackRequestActionType::CraftCreative;
        action.mCreativeItemNetworkId = command.value;
        action.mNumberOfRequestedCrafts = 1;
        request.mActions.push_back(action);
        slots[Output] = found->second;
        slots[Output].mCount = maxStack(found->second);
        slots[Output].mNetId = requestId;
        if (dropping) remove(request, Output, command.all ? slots[Output].mCount : 1, ItemStackRequestActionType::Drop);
        else if (anywhere) placeInInventory(request);
        else move(request, Output, target, command.all ? slots[Output].mCount : 1);
        // Creative crafting supplies a full stack; dispose of the unused generated items.
        if (!empty(slots[Output])) remove(request, Output, slots[Output].mCount, ItemStackRequestActionType::Destroy);
        return request;
    }
    if (command.action == InventoryAction::SelectRecipe) {
        auto found = std::find_if(recipes.begin(), recipes.end(), [&](const auto& r) { return r.recipe.mRecipeNetId == command.value; });
        if (found == recipes.end()) return request;
        if (!fitsGrid(*found)) return request;
        returnItems(request);
        int index = 0;
        for (const auto& ingredient : found->recipe.mInputs) {
            int dest = Craft + recipeCell(*found, index);
            ++index;
            if (!ingredient.mHasItem) continue;
            int remaining = std::max(1, ingredient.mCount);
            auto one = ingredient; one.mCount = 1;
            for (int i = 0; i < 36 && remaining > 0; ++i) if (ingredientMatches(one, slots[i])) remaining -= move(request, i, dest, remaining);
        }
        return request;
    }
    if ((slot == Output || command.action == InventoryAction::Craft) && type == ContainerType::Trade) {
        takeTrade(request, command.all || command.action == InventoryAction::QuickMove);
        return request;
    }
    if (slot == Output || command.action == InventoryAction::Craft) {
        std::vector<std::pair<int, int>> consumption;
        const auto* recipe = type == ContainerType::Inventory || type == ContainerType::Workbench ? matchingRecipe(&consumption) : matchingStationRecipe(&consumption);
        if (recipe) {
            InventoryRecipe selected = *recipe;
            if (type != ContainerType::Inventory && type != ContainerType::Workbench) {
                selected.output = stationRecipeResult(selected);
            }
            if (!empty(selected.output)) {
                craft(request, selected, consumption, command.all || command.action == InventoryAction::QuickMove);
            }
        } else if (type == ContainerType::Anvil || type == ContainerType::Grindstone || type == ContainerType::Loom) {
            takeStationOutput(request, command.action == InventoryAction::QuickMove);
        }
        return request;
    }
    if (command.action == InventoryAction::Distribute) {
        std::vector<int> targets;
        for (int target : command.slots) if (accepts(target, slots[Cursor]) && target != Cursor && (empty(slots[target]) || same(slots[target], slots[Cursor]))
            && std::find(targets.begin(), targets.end(), target) == targets.end()) targets.push_back(target);
        if (!targets.empty()) {
            int amount = command.all ? std::max(1, slots[Cursor].mCount / int(targets.size())) : 1;
            for (int target : targets) move(request, Cursor, target, amount);
        }
        return request;
    }
    if (slot < 0 || slot >= SlotCount) return request;
    switch (command.action) {
    case InventoryAction::Primary:
    case InventoryAction::Secondary:
        if (empty(slots[Cursor])) move(request, slot, Cursor, command.action == InventoryAction::Secondary ? (slots[slot].mCount + 1) / 2 : slots[slot].mCount);
        else if (empty(slots[slot]) || same(slots[slot], slots[Cursor])) move(request, Cursor, slot, command.action == InventoryAction::Secondary ? 1 : slots[Cursor].mCount);
        else swap(request, slot, Cursor);
        break;
    case InventoryAction::QuickMove: quickMove(request, slot); break;
    case InventoryAction::HotbarSwap: if (command.value >= 0 && command.value < 9) swap(request, slot, command.value); break;
    case InventoryAction::Move:
        if (slot < 36 && command.value >= 0 && command.value < 36) {
            move(request, slot, command.value, command.count > 0 ? command.count : slots[slot].mCount);
        }
        break;
    case InventoryAction::Drop: remove(request, slot, command.all ? slots[slot].mCount : 1, ItemStackRequestActionType::Drop); break;
    case InventoryAction::Collect:
        if (!empty(slots[Cursor])) for (int i = 0; i < SlotCount; ++i) {
            int room = maxStack(slots[Cursor]) - slots[Cursor].mCount;
            if (room <= 0) break;
            if (i != Cursor && i != Output && accepts(i, slots[i]) && same(slots[i], slots[Cursor])) move(request, i, Cursor, std::min(room, slots[i].mCount));
        }
        break;
    default: break;
    }
    return request;
}
}
