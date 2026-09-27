#include "client/Inventory.h"
#include "world/ItemInfo.h"

#include <algorithm>
#include <functional>
#include <numeric>

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

bool furnace(ContainerType type)
{
    return type == ContainerType::Furnace || type == ContainerType::BlastFurnace || type == ContainerType::Smoker;
}
}

bool InventoryModel::empty(const ItemStack& item) { return item.isAir() || item.mCount <= 0; }

bool InventoryModel::same(const ItemStack& a, const ItemStack& b)
{
    return !empty(a) && !empty(b) && a.mDefinition->getIdentifier() == b.mDefinition->getIdentifier()
        && a.mDamage == b.mDamage && a.mTag == b.mTag && a.mCanPlace == b.mCanPlace && a.mCanBreak == b.mCanBreak;
}

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
    if (name == "minecraft:ender_pearl" || name == "minecraft:egg" || name == "minecraft:snowball"
        || name == "minecraft:bucket" || name == "minecraft:armor_stand" || name.ends_with("_sign") || name == "minecraft:written_book") return 16;
    return 64;
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
    if (slot >= Craft && slot < Cursor) return slot < Craft + gridSize() * gridSize();
    if (slot >= Container) return slot < Container + containerSize && !(furnace(type) && slot == Container + 2);
    return true;
}

int InventoryModel::packetSlot(int container, int slot) const
{
    if (slot < 0) return -1;
    if (container == 0 && slot < 36) return slot;
    if (container == 120 && slot < 4) return Armor + slot;
    if (container == 119 && slot == 0) return Offhand;
    if (container == 124) {
        if (slot == 0) return Cursor;
        int start = gridSize() == 3 ? 32 : 28;
        if (slot >= start && slot < start + gridSize() * gridSize()) return Craft + slot - start;
        if (slot == 50) return Output;
    }
    if (windowId != 0 && container == windowId && slot < 54) return Container + slot;
    return -1;
}

int InventoryModel::responseSlot(ContainerSlotType container, int slot) const
{
    switch (container) {
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
    data.mContainer = ContainerSlotType::HotbarAndInventory;
    data.mSlot = slot;
    if (slot >= Armor && slot < Offhand) { data.mContainer = ContainerSlotType::Armor; data.mSlot -= Armor; }
    else if (slot == Offhand) { data.mContainer = ContainerSlotType::Offhand; data.mSlot = 0; }
    else if (slot >= Craft && slot < Cursor) { data.mContainer = ContainerSlotType::CraftingInput; data.mSlot = slot - Craft + (gridSize() == 3 ? 32 : 28); }
    else if (slot == Cursor) { data.mContainer = ContainerSlotType::Cursor; data.mSlot = 0; }
    else if (slot == Output) { data.mContainer = ContainerSlotType::CreatedOutput; data.mSlot = 50; }
    else if (slot >= Container) {
        data.mContainer = ContainerSlotType::LevelEntity;
        data.mSlot -= Container;
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
        int armor = armorSlot(slots[from]);
        if (armor >= 0 && empty(slots[armor])) destinations.push_back(armor);
        if (slots[from].mDefinition->getIdentifier() == "minecraft:shield" && empty(slots[Offhand])) destinations.push_back(Offhand);
        int begin = from < 9 ? 9 : 0, end = from < 9 ? 36 : 9;
        for (int i = begin; i < end; ++i) destinations.push_back(i);
    } else {
        for (int i = 9; i < 36; ++i) destinations.push_back(i);
        for (int i = 0; i < 9; ++i) destinations.push_back(i);
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

bool InventoryModel::canCraft(const InventoryRecipe& entry) const
{
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

bool InventoryModel::craft(ItemStackRequest& request, const InventoryRecipe& entry, const std::vector<std::pair<int, int>>& consumption, bool toInventory)
{
    int target = Cursor;
    if (toInventory) {
        target = -1;
        for (int i = 0; i < 36; ++i) if (same(slots[i], entry.output) && maxStack(slots[i]) - slots[i].mCount >= entry.output.mCount) { target = i; break; }
        if (target < 0) for (int i = 0; i < 36; ++i) if (empty(slots[i])) { target = i; break; }
    }
    if (target < 0 || (!empty(slots[target]) && (!same(slots[target], entry.output) || slots[target].mCount + entry.output.mCount > maxStack(entry.output)))) return false;
    ItemStackRequestAction action;
    action.mType = ItemStackRequestActionType::CraftRecipe;
    action.mRecipeNetworkId = entry.recipe.mRecipeNetId;
    action.mNumberOfRequestedCrafts = 1;
    request.mActions.push_back(action);
    for (auto [slot, count] : consumption) remove(request, slot, count, ItemStackRequestActionType::Consume);
    slots[Output] = entry.output;
    slots[Output].mNetId = request.mRequestId;
    move(request, Output, target, entry.output.mCount);
    return true;
}

ItemStackRequest InventoryModel::plan(const InventoryCommand& command, int requestId)
{
    ItemStackRequest request;
    request.mRequestId = requestId;
    int slot = command.slot;
    if (command.action == InventoryAction::Close) { returnItems(request); return request; }
    if (command.action == InventoryAction::Creative) {
        auto found = creative.find(command.value);
        if (!creativeMode || found == creative.end()) return request;
        int target = command.slot >= 0 && command.slot < 9 ? command.slot : Cursor;
        if (!empty(slots[target])) remove(request, target, slots[target].mCount, ItemStackRequestActionType::Destroy);
        ItemStackRequestAction action;
        action.mType = ItemStackRequestActionType::CraftCreative;
        action.mCreativeItemNetworkId = command.value;
        action.mNumberOfRequestedCrafts = 1;
        request.mActions.push_back(action);
        slots[Output] = found->second;
        slots[Output].mCount = maxStack(found->second);
        slots[Output].mNetId = requestId;
        move(request, Output, target, command.all ? slots[Output].mCount : 1);
        // Creative crafting supplies a full stack; dispose of the unused generated items.
        if (!empty(slots[Output])) remove(request, Output, slots[Output].mCount, ItemStackRequestActionType::Destroy);
        return request;
    }
    if (command.action == InventoryAction::SelectRecipe) {
        auto found = std::find_if(recipes.begin(), recipes.end(), [&](const auto& r) { return r.recipe.mRecipeNetId == command.value; });
        if (found == recipes.end()) return request;
        auto before = slots;
        returnItems(request);
        if (!canCraft(*found)) { slots = before; request.mActions.clear(); return request; }
        int index = 0;
        for (const auto& ingredient : found->recipe.mInputs) {
            int dest = Craft + (found->shaped ? (index / found->recipe.mWidth) * gridSize() + index % found->recipe.mWidth : index);
            ++index;
            if (!ingredient.mHasItem) continue;
            int remaining = std::max(1, ingredient.mCount);
            auto one = ingredient; one.mCount = 1;
            for (int i = 0; i < 36 && remaining > 0; ++i) if (ingredientMatches(one, slots[i])) remaining -= move(request, i, dest, remaining);
            if (remaining) { slots = before; request.mActions.clear(); return request; }
        }
        return request;
    }
    if (slot == Output || command.action == InventoryAction::Craft) {
        int crafts = command.all || command.action == InventoryAction::QuickMove ? 64 : 1;
        for (int i = 0; i < crafts; ++i) {
            std::vector<std::pair<int, int>> consumption;
            auto recipe = matchingRecipe(&consumption);
            if (!recipe || !craft(request, *recipe, consumption, command.all || command.action == InventoryAction::QuickMove)) break;
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
    case InventoryAction::Drop: remove(request, slot, command.all ? slots[slot].mCount : 1, ItemStackRequestActionType::Drop); break;
    case InventoryAction::Collect:
        if (!empty(slots[Cursor])) for (int i = 0; i < SlotCount; ++i) {
            int room = maxStack(slots[Cursor]) - slots[Cursor].mCount;
            if (room <= 0) break;
            if (i != Cursor && i != Output && same(slots[i], slots[Cursor])) move(request, i, Cursor, std::min(room, slots[i].mCount));
        }
        break;
    default: break;
    }
    return request;
}
}
