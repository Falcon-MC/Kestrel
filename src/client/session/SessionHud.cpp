#include "SessionData.h"

#include "Network/BedrockConnection.h"
#include "Protocol/Packets/AnimatePacket.h"
#include "Protocol/Packets/DeathInfoPacket.h"
#include "Protocol/Packets/InventoryContentPacket.h"
#include "Protocol/Packets/PlayerActionPacket.h"
#include "Protocol/Packets/RespawnPacket.h"
#include "Protocol/Packets/InventorySlotPacket.h"
#include "Protocol/Packets/MobEffectPacket.h"
#include "Protocol/Packets/MobEquipmentPacket.h"
#include "Protocol/Packets/PlayerHotbarPacket.h"
#include "Protocol/Packets/SetHealthPacket.h"
#include "Protocol/Packets/SetPlayerGameTypePacket.h"
#include "Protocol/Packets/UpdateAttributesPacket.h"
#include "client/DebugLog.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

constexpr int32_t AirDataId = 7;
constexpr int32_t MaxAirDataId = 42;
constexpr int32_t InventoryContainer = 0;
constexpr int32_t OffhandContainer = 119;
constexpr int32_t ArmorContainer = 120;

/**
 * The icon texture an item's registry components name under minecraft:icon,
 * either directly or as its default texture, searched at any depth.
 */
std::string componentIcon(const Tag& tag, int depth = 0)
{
    if (tag.getType() != Tag::Type::Compound || depth > 6) {
        return {};
    }
    if (const Tag* icon = tag.get("minecraft:icon")) {
        if (icon->getType() == Tag::Type::String) {
            return icon->asString();
        }
        if (icon->getType() == Tag::Type::Compound) {
            if (const Tag* texture = icon->get("texture"); texture && texture->getType() == Tag::Type::String) {
                return texture->asString();
            }
            const Tag* textures = icon->get("textures");
            if (textures && textures->getType() == Tag::Type::Compound) {
                if (const Tag* fallback = textures->get("default"); fallback && fallback->getType() == Tag::Type::String) {
                    return fallback->asString();
                }
            }
        }
    }
    for (const std::string& key : { std::string("components"), std::string("item_properties") }) {
        if (const Tag* child = tag.get(key)) {
            if (std::string found = componentIcon(*child, depth + 1); !found.empty()) {
                return found;
            }
        }
    }
    return {};
}

const Tag* itemComponent(const Tag& tag, const std::string& name, int depth = 0)
{
    if (tag.getType() != Tag::Type::Compound || depth > 6) return nullptr;
    if (const Tag* found = tag.get(name)) return found;
    for (const char* key : { "components", "item_properties" }) {
        if (const Tag* child = tag.get(key)) {
            if (const Tag* found = itemComponent(*child, name, depth + 1)) return found;
        }
    }
    return nullptr;
}

/**
 * The HUD view of a network stack: identifier, count, aux, the Damage tag of
 * tools and armor and the custom name under display.Name.
 */
}

HudItem hudItemOf(const ItemStack& stack)
{
    HudItem item;
    if (stack.isAir() || stack.mCount <= 0) {
        return item;
    }
    item.identifier = stack.mDefinition->getIdentifier();
    item.count = stack.mCount;
    item.aux = stack.mDamage;
    item.icon = componentIcon(stack.mDefinition->getComponentData());
    const std::string& id = item.identifier;
    item.handEquipped = id.ends_with("_sword") || id.ends_with("_pickaxe") || id.ends_with("_axe")
        || id.ends_with("_shovel") || id.ends_with("_hoe") || id == "minecraft:trident"
        || id == "minecraft:fishing_rod" || id == "minecraft:carrot_on_a_stick" || id == "minecraft:warped_fungus_on_a_stick";
    const Tag& components = stack.mDefinition->getComponentData();
    const Tag* hand = itemComponent(components, "minecraft:hand_equipped");
    if (!hand) hand = itemComponent(components, "hand_equipped");
    if (hand && hand->getType() == Tag::Type::Compound) hand = hand->get("value");
    if (hand && hand->getType() == Tag::Type::Byte) item.handEquipped = hand->asByte() != 0;
    if (stack.mTag.getType() != Tag::Type::Compound) {
        return item;
    }
    if (const Tag* damage = stack.mTag.get("Damage"); damage && damage->getType() == Tag::Type::Int) {
        item.damage = damage->asInt();
    }
    const Tag* display = stack.mTag.get("display");
    if (display && display->getType() == Tag::Type::Compound) {
        if (const Tag* name = display->get("Name"); name && name->getType() == Tag::Type::String) {
            item.customName = name->asString();
        }
    }
    return item;
}

/**
 * Updates the local player's HUD state from inventory, equipment, attribute,
 * health, game mode, effect and air packets.
 */
void Session::handleHudPacket(const std::shared_ptr<Packet>& packet)
{
    double now = secondsNow();
    if (auto animation = std::dynamic_pointer_cast<AnimatePacket>(packet)) {
        if (animation->mRuntimeActorId == localRuntimeId && animation->mAction == AnimatePacket::Action::SwingArm) {
            std::lock_guard<std::mutex> guard(mutex);
            current.hud.lastSwing = now;
        }
    } else if (auto equipment = std::dynamic_pointer_cast<MobEquipmentPacket>(packet)) {
        if (static_cast<uint64_t>(equipment->mRuntimeActorId) == localRuntimeId && equipment->mContainerId == InventoryContainer && equipment->mHotbarSlot >= 0 && equipment->mHotbarSlot < 9) {
            std::lock_guard<std::mutex> guard(mutex);
            // Equipment is authoritative too: fast transfers may replace the held
            // stack before the destination sends the rest of the inventory.
            int slot = inventoryModel.packetSlot(equipment->mContainerId, equipment->mInventorySlot);
            if (slot >= 0) {
                inventoryModel.slots[slot] = equipment->mItem;
                if (inventoryBefore) (*inventoryBefore)[slot] = equipment->mItem;
                publishInventory();
            }
            if (current.hud.selectedSlot != equipment->mHotbarSlot) {
                current.hud.selectedSlot = equipment->mHotbarSlot;
                current.hud.selectedChanged = now;
            }
        }
    } else if (auto hotbar = std::dynamic_pointer_cast<PlayerHotbarPacket>(packet)) {
        if (hotbar->mSelectHotbarSlot && hotbar->mSelectedHotbarSlot >= 0 && hotbar->mSelectedHotbarSlot < 9) {
            std::lock_guard<std::mutex> guard(mutex);
            current.hud.selectedSlot = hotbar->mSelectedHotbarSlot;
            current.hud.selectedChanged = now;
        }
    } else if (auto attributes = std::dynamic_pointer_cast<UpdateAttributesPacket>(packet)) {
        if (static_cast<uint64_t>(attributes->mRuntimeActorId) != localRuntimeId) {
            return;
        }
        std::lock_guard<std::mutex> guard(mutex);
        HudState& hud = current.hud;
        for (const AttributeData& attribute : attributes->mAttributes) {
            if (!std::isfinite(attribute.mValue)) {
                continue;
            }
            if (attribute.mName == "minecraft:health") {
                if (hud.statsKnown && attribute.mValue < hud.health) {
                    hud.lastHealthDrop = now;
                    if (now - hud.lastHurt > 0.1) hud.lastHurt = now;
                }
                hud.health = attribute.mValue;
                hud.maxHealth = std::isfinite(attribute.mMaximum) && attribute.mMaximum > 0.0f ? attribute.mMaximum : 20.0f;
                hud.statsKnown = true;
                current.dead = hud.health <= 0.0f;
            } else if (attribute.mName == "minecraft:player.hunger") {
                hud.hunger = attribute.mValue;
            } else if (attribute.mName == "minecraft:player.saturation") {
                hud.saturation = attribute.mValue;
            } else if (attribute.mName == "minecraft:absorption") {
                hud.absorption = attribute.mValue;
            } else if (attribute.mName == "minecraft:player.experience") {
                hud.experience = std::clamp(attribute.mValue, 0.0f, 1.0f);
            } else if (attribute.mName == "minecraft:player.level") {
                hud.level = static_cast<int32_t>(attribute.mValue);
            }
        }
    } else if (auto health = std::dynamic_pointer_cast<SetHealthPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        if (float(health->mHealth) < current.hud.health) {
            current.hud.lastHealthDrop = now;
            if (now - current.hud.lastHurt > 0.1) current.hud.lastHurt = now;
        }
        current.hud.health = float(health->mHealth);
        current.hud.statsKnown = true;
        current.dead = current.hud.health <= 0.0f;
    } else if (auto death = std::dynamic_pointer_cast<DeathInfoPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        current.dead = true;
        current.deathCause = death->mCauseAttackName;
        current.deathParameters = death->mMessageList;
    } else if (auto respawn = std::dynamic_pointer_cast<RespawnPacket>(packet)) {
        if (respawn->mState == RespawnPacket::State::ServerReady && respawnPending && connection) {
            respawnPending = false;
            PlayerActionPacket action;
            action.mRuntimeActorId = static_cast<int64_t>(localRuntimeId);
            action.mAction = PlayerActionType::Respawn;
            action.mFace = -1;
            connection->send(action);
        }
    } else if (auto mode = std::dynamic_pointer_cast<SetPlayerGameTypePacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        current.hud.gameType = mode->mGamemode;
    } else if (auto effect = std::dynamic_pointer_cast<MobEffectPacket>(packet)) {
        if (effect->mRuntimeActorId != localRuntimeId) {
            return;
        }
        std::lock_guard<std::mutex> guard(mutex);
        std::vector<HudEffect>& effects = current.hud.effects;
        std::erase_if(effects, [&](const HudEffect& entry) {
            return entry.id == effect->mEffectId;
        });
        if (effect->mEvent == MobEffectPacket::Event::Add || effect->mEvent == MobEffectPacket::Event::Modify) {
            HudEffect entry;
            entry.id = effect->mEffectId;
            entry.amplifier = effect->mAmplifier;
            entry.expires = effect->mDuration < 0 ? -1.0 : now + effect->mDuration / 20.0;
            entry.ambient = effect->mAmbient;
            effects.push_back(entry);
        }
    } else if (auto data = std::dynamic_pointer_cast<SetActorDataPacket>(packet)) {
        if (static_cast<uint64_t>(data->mRuntimeActorId) != localRuntimeId) {
            return;
        }
        std::lock_guard<std::mutex> guard(mutex);
        for (const EntityDataEntry& entry : data->mMetadata.mEntries) {
            if (entry.mId == AirDataId && entry.mFormat == EntityDataFormat::Short) {
                current.hud.air = entry.mShortValue;
            } else if (entry.mId == MaxAirDataId && entry.mFormat == EntityDataFormat::Short) {
                current.hud.maxAir = std::max<int32_t>(entry.mShortValue, 1);
            }
        }
    }
}

/**
 * Asks the network thread to hold another hotbar slot; the HUD shows it at
 * once and the server hears about it with the next loop.
 */
void Session::selectHotbarSlot(int slot)
{
    if (slot < 0 || slot > 8) {
        return;
    }
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (current.hud.selectedSlot == slot) {
            return;
        }
        current.hud.selectedSlot = slot;
        current.hud.selectedChanged = secondsNow();
    }
    requestedSlot = slot;
}

/**
 * Tells the server which hotbar slot the local player now holds, carrying the
 * stack in that slot.
 */
void Session::requestRespawn()
{
    respawnRequested = true;
}

/**
 * Tells the server the client is ready to respawn; the server answers with the
 * spawn point, and the respawn action follows once it is ready.
 */
void Session::sendRespawnRequest()
{
    if (!connection) {
        return;
    }
    RespawnPacket packet;
    packet.mPosition = { 0.0f, 0.0f, 0.0f };
    packet.mState = RespawnPacket::State::ClientReady;
    packet.mRuntimeActorId = localRuntimeId;
    connection->send(packet);
    respawnPending = true;
}

void Session::sendSelectedSlot(int slot)
{
    if (!connection || slot < 0 || slot > 8) {
        return;
    }
    MobEquipmentPacket packet;
    packet.mRuntimeActorId = static_cast<int64_t>(localRuntimeId);
    packet.mItem = inventoryModel.slots[size_t(slot)];
    packet.mInventorySlot = slot;
    packet.mHotbarSlot = slot;
    packet.mContainerId = InventoryContainer;
    connection->send(packet);
}

}
