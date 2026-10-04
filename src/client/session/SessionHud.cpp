#include "SessionData.h"

#include "Network/BedrockConnection.h"
#include "Protocol/Packets/AnimatePacket.h"
#include "Protocol/Packets/BossEventPacket.h"
#include "Protocol/Packets/DeathInfoPacket.h"
#include "Protocol/Packets/InventoryContentPacket.h"
#include "Protocol/Packets/PlayerActionPacket.h"
#include "Protocol/Packets/RespawnPacket.h"
#include "Protocol/Packets/InventorySlotPacket.h"
#include "Protocol/Packets/MobEffectPacket.h"
#include "Protocol/Packets/MobEquipmentPacket.h"
#include "Protocol/Packets/PlayerHotbarPacket.h"
#include "Protocol/Packets/SetHealthPacket.h"
#include "Protocol/Packets/SetHudPacket.h"
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
 * The use duration in ticks a server item states in its components: the
 * use_modifiers use_duration in seconds, or the older use_duration in ticks.
 * 0 when it states none.
 */
int32_t componentUseTicks(const Tag& components)
{
    auto number = [](const Tag* tag) -> double {
        if (!tag) {
            return 0.0;
        }
        switch (tag->getType()) {
        case Tag::Type::Float:
            return tag->asFloat();
        case Tag::Type::Double:
            return tag->asDouble();
        case Tag::Type::Int:
            return tag->asInt();
        case Tag::Type::Short:
            return tag->asShort();
        case Tag::Type::Byte:
            return tag->asByte();
        default:
            return 0.0;
        }
    };
    double ticks = 0.0;
    const Tag* modifiers = itemComponent(components, "minecraft:use_modifiers");
    if (modifiers && modifiers->getType() == Tag::Type::Compound) {
        ticks = number(modifiers->get("use_duration")) * 20.0;
    }
    if (ticks < 1.0) {
        const Tag* legacy = itemComponent(components, "minecraft:use_duration");
        if (legacy && legacy->getType() == Tag::Type::Compound) {
            legacy = legacy->get("value");
        }
        ticks = number(legacy);
    }
    return std::isfinite(ticks) && ticks >= 1.0 ? static_cast<int32_t>(std::lround(ticks)) : 0;
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
    item.useTicks = componentUseTicks(components);
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
        if (const Tag* lore = display->get("Lore"); lore && lore->isList()) {
            for (const Tag& line : lore->getList()) {
                if (line.getType() == Tag::Type::String && item.lore.size() < 128) {
                    item.lore.push_back(line.asString());
                }
            }
        }
    }
    if (const Tag* enchantments = stack.mTag.get("ench"); enchantments && enchantments->isList()) {
        item.enchanted = !enchantments->getList().empty();
        for (const Tag& enchantment : enchantments->getList()) {
            if (!enchantment.isCompound() || item.enchantments.size() >= 128) {
                continue;
            }
            const Tag* id = enchantment.get("id");
            const Tag* level = enchantment.get("lvl");
            if (id && level && id->getType() == Tag::Type::Short && level->getType() == Tag::Type::Short) {
                item.enchantments.emplace_back(id->asShort(), level->asShort());
            }
        }
    }
    if (const Tag* map = stack.mTag.get("map_uuid"); map && map->getType() == Tag::Type::Long) {
        item.mapId = map->asLong();
    }
    if (const Tag* patterns = stack.mTag.get("Patterns"); patterns && patterns->isList()) {
        for (const auto& pattern : patterns->getList()) {
            if (pattern.isCompound() && item.bannerPatterns.size() < 16) {
                item.bannerPatterns.emplace_back(pattern.getString("Pattern", ""), pattern.getInt("Color", 0));
            }
        }
    }
    return item;
}

/**
 * Updates the local player's HUD state from inventory, equipment, attribute,
 * health, game mode, effect and air packets.
 */
/**
 * Keeps the boss bars the way the server describes them: a bar appears when
 * its boss is created and stays, in the order the bars came, until the
 * server removes it; later events change its fill, title or color.
 */
void Session::handleBossEvent(const BossEventPacket& event)
{
    using Action = BossEventPacket::Action;
    if (!std::isfinite(event.mHealthPercentage)) {
        return;
    }
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<BossBarView>& bars = current.hud.bossBars;
    auto bar = std::find_if(bars.begin(), bars.end(), [&](const BossBarView& view) {
        return view.bossId == event.mBossUniqueActorId;
    });
    switch (event.mAction) {
    case Action::Create:
        if (bar == bars.end()) {
            bars.push_back({ event.mBossUniqueActorId });
            bar = bars.end() - 1;
        }
        bar->title = event.mTitle;
        bar->progress = std::clamp(event.mHealthPercentage, 0.0f, 1.0f);
        bar->color = event.mColor;
        bar->overlay = event.mOverlay;
        break;
    case Action::Remove:
        if (bar != bars.end()) {
            bars.erase(bar);
        }
        break;
    case Action::UpdatePercentage:
        if (bar != bars.end()) {
            bar->progress = std::clamp(event.mHealthPercentage, 0.0f, 1.0f);
        }
        break;
    case Action::UpdateName:
        if (bar != bars.end()) {
            bar->title = event.mTitle;
        }
        break;
    case Action::UpdateProperties:
    case Action::UpdateStyle:
        if (bar != bars.end()) {
            bar->color = event.mColor;
            bar->overlay = event.mOverlay;
        }
        break;
    default:
        break;
    }
}

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
            // Equipment replaces the held stack when it names another item: fast
            // transfers may do so before the destination sends the rest of the
            // inventory. The count stays the inventory's, since servers echo the
            // held stack as it was when a placement began.
            int slot = inventoryModel.packetSlot(equipment->mContainerId, equipment->mInventorySlot);
            const ItemStack& held = slot >= 0 ? inventoryModel.slots[slot] : equipment->mItem;
            bool sameItem = held.isAir() == equipment->mItem.isAir()
                && (held.isAir() || held.mDefinition->getRuntimeId() == equipment->mItem.mDefinition->getRuntimeId());
            if (slot >= 0 && !sameItem) {
                inventoryModel.slots[slot] = equipment->mItem;
                if (inventoryBefore) (*inventoryBefore)[slot] = equipment->mItem;
                publishInventory();
            }
            if (current.hud.selectedSlot != equipment->mHotbarSlot) {
                current.hud.selectedSlot = equipment->mHotbarSlot;
                current.hud.selectedChanged = now;
            }
        }
    } else if (auto boss = std::dynamic_pointer_cast<BossEventPacket>(packet)) {
        handleBossEvent(*boss);
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
                if (hud.healthFeedback.change(hud.health, attribute.mValue, now, hud.statsKnown)) {
                    hud.lastHurt = now;
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
        if (current.hud.healthFeedback.change(current.hud.health, float(health->mHealth), now, current.hud.statsKnown, true)) {
            current.hud.lastHurt = now;
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
            transmit(action);
        }
    } else if (auto hud = std::dynamic_pointer_cast<SetHudPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        for (int32_t element : hud->mElements) {
            if (element < 0 || element >= 32) {
                continue;
            }
            if (hud->mVisibility == SetHudPacket::HudVisibility::Hide) {
                current.hud.hiddenElements |= 1u << element;
            } else {
                current.hud.hiddenElements &= ~(1u << element);
            }
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
    transmit(packet);
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
    transmit(packet);
}

}
