#include "client/session/SessionData.h"

#include "Protocol/Packets/InventoryTransactionPacket.h"
#include "Protocol/Packets/PlayerAuthInputPacket.h"
#include "client/DebugLog.h"
#include "world/ItemInfo.h"

#include <algorithm>

namespace kestrel {

namespace {

constexpr int32_t ReleaseItem = 0;
constexpr int16_t RiptideEnchantment = 30;
constexpr double TridentChargeSeconds = 0.5;

}

/**
 * Starts holding the item in use when it is one that charges while held, the
 * way a bow is drawn. Like the game, a bow only draws in creative or with an
 * arrow somewhere in the inventory or the off hand.
 */
bool Session::startItemUse(int32_t slot, const ItemStack& item)
{
    if (itemInUse || item.isAir()) {
        return false;
    }
    const std::string& identifier = item.mDefinition->getIdentifier();
    if (world::itemMaxUseTicks(identifier) <= 0) {
        return false;
    }
    bool creative = false;
    {
        std::lock_guard<std::mutex> guard(mutex);
        creative = current.gameMode == "Creative";
    }
    auto isArrow = [](const ItemStack& stack) {
        return !stack.isAir() && stack.mDefinition->getIdentifier() == "minecraft:arrow";
    };
    const auto& slots = inventoryModel.slots;
    bool armed = std::any_of(slots.begin(), slots.begin() + inventory::Armor, isArrow) || isArrow(slots[inventory::Offhand]);
    if (identifier == "minecraft:bow" && !creative && !armed) {
        return false;
    }
    itemInUse = ItemInUse { slot, identifier, false };
    debugLog("start using " + identifier);
    std::lock_guard<std::mutex> guard(mutex);
    current.hud.itemUseStarted = secondsNow();
    return true;
}

/**
 * Keeps the item in use while the button stays down. The first tick tells the
 * server the use started; letting go releases the item, which is when a bow
 * shoots, and switching away from it drops the use without releasing.
 */
void Session::tickItemUse(PlayerAuthInputPacket& packet)
{
    if (!itemInUse) {
        return;
    }
    int32_t selected = 0;
    {
        std::lock_guard<std::mutex> guard(mutex);
        selected = std::clamp(current.hud.selectedSlot, 0, 8);
    }
    const ItemStack& held = inventoryModel.slots[size_t(itemInUse->slot)];
    if (selected != itemInUse->slot || held.isAir() || held.mDefinition->getIdentifier() != itemInUse->identifier) {
        debugLog("stop using " + itemInUse->identifier);
        stopItemUse();
        return;
    }
    if (!itemInUse->announced) {
        packet.mInputData.push_back(static_cast<int32_t>(PlayerAuthInputData::StartUsingItem));
        itemInUse->announced = true;
    }
    if (useHeld.load()) {
        return;
    }
    InventoryTransactionPacket release;
    release.mTransactionType = InventoryTransactionType::ItemRelease;
    release.mActionType = ReleaseItem;
    release.mHotbarSlot = itemInUse->slot;
    release.mItemInHand = held;
    release.mHeadPosition = Vector3f(float(lookOrigin[0]), float(lookOrigin[1]), float(lookOrigin[2]));
    debugLog("release " + itemInUse->identifier);
    transmit(release);
    if (itemInUse->identifier == "minecraft:trident") {
        double started = 0.0;
        {
            std::lock_guard<std::mutex> guard(mutex);
            started = current.hud.itemUseStarted;
        }
        if (secondsNow() - started >= TridentChargeSeconds) {
            pendingRiptide = session::enchantmentLevel(held, RiptideEnchantment);
        }
    }
    stopItemUse();
}

void Session::stopItemUse()
{
    itemInUse.reset();
    std::lock_guard<std::mutex> guard(mutex);
    current.hud.itemUseStarted = 0.0;
}

}
