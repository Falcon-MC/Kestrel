#include "client/Inventory.h"

#include <algorithm>

namespace kestrel {

namespace {

constexpr int FirstIngredient = inventory::Ui + 4;
constexpr int SecondIngredient = inventory::Ui + 5;

/**
 * Whether a stack can pay for what an offer asks: the same item and data
 * value, and the same tag when the offer names one.
 */
bool pays(const ItemStack& price, const ItemStack& item)
{
    if (InventoryModel::empty(price) || InventoryModel::empty(item)) {
        return false;
    }
    if (price.mDefinition->getIdentifier() != item.mDefinition->getIdentifier() || price.mDamage != item.mDamage) {
        return false;
    }
    if (price.mTag.isCompound() && price.mTag.size() > 0) {
        return price.mTag == item.mTag;
    }
    return true;
}

}

/**
 * What the selected offer gives for the items in the two trade slots, with
 * how many of each it takes, or nothing while they do not pay for it or the
 * offer is sold out.
 */
ItemStack InventoryModel::tradePreview(std::vector<std::pair<int, int>>* consumption) const
{
    if (type != ContainerType::Trade || selectedTrade < 0 || selectedTrade >= int(trades.size())) {
        return ItemStack::air();
    }
    const TradeOffer& offer = trades[size_t(selectedTrade)];
    if (offer.tier > tradeTier || (offer.maxUses > 0 && offer.uses >= offer.maxUses)) {
        return ItemStack::air();
    }
    const ItemStack& first = slots[FirstIngredient];
    const ItemStack& second = slots[SecondIngredient];
    if (!pays(offer.buyA, first) || first.mCount < offer.countA) {
        return ItemStack::air();
    }
    bool needsSecond = !empty(offer.buyB) && offer.countB > 0;
    if (needsSecond && (!pays(offer.buyB, second) || second.mCount < offer.countB)) {
        return ItemStack::air();
    }
    if (consumption) {
        consumption->clear();
        consumption->emplace_back(FirstIngredient, offer.countA);
        if (needsSecond) {
            consumption->emplace_back(SecondIngredient, offer.countB);
        }
    }
    return offer.sell;
}

/**
 * Picks an offer the way the game does when one is clicked: whatever sits in
 * the trade slots goes back to the inventory, then the items the offer asks
 * for are moved in from the inventory, as far as there are enough.
 */
void InventoryModel::selectTrade(ItemStackRequest& request, int index)
{
    if (index < 0 || index >= int(trades.size())) {
        return;
    }
    selectedTrade = index;
    const TradeOffer& offer = trades[size_t(index)];
    for (int slot : { FirstIngredient, SecondIngredient }) {
        if (!empty(slots[slot])) {
            quickMove(request, slot);
        }
    }
    auto fill = [&](const ItemStack& price, int count, int target) {
        if (empty(price) || count <= 0 || !empty(slots[target])) {
            return;
        }
        int wanted = std::min(count, maxStack(price));
        for (int from = 0; from < 36 && wanted > 0; ++from) {
            if (pays(price, slots[from])) {
                wanted -= move(request, from, target, wanted);
            }
        }
    };
    fill(offer.buyA, offer.countA, FirstIngredient);
    fill(offer.buyB, offer.countB, SecondIngredient);
}

/**
 * Takes what the selected offer gives: the server is told which offer was
 * used, the price leaves the trade slots, and the result goes to the cursor
 * or straight into the inventory.
 */
void InventoryModel::takeTrade(ItemStackRequest& request, bool toInventory)
{
    std::vector<std::pair<int, int>> consumption;
    ItemStack output = tradePreview(&consumption);
    if (empty(output)) {
        return;
    }
    if (!toInventory && !empty(slots[inventory::Cursor])
        && (!same(slots[inventory::Cursor], output) || slots[inventory::Cursor].mCount + output.mCount > maxStack(output))) {
        return;
    }
    if (toInventory && inventoryRoom(output) < output.mCount) {
        return;
    }
    TradeOffer& offer = trades[size_t(selectedTrade)];
    ItemStackRequestAction craft;
    craft.mType = ItemStackRequestActionType::CraftRecipe;
    craft.mRecipeNetworkId = offer.netId;
    craft.mNumberOfRequestedCrafts = 1;
    request.mActions.push_back(craft);
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
    ++offer.uses;
}

}
