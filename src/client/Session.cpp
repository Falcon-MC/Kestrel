#include "client/Session.h"

#include "Network/BedrockConnection.h"
#include "Network/Client/ClientNetworkSystem.h"
#include "Network/Session/RealmsService.h"
#include "client/DebugLog.h"
#include "Protocol/Packets/AddActorPacket.h"
#include "Protocol/Packets/AddPlayerPacket.h"
#include "Protocol/Packets/BlockActorDataPacket.h"
#include "Protocol/Packets/ChangeDimensionPacket.h"
#include "Protocol/Packets/ChunkRadiusUpdatedPacket.h"
#include "Protocol/Packets/GameRulesChangedPacket.h"
#include "Protocol/Packets/LevelEventPacket.h"
#include "Protocol/Packets/MoveActorAbsolutePacket.h"
#include "Protocol/Packets/MoveActorDeltaPacket.h"
#include "Protocol/Packets/PlayerListPacket.h"
#include "Protocol/Packets/RemoveActorPacket.h"
#include "Protocol/Packets/PlayStatusPacket.h"
#include "Protocol/Packets/InventoryContentPacket.h"
#include "Protocol/Packets/InventorySlotPacket.h"
#include "Protocol/Packets/ItemRegistryPacket.h"
#include "Protocol/Packets/MobEffectPacket.h"
#include "Protocol/Packets/MobEquipmentPacket.h"
#include "Protocol/Packets/PlayerHotbarPacket.h"
#include "Protocol/Packets/RequestChunkRadiusPacket.h"
#include "Protocol/Packets/SetHealthPacket.h"
#include "Protocol/Packets/SetPlayerGameTypePacket.h"
#include "Protocol/Packets/UpdateAttributesPacket.h"
#include "Protocol/Packets/SetActorDataPacket.h"
#include "Protocol/Packets/SetLocalPlayerAsInitializedPacket.h"
#include "Protocol/Packets/SetTimePacket.h"
#include "Protocol/Packets/LevelChunkPacket.h"
#include "Protocol/Packets/MovePlayerPacket.h"
#include "Protocol/Packets/NetworkChunkPublisherUpdatePacket.h"
#include "Protocol/Packets/StartGamePacket.h"
#include "Protocol/Packets/SubChunkPacket.h"
#include "Protocol/Packets/SubChunkRequestPacket.h"
#include "Protocol/Packets/UpdateBlockPacket.h"
#include "Protocol/Packets/UpdateSubChunkBlocksPacket.h"
#include "Protocol/Packets/CorrectPlayerMovePredictionPacket.h"
#include "Protocol/Packets/NetworkStackLatencyPacket.h"
#include "Protocol/Packets/PlayerAuthInputPacket.h"
#include "Protocol/Packets/RespawnPacket.h"
#include "Protocol/Packets/LevelSoundEventPacket.h"
#include "Protocol/Packets/PlaySoundPacket.h"
#include "Protocol/Packets/StopSoundPacket.h"
#include "Protocol/Packets/SetActorMotionPacket.h"
#include "Protocol/Packets/UpdateAbilitiesPacket.h"
#include "world/BlockCollisions.h"

#include "platform/Paths.h"
#include "ui/Font.h"
#include "ui/Image.h"
#include "world/ServerPack.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace kestrel {

double secondsNow()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double currentWorldTime(const SessionSnapshot& snapshot)
{
    if (!snapshot.daylightCycle) {
        return static_cast<double>(snapshot.worldTime);
    }
    return static_cast<double>(snapshot.worldTime) + (secondsNow() - snapshot.worldTimeStamp) * 20.0;
}

namespace {

constexpr int ProtocolVersion = 2193;
constexpr const char* GameVersion = "1.26.51";
constexpr const char* RealmPrefix = "realm_id/";
constexpr unsigned int TimeoutMs = 30000;
constexpr double PlayerEyeHeight = 1.62;
constexpr int32_t ScaleDataId = 38;
constexpr int32_t AirDataId = 7;
constexpr int32_t MaxAirDataId = 42;
constexpr int32_t InventoryContainer = 0;
constexpr int32_t OffhandContainer = 119;
constexpr int32_t ArmorContainer = 120;
constexpr float EyeHeight = 1.62f;
constexpr double TickSeconds = 0.05;
constexpr int32_t FlagsDataId = 0;
constexpr int SprintingFlag = 3;
constexpr int NoAiFlag = 16;
constexpr int HasGravityFlag = 49;
constexpr int32_t JumpBoostEffect = 8;
constexpr int32_t LevitationEffect = 24;
constexpr int32_t SlowFallingEffect = 27;
constexpr int32_t WeavingEffect = 33;
constexpr uint16_t BaseAbilityLayer = 1;
constexpr uint32_t FlyingAbility = 1u << 9;
constexpr uint32_t MayFlyAbility = 1u << 10;
constexpr uint32_t NoClipAbility = 1u << 17;

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

/**
 * The HUD view of a network stack: identifier, count, aux, the Damage tag of
 * tools and armor and the custom name under display.Name.
 */
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
 * The entity scale carried by a metadata update, or fallback when the update
 * does not set it.
 */
float metadataScale(const EntityDataMap& metadata, float fallback)
{
    for (const EntityDataEntry& entry : metadata.mEntries) {
        if (entry.mId == ScaleDataId && entry.mFormat == EntityDataFormat::Float && entry.mFloatValue > 0.0f) {
            return entry.mFloatValue;
        }
    }
    return fallback;
}

/**
 * Copies the entity state a metadata update carries (flags, variants, color,
 * skin id) into the actor, keeping every value the update leaves out.
 */
void applyActorMetadata(const EntityDataMap& metadata, ActorView& actor)
{
    for (const EntityDataEntry& entry : metadata.mEntries) {
        switch (entry.mId) {
        case 0:
            actor.flags[0] = static_cast<uint64_t>(entry.mLongValue);
            break;
        case 92:
            actor.flags[1] = static_cast<uint64_t>(entry.mLongValue);
            break;
        case 2:
            actor.variant = entry.mIntValue;
            break;
        case 43:
            actor.markVariant = entry.mIntValue;
            break;
        case 3:
            actor.color = static_cast<uint8_t>(entry.mByteValue);
            break;
        case 104:
            actor.skinId = entry.mIntValue;
            break;
        case 4:
            actor.name = entry.mStringValue;
            break;
        default:
            break;
        }
    }
}

const char* gameModeName(GameType type)
{
    switch (type) {
    case GameType::Survival:
        return "Survival";
    case GameType::Creative:
        return "Creative";
    case GameType::Adventure:
        return "Adventure";
    case GameType::Spectator:
        return "Spectator";
    case GameType::SurvivalViewer:
    case GameType::CreativeViewer:
        return "Viewer";
    case GameType::Default:
        break;
    }
    return "Default";
}

int32_t floorChunk(float blockCoordinate)
{
    return static_cast<int32_t>(std::floor(blockCoordinate / 16.0f));
}

std::filesystem::path packDirectory()
{
    return platform::dataDirectory() / "packs";
}

std::filesystem::path packPath(const std::string& id, const std::string& version)
{
    return packDirectory() / (id + "_" + version + ".zip");
}

void savePack(const DownloadedResourcePack& pack)
{
    std::error_code error;
    std::filesystem::create_directories(packDirectory(), error);
    std::filesystem::path path = packPath(pack.mOffer.mPackId, pack.mOffer.mPackVersion);
    std::ofstream archive(path, std::ios::binary | std::ios::trunc);
    archive.write(pack.mData.data(), static_cast<std::streamsize>(pack.mData.size()));
    if (!pack.mOffer.mContentKey.empty()) {
        std::ofstream key(std::filesystem::path(path).replace_extension(".key"), std::ios::trunc);
        key << pack.mOffer.mContentKey;
    }
}

std::shared_ptr<const std::vector<uint8_t>> packTitle(const world::PackFiles& pack)
{
    const std::string* encoded = pack.find("textures/ui/title.png");
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    if (!encoded || !ui::decodeImage(*encoded, width, height, rgba) || width == 0 || height == 0) {
        return nullptr;
    }
    constexpr uint32_t SlotWidth = ui::Font::TitleWidth;
    constexpr uint32_t SlotHeight = ui::Font::TitleHeight;
    float fit = std::min(float(SlotWidth) / float(width), float(SlotHeight) / float(height));
    uint32_t drawnWidth = std::max<uint32_t>(1, uint32_t(float(width) * fit));
    uint32_t drawnHeight = std::max<uint32_t>(1, uint32_t(float(height) * fit));
    uint32_t left = (SlotWidth - drawnWidth) / 2;
    uint32_t top = (SlotHeight - drawnHeight) / 2;
    auto slot = std::make_shared<std::vector<uint8_t>>(size_t(SlotWidth) * SlotHeight * 4, 0);
    for (uint32_t y = 0; y < drawnHeight; ++y) {
        for (uint32_t x = 0; x < drawnWidth; ++x) {
            uint32_t sx0 = x * width / drawnWidth;
            uint32_t sx1 = std::max(sx0 + 1, (x + 1) * width / drawnWidth);
            uint32_t sy0 = y * height / drawnHeight;
            uint32_t sy1 = std::max(sy0 + 1, (y + 1) * height / drawnHeight);
            uint32_t sum[4] = {};
            uint32_t count = 0;
            for (uint32_t sy = sy0; sy < sy1; ++sy) {
                for (uint32_t sx = sx0; sx < sx1; ++sx) {
                    const uint8_t* texel = rgba.data() + (size_t(sy) * width + sx) * 4;
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += texel[c];
                    }
                    ++count;
                }
            }
            uint8_t* out = slot->data() + (size_t(top + y) * SlotWidth + left + x) * 4;
            for (int c = 0; c < 4; ++c) {
                out[c] = static_cast<uint8_t>(sum[c] / count);
            }
        }
    }
    return slot;
}

bool parseHostPort(const std::string& address, std::string& host, unsigned short& port)
{
    size_t colon = address.rfind(':');
    host = colon == std::string::npos ? address : address.substr(0, colon);
    long parsed = colon == std::string::npos ? 19132 : std::strtol(address.c_str() + colon + 1, nullptr, 10);
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }
    if (host.empty() || parsed <= 0 || parsed > 65535) {
        return false;
    }
    port = static_cast<unsigned short>(parsed);
    return true;
}

}

Session::Session() = default;

Session::~Session()
{
    disconnect();
}

void Session::connect(std::string name, std::string target, MinecraftAuthentication* authentication, std::string offlineName)
{
    disconnect();
    cancelled = false;
    {
        std::lock_guard<std::mutex> guard(mutex);
        current = SessionSnapshot {};
        current.state = target.rfind(RealmPrefix, 0) == 0 ? SessionState::Resolving : SessionState::Connecting;
        current.name = std::move(name);
        current.target = target;
    }
    worker = std::thread([this, target = std::move(target), authentication, offlineName = std::move(offlineName)]() mutable {
        run(std::move(target), authentication, std::move(offlineName));
    });
}

void Session::disconnect()
{
    cancelled = true;
    if (worker.joinable()) {
        worker.join();
    }
    std::lock_guard<std::mutex> guard(mutex);
    if (current.state == SessionState::Joined) {
        current.state = SessionState::Idle;
    }
}

SessionSnapshot Session::snapshot() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return current;
}

std::vector<SkinUpload> Session::takeSkinUploads()
{
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<SkinUpload> uploads = std::move(pendingSkins);
    pendingSkins.clear();
    return uploads;
}

/**
 * Places an entity at a network position. Players are sent at eye height, so
 * their feet sit one eye height lower. Every move counts as a new sample for
 * the renderer to glide toward, and a teleport tells it to jump instead.
 */
void Session::moveActor(uint64_t runtimeId, double x, double y, double z, float yaw, float headYaw, float pitch, bool teleport, bool onGround)
{
    auto actor = actors.find(runtimeId);
    if (actor == actors.end()) {
        return;
    }
    ++actor->second.moves;
    if (teleport) {
        ++actor->second.teleports;
    }
    actor->second.onGround = onGround;
    actor->second.x = x;
    actor->second.y = y - (actor->second.identifier == "minecraft:player" ? PlayerEyeHeight : 0.0);
    actor->second.z = z;
    actor->second.yaw = yaw;
    actor->second.headYaw = headYaw;
    actor->second.pitch = pitch;
}

/**
 * Updates the local player's HUD state from inventory, equipment, attribute,
 * health, game mode, effect and air packets.
 */
void Session::handleHudPacket(const std::shared_ptr<Packet>& packet)
{
    double now = secondsNow();
    auto slotOf = [&](int32_t container, int32_t slot) -> HudItem* {
        HudState& hud = current.hud;
        if (container == InventoryContainer && slot >= 0 && slot < int32_t(hud.inventory.size())) {
            return &hud.inventory[size_t(slot)];
        }
        if (container == ArmorContainer && slot >= 0 && slot < int32_t(hud.armor.size())) {
            return &hud.armor[size_t(slot)];
        }
        if (container == OffhandContainer && slot == 0) {
            return &hud.offhand;
        }
        return nullptr;
    };
    if (auto content = std::dynamic_pointer_cast<InventoryContentPacket>(packet)) {
        debugLog("inventory content container " + std::to_string(content->mContainerId) + ", " + std::to_string(content->mContents.size()) + " slots");
        std::lock_guard<std::mutex> guard(mutex);
        for (size_t slot = 0; slot < content->mContents.size(); ++slot) {
            if (HudItem* target = slotOf(content->mContainerId, int32_t(slot))) {
                *target = hudItemOf(content->mContents[slot]);
            }
            if (content->mContainerId == InventoryContainer && slot < inventoryStacks.size()) {
                inventoryStacks[slot] = content->mContents[slot];
            }
        }
    } else if (auto single = std::dynamic_pointer_cast<InventorySlotPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        if (HudItem* target = slotOf(single->mContainerId, single->mSlot)) {
            *target = hudItemOf(single->mItem);
        }
        if (single->mContainerId == InventoryContainer && single->mSlot >= 0 && size_t(single->mSlot) < inventoryStacks.size()) {
            inventoryStacks[size_t(single->mSlot)] = single->mItem;
        }
    } else if (auto equipment = std::dynamic_pointer_cast<MobEquipmentPacket>(packet)) {
        if (static_cast<uint64_t>(equipment->mRuntimeActorId) == localRuntimeId && equipment->mContainerId == InventoryContainer && equipment->mHotbarSlot >= 0 && equipment->mHotbarSlot < 9) {
            std::lock_guard<std::mutex> guard(mutex);
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
                }
                hud.health = attribute.mValue;
                hud.maxHealth = std::isfinite(attribute.mMaximum) && attribute.mMaximum > 0.0f ? attribute.mMaximum : 20.0f;
                hud.statsKnown = true;
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
        }
        current.hud.health = float(health->mHealth);
        current.hud.statsKnown = true;
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
void Session::sendSelectedSlot(int slot)
{
    if (!connection || slot < 0 || slot > 8) {
        return;
    }
    MobEquipmentPacket packet;
    packet.mRuntimeActorId = static_cast<int64_t>(localRuntimeId);
    packet.mItem = inventoryStacks[size_t(slot)];
    packet.mInventorySlot = slot;
    packet.mHotbarSlot = slot;
    packet.mContainerId = InventoryContainer;
    connection->send(packet);
}

std::vector<SoundRequest> Session::takeSounds()
{
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<SoundRequest> sounds = std::move(pendingSounds);
    pendingSounds.clear();
    return sounds;
}

void Session::queueSound(SoundRequest request)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (pendingSounds.size() < 256) {
        pendingSounds.push_back(std::move(request));
    }
}

std::string Session::blockNameAt(int32_t x, int32_t y, int32_t z)
{
    if (!assets) {
        return {};
    }
    std::shared_ptr<const world::SubChunk> sub = world.store().subChunk({ motionDimension, x >> 4, y >> 4, z >> 4 });
    if (!sub) {
        return {};
    }
    uint32_t value = sub->runtimeId(0, uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15));
    return value == world::ImplicitAir ? std::string() : assets->blockName(value, ids.hashed, ids.sequential.get());
}

/**
 * Turns the sound packets into sound requests: level sound events with the
 * block their extra data names, named sounds, stops, and the level events
 * that only carry a sound.
 */
void Session::handleSoundPacket(const std::shared_ptr<Packet>& packet)
{
    if (auto event = std::dynamic_pointer_cast<LevelSoundEventPacket>(packet)) {
        SoundRequest request;
        request.name = event->mSound;
        Vector3f at = event->mHasFirePosition ? event->mFirePosition : event->mPosition;
        request.position = { at.x, at.y, at.z };
        request.actor = event->mActorType;
        request.baby = event->mIsBabyMob;
        request.global = event->mDisableRelativeVolume;
        if (event->mExtraData >= 0 && assets) {
            std::string name = assets->blockName(static_cast<uint32_t>(event->mExtraData), ids.hashed, ids.sequential.get());
            if (name != "minecraft:air") {
                request.block = std::move(name);
            }
        }
        queueSound(std::move(request));
    } else if (auto play = std::dynamic_pointer_cast<PlaySoundPacket>(packet)) {
        SoundRequest request;
        request.kind = SoundRequest::Kind::Named;
        request.name = play->mSound;
        request.position = { play->mPosition.x, play->mPosition.y, play->mPosition.z };
        request.volume = play->mVolume;
        request.pitch = play->mPitch;
        request.global = play->mBypassListenerRangeCheck;
        queueSound(std::move(request));
    } else if (auto stop = std::dynamic_pointer_cast<StopSoundPacket>(packet)) {
        SoundRequest request;
        request.kind = stop->mStoppingAllSound ? SoundRequest::Kind::StopAll : SoundRequest::Kind::Stop;
        request.name = stop->mSoundName;
        queueSound(std::move(request));
    } else if (auto level = std::dynamic_pointer_cast<LevelEventPacket>(packet)) {
        struct LevelSound {
            int32_t event;
            const char* sound;
            float pitch;
        };
        static constexpr LevelSound Sounds[] = {
            { 1000, "random.click", 1.0f },
            { 1001, "random.click", 1.2f },
            { 1002, "random.bow", 1.2f },
            { 1003, "random.door_open", 1.0f },
            { 1004, "random.fizz", 1.0f },
            { 1005, "random.fuse", 1.0f },
            { 1007, "mob.ghast.charge", 1.0f },
            { 1008, "mob.ghast.fireball", 1.0f },
            { 1009, "mob.ghast.fireball", 1.0f },
            { 1010, "mob.zombie.wood", 1.0f },
            { 1012, "mob.zombie.woodbreak", 1.0f },
            { 1016, "mob.zombie.remedy", 1.0f },
            { 1017, "mob.zombie.unfect", 1.0f },
            { 1018, "mob.endermen.portal", 1.0f },
            { 1020, "random.anvil_break", 1.0f },
            { 1021, "random.anvil_use", 1.0f },
            { 1022, "random.anvil_land", 1.0f },
        };
        for (const LevelSound& sound : Sounds) {
            if (sound.event != level->mEventId) {
                continue;
            }
            SoundRequest request;
            request.kind = SoundRequest::Kind::Named;
            request.name = sound.sound;
            request.position = { level->mPosition.x, level->mPosition.y, level->mPosition.z };
            request.pitch = sound.pitch;
            queueSound(std::move(request));
        }
    }
}

/**
 * The sounds the local player makes itself, which the server does not send
 * back: a step each time it walks far enough on the ground, a jump and a
 * landing, each from the block under its feet.
 */
void Session::playMotionSounds(const MotionTick& tick, const MotionVector& before)
{
    auto under = [&](const MotionVector& feet) {
        int32_t x = static_cast<int32_t>(std::floor(feet.x));
        int32_t y = static_cast<int32_t>(std::floor(feet.y - 0.2f));
        int32_t z = static_cast<int32_t>(std::floor(feet.z));
        std::string name = blockNameAt(x, y, z);
        if (name.empty()) {
            name = blockNameAt(x, y - 1, z);
        }
        return name;
    };
    auto emit = [&](const char* event, const MotionVector& feet) {
        SoundRequest request;
        request.name = event;
        request.position = { feet.x, feet.y, feet.z };
        request.actor = "minecraft:player";
        request.block = under(feet);
        if (!request.block.empty()) {
            queueSound(std::move(request));
        }
    };

    if (tick.flying) {
        wasOnGround = false;
        fallStartY = tick.position.y;
        return;
    }
    if (tick.startedJump) {
        emit("jump", before);
    }
    if (tick.onGround && !wasOnGround) {
        if (fallStartY - tick.position.y > 0.6f) {
            emit("land", tick.position);
        }
        nextStepDistance = walkedDistance + 1.0f;
    }
    if (!tick.onGround) {
        fallStartY = wasOnGround ? before.y : std::max(fallStartY, tick.position.y);
    }
    wasOnGround = tick.onGround;

    float dx = tick.position.x - before.x;
    float dz = tick.position.z - before.z;
    walkedDistance += std::sqrt(dx * dx + dz * dz) * 0.6f;
    if (tick.onGround && !tick.sneaking && walkedDistance > nextStepDistance) {
        nextStepDistance = walkedDistance + 1.0f;
        emit("step", tick.position);
    }
}

void Session::setMotionInput(const MotionInput& input)
{
    std::lock_guard<std::mutex> guard(mutex);
    motionInput = input;
}

/**
 * Both block layers at a position as collision states: null for air or an
 * unloaded sub-chunk, a full cube for a block state the collision table does
 * not know.
 */
MotionCell Session::motionCell(int32_t x, int32_t y, int32_t z)
{
    MotionCell cell;
    if (!assets) {
        return cell;
    }
    std::shared_ptr<const world::SubChunk> sub = world.store().subChunk({ motionDimension, x >> 4, y >> 4, z >> 4 });
    if (!sub) {
        return cell;
    }
    const world::BlockCollisions& table = world::BlockCollisions::shared();
    auto resolve = [&](uint32_t layer) -> const world::CollisionState* {
        uint32_t value = sub->runtimeId(layer, uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15));
        if (value == world::ImplicitAir) {
            return nullptr;
        }
        if (const world::CollisionState* state = table.find(assets->stateHash(value, ids.hashed, ids.sequential.get()))) {
            return state;
        }
        const world::BlockVisual& visual = assets->visual(value, ids.hashed, ids.sequential.get());
        return visual.flags & world::FlagAir ? nullptr : table.fullBlock();
    };
    cell.primary = resolve(0);
    cell.extra = resolve(1);
    return cell;
}

bool Session::motionAreaLoaded(const MotionVector& feet)
{
    int32_t x = static_cast<int32_t>(std::floor(feet.x));
    int32_t z = static_cast<int32_t>(std::floor(feet.z));
    return world.store().isLoaded({ motionDimension, x >> 4, z >> 4 });
}

/**
 * Feeds the local player's movement state from the packets the server sends
 * about it: teleports and corrections, knockback, speed, sprint and gravity
 * flags, abilities, and the latency probes whose answers tell the server
 * which of those the client has already applied.
 */
void Session::handleMotionPacket(const std::shared_ptr<Packet>& packet)
{
    if (auto latency = std::dynamic_pointer_cast<NetworkStackLatencyPacket>(packet)) {
        if (latency->mFromServer && connection) {
            NetworkStackLatencyPacket answer;
            answer.mTimestamp = latency->mTimestamp;
            answer.mFromServer = false;
            connection->send(answer);
            connection->flush();
        }
    } else if (auto move = std::dynamic_pointer_cast<MovePlayerPacket>(packet)) {
        if (static_cast<uint64_t>(move->mRuntimeActorId) != localRuntimeId) {
            return;
        }
        MotionVector was = motion.position();
        motion.teleport({ move->mPosition.x, move->mPosition.y - EyeHeight, move->mPosition.z });
        teleportHandled = true;
        debugLog("server moved player at tick " + std::to_string(clientTick) + " mode " + std::to_string(static_cast<int>(move->mMode)) + " from " + std::to_string(was.x) + " " + std::to_string(was.y) + " " + std::to_string(was.z)
            + " to " + std::to_string(move->mPosition.x) + " " + std::to_string(move->mPosition.y - EyeHeight) + " " + std::to_string(move->mPosition.z));
        std::lock_guard<std::mutex> guard(mutex);
        MotionVector feet = motion.position();
        current.player.previous = { feet.x, feet.y, feet.z };
        current.player.current = current.player.previous;
        current.player.tickTime = secondsNow();
        ++current.player.teleports;
    } else if (auto respawn = std::dynamic_pointer_cast<RespawnPacket>(packet)) {
        if (respawn->mState != RespawnPacket::State::ServerReady) {
            return;
        }
        MotionVector feet { respawn->mPosition.x, respawn->mPosition.y - EyeHeight, respawn->mPosition.z };
        motion.reset(feet);
        debugLog("respawn at " + std::to_string(feet.x) + " " + std::to_string(feet.y) + " " + std::to_string(feet.z));
        if (connection) {
            RespawnPacket ready;
            ready.mPosition = respawn->mPosition;
            ready.mState = RespawnPacket::State::ClientReady;
            ready.mRuntimeActorId = localRuntimeId;
            connection->send(ready);
            connection->flush();
        }
        std::lock_guard<std::mutex> guard(mutex);
        current.player.previous = { feet.x, feet.y, feet.z };
        current.player.current = current.player.previous;
        current.player.tickTime = secondsNow();
        ++current.player.teleports;
        current.spawnX = respawn->mPosition.x;
        current.spawnY = respawn->mPosition.y;
        current.spawnZ = respawn->mPosition.z;
        ++current.teleportCount;
    } else if (auto correction = std::dynamic_pointer_cast<CorrectPlayerMovePredictionPacket>(packet)) {
        if (correction->mPredictionType != PredictionType::Player) {
            return;
        }
        debugLog("server corrected movement at tick " + std::to_string(clientTick) + " for tick " + std::to_string(correction->mTick));
        motion.correct({ correction->mPosition.x, correction->mPosition.y - EyeHeight, correction->mPosition.z }, { correction->mDelta.x, correction->mDelta.y, correction->mDelta.z }, correction->mOnGround);
    } else if (auto push = std::dynamic_pointer_cast<SetActorMotionPacket>(packet)) {
        if (push->mRuntimeActorId == localRuntimeId) {
            motion.knockback({ push->mMotion.x, push->mMotion.y, push->mMotion.z });
        }
    } else if (auto attributes = std::dynamic_pointer_cast<UpdateAttributesPacket>(packet)) {
        if (static_cast<uint64_t>(attributes->mRuntimeActorId) != localRuntimeId) {
            return;
        }
        for (const AttributeData& attribute : attributes->mAttributes) {
            if (attribute.mName == "minecraft:movement") {
                motion.setMovementSpeed(attribute.mValue, attribute.mDefaultValue);
            }
        }
    } else if (auto data = std::dynamic_pointer_cast<SetActorDataPacket>(packet)) {
        if (static_cast<uint64_t>(data->mRuntimeActorId) != localRuntimeId) {
            return;
        }
        for (const EntityDataEntry& entry : data->mMetadata.mEntries) {
            if (entry.mId == FlagsDataId) {
                uint64_t flags = static_cast<uint64_t>(entry.mLongValue);
                motion.setServerSprint((flags >> SprintingFlag) & 1);
                motion.setImmobile((flags >> NoAiFlag) & 1);
                motion.setGravity((flags >> HasGravityFlag) & 1);
            } else if (entry.mId == ScaleDataId && entry.mFormat == EntityDataFormat::Float) {
                motion.setScale(entry.mFloatValue);
            }
        }
    } else if (auto abilities = std::dynamic_pointer_cast<UpdateAbilitiesPacket>(packet)) {
        for (const AbilityLayer& layer : abilities->mAbilities.mAbilityLayers) {
            if (layer.mLayerType != BaseAbilityLayer) {
                continue;
            }
            motion.setAbilities(layer.mAbilityValues & MayFlyAbility, layer.mAbilityValues & FlyingAbility, layer.mAbilityValues & NoClipAbility, layer.mFlySpeed, layer.mVerticalFlySpeed);
        }
    } else if (auto mode = std::dynamic_pointer_cast<SetPlayerGameTypePacket>(packet)) {
        motion.setGameType(mode->mGamemode);
    }
}

/**
 * Runs one movement tick every 50 ms once the player has spawned and the
 * ground under it has loaded, and sends the server that tick as a
 * PlayerAuthInput: eye position, rotation, the movement vector, the velocity
 * after the tick and the input flags the server replays it with.
 */
void Session::tickMotion()
{
    if (!connection || !spawnInitialized || !assets) {
        return;
    }
    double now = secondsNow();
    if (nextMotionTick == 0.0) {
        nextMotionTick = now;
    }
    if (now < nextMotionTick) {
        return;
    }
    nextMotionTick += TickSeconds;
    if (now - nextMotionTick > TickSeconds * 5.0) {
        nextMotionTick = now + TickSeconds;
    }

    MotionInput input;
    {
        std::lock_guard<std::mutex> guard(mutex);
        input = motionInput;
        int32_t jumpBoost = 0;
        int32_t levitation = 0;
        bool slowFalling = false;
        bool weaving = false;
        for (const HudEffect& effect : current.hud.effects) {
            if (effect.expires >= 0.0 && effect.expires < now) {
                continue;
            }
            if (effect.id == JumpBoostEffect) {
                jumpBoost = effect.amplifier + 1;
            } else if (effect.id == LevitationEffect) {
                levitation = effect.amplifier + 1;
            } else if (effect.id == SlowFallingEffect) {
                slowFalling = true;
            } else if (effect.id == WeavingEffect) {
                weaving = true;
            }
        }
        motion.setEffects(jumpBoost, levitation, slowFalling, weaving);
        motion.setHunger(current.hud.hunger);
    }

    if (!motionStarted) {
        if (!world.cohortLoaded() || world.stats().pendingSubChunks > 0 || !motionAreaLoaded(motion.position())) {
            return;
        }
        motionStarted = true;
        MotionVector start = motion.position();
        debugLog("movement started at " + std::to_string(start.x) + " " + std::to_string(start.y) + " " + std::to_string(start.z));
    }

    MotionVector before = motion.position();
    MotionTick tick;
    if (motionAreaLoaded(before)) {
        PlayerMotion::CellLookup lookup = [this](int32_t x, int32_t y, int32_t z) {
            return motionCell(x, y, z);
        };
        tick = motion.step(input, lookup);
        playMotionSounds(tick, before);
    } else {
        tick.position = before;
        tick.sneaking = motion.sneaking();
    }

    float yaw = std::remainder(input.yaw, 360.0f);
    float pitch = std::clamp(input.pitch, -90.0f, 90.0f);
    PlayerAuthInputPacket packet;
    float eyeY = tick.position.y + EyeHeight;
    packet.mPosition = Vector3f(tick.position.x, eyeY, tick.position.z);
    packet.mRotation = Vector3f(pitch, yaw, yaw);
    packet.mMotionX = input.sideways;
    packet.mMotionY = input.forward;
    packet.mAnalogMoveVectorX = input.sideways;
    packet.mAnalogMoveVectorY = input.forward;
    packet.mRawMoveVectorX = input.sideways;
    packet.mRawMoveVectorY = input.forward;
    packet.mDelta = Vector3f(tick.velocity.x, tick.velocity.y, tick.velocity.z);
    packet.mTick = static_cast<int64_t>(++clientTick);
    packet.mInputMode = PlayerInputMode::Mouse;
    packet.mPlayMode = PlayerClientPlayMode::Normal;
    packet.mInputInteractionModel = PlayerInputInteractionModel::Crosshair;
    packet.mInteractRotationX = pitch;
    packet.mInteractRotationY = yaw;
    constexpr float Radians = 3.14159265f / 180.0f;
    packet.mCameraOrientation = Vector3f(-std::sin(yaw * Radians) * std::cos(pitch * Radians), -std::sin(pitch * Radians), std::cos(yaw * Radians) * std::cos(pitch * Radians));

    auto flag = [&](PlayerAuthInputData value) {
        packet.mInputData.push_back(static_cast<int32_t>(value));
    };
    if (input.forward > 0.0f) {
        flag(PlayerAuthInputData::Up);
    }
    if (input.forward < 0.0f) {
        flag(PlayerAuthInputData::Down);
    }
    if (input.sideways > 0.0f) {
        flag(PlayerAuthInputData::Left);
    }
    if (input.sideways < 0.0f) {
        flag(PlayerAuthInputData::Right);
    }
    if (input.jump) {
        flag(PlayerAuthInputData::JumpDown);
        flag(PlayerAuthInputData::Jumping);
        flag(PlayerAuthInputData::JumpCurrentRaw);
        if (!lastMotionInput.jump) {
            flag(PlayerAuthInputData::JumpPressedRaw);
        }
    } else if (lastMotionInput.jump) {
        flag(PlayerAuthInputData::JumpReleasedRaw);
    }
    if (tick.startedJump) {
        flag(PlayerAuthInputData::StartJumping);
    }
    if (input.sneak) {
        flag(PlayerAuthInputData::Sneaking);
        flag(PlayerAuthInputData::SneakCurrentRaw);
        if (!lastMotionInput.sneak) {
            flag(PlayerAuthInputData::SneakPressedRaw);
        }
    } else if (lastMotionInput.sneak) {
        flag(PlayerAuthInputData::SneakReleasedRaw);
    }
    if (tick.sneaking) {
        flag(PlayerAuthInputData::SneakDown);
    }
    if (tick.startSneaking) {
        flag(PlayerAuthInputData::StartSneaking);
    }
    if (tick.stopSneaking) {
        flag(PlayerAuthInputData::StopSneaking);
    }
    if (input.sprint) {
        flag(PlayerAuthInputData::SprintDown);
    }
    if (tick.sprinting) {
        flag(PlayerAuthInputData::Sprinting);
    }
    if (tick.startSprinting) {
        flag(PlayerAuthInputData::StartSprinting);
    }
    if (tick.stopSprinting) {
        flag(PlayerAuthInputData::StopSprinting);
    }
    if (tick.startFlying) {
        flag(PlayerAuthInputData::StartFlying);
    }
    if (tick.stopFlying) {
        flag(PlayerAuthInputData::StopFlying);
    }
    if (tick.horizontalCollision) {
        flag(PlayerAuthInputData::HorizontalCollision);
    }
    if (tick.verticalCollision) {
        flag(PlayerAuthInputData::VerticalCollision);
    }
    if (teleportHandled) {
        flag(PlayerAuthInputData::HandleTeleport);
        teleportHandled = false;
    }
    flag(PlayerAuthInputData::BlockBreakingDelayEnabled);
    connection->send(packet);
    connection->flush();
    lastMotionInput = input;

    motion.anchor({ tick.position.x, eyeY - EyeHeight, tick.position.z });
    MotionVector feet = motion.position();
    if (clientTick % 20 == 0) {
        char line[192];
        std::snprintf(line, sizeof(line), "tick %llu feet %.4f %.4f %.4f velocity %.4f %.4f %.4f ground %d jump %d sprint %d sneak %d", static_cast<unsigned long long>(clientTick), feet.x, feet.y, feet.z,
            tick.velocity.x, tick.velocity.y, tick.velocity.z, tick.onGround ? 1 : 0, tick.startedJump ? 1 : 0, tick.sprinting ? 1 : 0, tick.sneaking ? 1 : 0);
        debugLog(line);
    }
    std::lock_guard<std::mutex> guard(mutex);
    current.player.active = true;
    current.player.previous = current.player.current;
    current.player.current = { feet.x, feet.y, feet.z };
    current.player.tickTime = now;
    current.player.sneaking = tick.sneaking;
    current.player.sprinting = tick.sprinting;
    current.player.flying = tick.flying;
    current.player.movementSpeed = motion.speed();
}

/**
 * Keeps a player's skin in a free skin slot, scaled to the entity texture
 * size, and points that player's entity at it.
 */
void Session::storeSkin(const std::string& uuid, const SerializedSkin& skin)
{
    const SkinImageData& image = skin.mSkinData;
    if (image.mWidth <= 0 || image.mHeight <= 0 || image.mData.size() < size_t(image.mWidth) * size_t(image.mHeight) * 4) {
        return;
    }
    uint32_t slot = NoSkin;
    if (auto existing = skinByUuid.find(uuid); existing != skinByUuid.end()) {
        slot = existing->second.first;
    } else {
        for (uint32_t candidate = 0; candidate < world::SkinSlots; ++candidate) {
            if (slotOwners[candidate].empty()) {
                slot = candidate;
                break;
            }
        }
    }
    if (slot == NoSkin) {
        return;
    }
    bool slim = skin.mSkinResourcePatch.find("Slim") != std::string::npos || skin.mSkinResourcePatch.find("slim") != std::string::npos;
    slotOwners[slot] = uuid;
    skinByUuid[uuid] = { slot, slim };

    SkinUpload upload;
    upload.slot = slot;
    upload.rig = world::buildSkinRig(skin.mGeometryData, skin.mSkinResourcePatch);
    upload.pixels.resize(size_t(world::EntityTextureSize) * world::EntityTextureSize * 4);
    for (uint32_t y = 0; y < world::EntityTextureSize; ++y) {
        for (uint32_t x = 0; x < world::EntityTextureSize; ++x) {
            size_t source = (size_t(y * uint32_t(image.mHeight) / world::EntityTextureSize) * size_t(image.mWidth) + x * uint32_t(image.mWidth) / world::EntityTextureSize) * 4;
            std::memcpy(upload.pixels.data() + (size_t(y) * world::EntityTextureSize + x) * 4, image.mData.data() + source, 4);
        }
    }
    for (auto& [runtime, actor] : actors) {
        auto owner = uuidByRuntime.find(runtime);
        if (owner != uuidByRuntime.end() && owner->second == uuid) {
            actor.skinSlot = slot;
            actor.slim = slim;
        }
    }
    std::lock_guard<std::mutex> guard(mutex);
    pendingSkins.push_back(std::move(upload));
}

void Session::releaseSkin(const std::string& uuid)
{
    auto skin = skinByUuid.find(uuid);
    if (skin == skinByUuid.end()) {
        return;
    }
    slotOwners[skin->second.first].clear();
    skinByUuid.erase(skin);
}

std::vector<MeshUpdate> Session::takeMeshUpdates()
{
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<MeshUpdate> updates = std::move(pendingUpdates);
    pendingUpdates.clear();
    return updates;
}

/**
 * Tells the server the local player has finished loading, once, after its
 * PlayerSpawn status; the connection hands over before that status arrives so
 * chunks can stream in while the server waits for it.
 */
void Session::initializeLocalPlayer(BedrockConnection& target, uint64_t runtimeId)
{
    if (spawnInitialized) {
        return;
    }
    SetLocalPlayerAsInitializedPacket initialized;
    initialized.mRuntimeActorId = runtimeId;
    target.send(initialized);
    target.flush();
    spawnInitialized = true;
    debugLog("sent SetLocalPlayerAsInitialized");
}

void Session::setRenderDistance(int chunks)
{
    requestedRadius = chunks;
}

void Session::answerResourcePacks(bool download)
{
    packDecision = static_cast<int>(download ? ResourcePackDecision::Download : ResourcePackDecision::Skip);
    std::lock_guard<std::mutex> guard(mutex);
    current.packPrompt = false;
    current.packDownloading = download;
    current.packReceived = 0;
    current.packTotal = current.packBytes;
}

void Session::setLookRay(const std::array<double, 3>& origin, const std::array<float, 3>& direction)
{
    std::lock_guard<std::mutex> guard(mutex);
    lookOrigin = origin;
    lookDirection = direction;
}

/**
 * The liquid the given point sits in: 0 for air, 1 for water, 2 for lava. A
 * liquid fills (8 - level) / 9 of its block, a falling one or one under the
 * same liquid fills it all, the way liquid surfaces are meshed.
 */
uint8_t Session::mediumAt(const std::array<double, 3>& position)
{
    if (!assets) {
        return 0;
    }
    auto liquidAt = [&](int64_t x, int64_t y, int64_t z, uint8_t& level) -> uint8_t {
        world::SubChunkKey key { current.dimension, int32_t(x >> 4), int32_t(y >> 4), int32_t(z >> 4) };
        std::shared_ptr<const world::SubChunk> sub = world.store().subChunk(key);
        if (!sub) {
            return 0;
        }
        for (uint32_t layer = 0; layer < 2; ++layer) {
            uint32_t value = sub->runtimeId(layer, uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15));
            if (value == world::ImplicitAir) {
                continue;
            }
            const world::BlockVisual& visual = assets->visual(value, ids.hashed, ids.sequential.get());
            if (visual.liquid) {
                level = visual.liquidLevel;
                return visual.liquid;
            }
        }
        return 0;
    };
    int64_t x = static_cast<int64_t>(std::floor(position[0]));
    int64_t y = static_cast<int64_t>(std::floor(position[1]));
    int64_t z = static_cast<int64_t>(std::floor(position[2]));
    uint8_t level = 0;
    uint8_t kind = liquidAt(x, y, z, level);
    if (!kind) {
        return 0;
    }
    uint8_t above = 0;
    double surface = level >= 8 || liquidAt(x, y + 1, z, above) == kind ? 1.0 : (8.0 - (level & 7)) / 9.0;
    return position[1] - static_cast<double>(y) < surface ? kind : 0;
}

std::string Session::traceTarget()
{
    constexpr double Reach = 32.0;
    std::array<int64_t, 3> cell {};
    std::array<int64_t, 3> step {};
    std::array<double, 3> next {};
    std::array<double, 3> delta {};
    for (int axis = 0; axis < 3; ++axis) {
        double origin = lookOrigin[axis];
        double direction = lookDirection[axis];
        cell[axis] = static_cast<int64_t>(std::floor(origin));
        step[axis] = direction > 0.0 ? 1 : (direction < 0.0 ? -1 : 0);
        delta[axis] = direction != 0.0 ? std::abs(1.0 / direction) : 1e30;
        double boundary = direction > 0.0 ? double(cell[axis] + 1) - origin : origin - double(cell[axis]);
        next[axis] = direction != 0.0 ? boundary * delta[axis] : 1e30;
    }

    double travelled = 0.0;
    while (travelled <= Reach) {
        world::SubChunkKey key { current.dimension, int32_t(cell[0] >> 4), int32_t(cell[1] >> 4), int32_t(cell[2] >> 4) };
        if (std::shared_ptr<const world::SubChunk> sub = world.store().subChunk(key)) {
            uint32_t value = sub->runtimeId(0, uint32_t(cell[0] & 15), uint32_t(cell[1] & 15), uint32_t(cell[2] & 15));
            const world::BlockVisual& visual = assets->visual(value, ids.hashed, ids.sequential.get());
            std::string name = assets->describe(value, ids.hashed, ids.sequential.get());
            bool fluid = name.find("water") != std::string::npos || name.find("lava") != std::string::npos;
            if (value != world::ImplicitAir && visual.flags != 0 && !(visual.flags & world::FlagAir) && !fluid) {
                return name + "  (" + std::to_string(cell[0]) + ", " + std::to_string(cell[1]) + ", " + std::to_string(cell[2]) + ")";
            }
        }
        int axis = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2) : (next[1] < next[2] ? 1 : 2);
        travelled = next[axis];
        next[axis] += delta[axis];
        cell[axis] += step[axis];
    }
    return "nothing";
}

void Session::handleWorldPacket(const std::string& payload)
{
    MinecraftPacketIds id;
    if (!BedrockConnection::peekPacketId(payload, id)) {
        return;
    }
    if (seenPackets.insert(static_cast<int>(id)).second) {
        debugLog("first world packet " + std::to_string(static_cast<int>(id)));
    }

    switch (id) {
    case MinecraftPacketIds::PlayStatus:
    case MinecraftPacketIds::BlockActorData:
    case MinecraftPacketIds::LevelChunk:
    case MinecraftPacketIds::SubChunk:
    case MinecraftPacketIds::UpdateBlock:
    case MinecraftPacketIds::UpdateSubChunkBlocks:
    case MinecraftPacketIds::NetworkChunkPublisherUpdate:
    case MinecraftPacketIds::ChunkRadiusUpdated:
    case MinecraftPacketIds::ChangeDimension:
    case MinecraftPacketIds::MovePlayer:
    case MinecraftPacketIds::AddPlayer:
    case MinecraftPacketIds::AddActor:
    case MinecraftPacketIds::RemoveActor:
    case MinecraftPacketIds::MoveActorAbsolute:
    case MinecraftPacketIds::MoveActorDelta:
    case MinecraftPacketIds::SetActorData:
    case MinecraftPacketIds::InventoryContent:
    case MinecraftPacketIds::InventorySlot:
    case MinecraftPacketIds::MobEquipment:
    case MinecraftPacketIds::PlayerHotbar:
    case MinecraftPacketIds::UpdateAttributes:
    case MinecraftPacketIds::SetHealth:
    case MinecraftPacketIds::SetPlayerGameType:
    case MinecraftPacketIds::MobEffect:
    case MinecraftPacketIds::PlayerList:
    case MinecraftPacketIds::SetTime:
    case MinecraftPacketIds::GameRulesChanged:
    case MinecraftPacketIds::LevelEvent:
    case MinecraftPacketIds::NetworkStackLatency:
    case MinecraftPacketIds::Respawn:
    case MinecraftPacketIds::LevelSoundEvent:
    case MinecraftPacketIds::PlaySound:
    case MinecraftPacketIds::StopSound:
    case MinecraftPacketIds::CorrectPlayerMovePrediction:
    case MinecraftPacketIds::SetActorMotion:
    case MinecraftPacketIds::UpdateAbilities:
        break;
    default:
        return;
    }

    std::shared_ptr<Packet> packet = connection->decode(payload);
    if (!packet) {
        return;
    }
    handleHudPacket(packet);
    handleMotionPacket(packet);
    handleSoundPacket(packet);

    if (auto levelChunk = std::dynamic_pointer_cast<LevelChunkPacket>(packet)) {
        world.handle(*levelChunk);
    } else if (auto subChunk = std::dynamic_pointer_cast<SubChunkPacket>(packet)) {
        world.handle(*subChunk);
    } else if (auto updateBlock = std::dynamic_pointer_cast<UpdateBlockPacket>(packet)) {
        world.handle(*updateBlock);
    } else if (auto updateSubChunk = std::dynamic_pointer_cast<UpdateSubChunkBlocksPacket>(packet)) {
        world.handle(*updateSubChunk);
    } else if (auto publisher = std::dynamic_pointer_cast<NetworkChunkPublisherUpdatePacket>(packet)) {
        world.handle(*publisher);
    } else if (auto radius = std::dynamic_pointer_cast<ChunkRadiusUpdatedPacket>(packet)) {
        debugLog("server chunk radius " + std::to_string(radius->mRadius));
        world.setChunkRadius(radius->mRadius);
        std::lock_guard<std::mutex> guard(mutex);
        current.chunkRadius = radius->mRadius;
    } else if (auto move = std::dynamic_pointer_cast<MovePlayerPacket>(packet)) {
        if (static_cast<uint64_t>(move->mRuntimeActorId) == localRuntimeId) {
            std::lock_guard<std::mutex> guard(mutex);
            current.spawnX = move->mPosition.x;
            current.spawnY = move->mPosition.y;
            current.spawnZ = move->mPosition.z;
            current.spawnPitch = move->mRotation.x;
            current.spawnYaw = move->mRotation.y;
            ++current.teleportCount;
        } else {
            bool teleport = move->mMode == MovePlayerMode::Teleport || move->mMode == MovePlayerMode::Respawn;
            moveActor(static_cast<uint64_t>(move->mRuntimeActorId), move->mPosition.x, move->mPosition.y, move->mPosition.z, move->mRotation.y, move->mRotation.z, move->mRotation.x, teleport, move->mOnGround);
        }
    } else if (auto player = std::dynamic_pointer_cast<AddPlayerPacket>(packet)) {
        uint64_t runtime = static_cast<uint64_t>(player->mRuntimeActorId);
        if (runtime != localRuntimeId) {
            ActorView actor;
            actor.runtimeId = runtime;
            actor.identifier = "minecraft:player";
            std::string uuid = player->mUuid.toString();
            uuidByRuntime[runtime] = uuid;
            if (auto skin = skinByUuid.find(uuid); skin != skinByUuid.end()) {
                actor.skinSlot = skin->second.first;
                actor.slim = skin->second.second;
            }
            actor.scale = metadataScale(player->mMetadata, 1.0f);
            applyActorMetadata(player->mMetadata, actor);
            actors[runtime] = actor;
            runtimeByUnique[player->mRuntimeActorId] = runtime;
            moveActor(runtime, player->mPosition.x, player->mPosition.y, player->mPosition.z, player->mRotation.y, player->mRotation.z, player->mRotation.x, true, true);
        }
    } else if (auto added = std::dynamic_pointer_cast<AddActorPacket>(packet)) {
        uint64_t runtime = static_cast<uint64_t>(added->mRuntimeActorId);
        ActorView actor;
        actor.runtimeId = runtime;
        actor.identifier = added->mIdentifier;
        actor.scale = metadataScale(added->mMetadata, 1.0f);
        applyActorMetadata(added->mMetadata, actor);
        actors[runtime] = actor;
        runtimeByUnique[added->mUniqueActorId] = runtime;
        moveActor(runtime, added->mPosition.x, added->mPosition.y, added->mPosition.z, added->mBodyRotation, added->mHeadRotation, added->mRotation.x, true, true);
    } else if (auto data = std::dynamic_pointer_cast<SetActorDataPacket>(packet)) {
        if (auto actor = actors.find(static_cast<uint64_t>(data->mRuntimeActorId)); actor != actors.end()) {
            actor->second.scale = metadataScale(data->mMetadata, actor->second.scale);
            applyActorMetadata(data->mMetadata, actor->second);
        }
    } else if (auto removed = std::dynamic_pointer_cast<RemoveActorPacket>(packet)) {
        auto runtime = runtimeByUnique.find(removed->mUniqueActorId);
        if (runtime != runtimeByUnique.end()) {
            actors.erase(runtime->second);
            uuidByRuntime.erase(runtime->second);
            runtimeByUnique.erase(runtime);
        }
    } else if (auto absolute = std::dynamic_pointer_cast<MoveActorAbsolutePacket>(packet)) {
        moveActor(static_cast<uint64_t>(absolute->mRuntimeActorId), absolute->mPosition.x, absolute->mPosition.y, absolute->mPosition.z, absolute->mRotation.y, absolute->mRotation.z, absolute->mRotation.x, absolute->mTeleported, absolute->mOnGround);
    } else if (auto delta = std::dynamic_pointer_cast<MoveActorDeltaPacket>(packet)) {
        auto actor = actors.find(delta->mRuntimeActorId);
        if (actor != actors.end()) {
            double eye = actor->second.identifier == "minecraft:player" ? PlayerEyeHeight : 0.0;
            moveActor(delta->mRuntimeActorId, delta->mHasX ? delta->mX : actor->second.x, delta->mHasY ? delta->mY : actor->second.y + eye,
                delta->mHasZ ? delta->mZ : actor->second.z, delta->mHasYaw ? delta->mYaw : actor->second.yaw, delta->mHasHeadYaw ? delta->mHeadYaw : actor->second.headYaw,
                delta->mHasPitch ? delta->mPitch : actor->second.pitch, false, delta->mOnGround);
        }
    } else if (auto list = std::dynamic_pointer_cast<PlayerListPacket>(packet)) {
        for (const PlayerListPacket::Entry& entry : list->mEntries) {
            std::string uuid = entry.mUuid.toString();
            if (entry.mAction == PlayerListPacket::Action::Remove) {
                releaseSkin(uuid);
                continue;
            }
            storeSkin(uuid, entry.mSkin);
        }
    } else if (auto time = std::dynamic_pointer_cast<SetTimePacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        current.worldTime = time->mTime;
        current.worldTimeStamp = secondsNow();
        debugLog("set time " + std::to_string(time->mTime));
    } else if (auto rules = std::dynamic_pointer_cast<GameRulesChangedPacket>(packet)) {
        for (const ChangedGameRuleData& rule : rules->mGameRules) {
            if (rule.mName == "dodaylightcycle" && rule.mType == ChangedGameRuleType::Bool) {
                std::lock_guard<std::mutex> guard(mutex);
                current.worldTime = currentWorldTime(current);
                current.worldTimeStamp = secondsNow();
                current.daylightCycle = rule.mBoolValue;
            }
        }
    } else if (auto actor = std::dynamic_pointer_cast<BlockActorDataPacket>(packet)) {
        world.handle(*actor);
    } else if (auto status = std::dynamic_pointer_cast<PlayStatusPacket>(packet)) {
        debugLog("play status " + std::to_string(static_cast<int>(status->mStatus)));
        if (status->mStatus == PlayStatusPacket::Status::PlayerSpawn) {
            initializeLocalPlayer(*connection, localRuntimeId);
        }
    } else if (auto event = std::dynamic_pointer_cast<LevelEventPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        switch (event->mEventId) {
        case LevelEventPacket::StartRain:
            current.rainLevel = 1.0f;
            break;
        case LevelEventPacket::StopRain:
            current.rainLevel = 0.0f;
            break;
        case LevelEventPacket::StartThunder:
            current.thunderLevel = 1.0f;
            break;
        case LevelEventPacket::StopThunder:
            current.thunderLevel = 0.0f;
            break;
        default:
            break;
        }
    } else if (auto dimension = std::dynamic_pointer_cast<ChangeDimensionPacket>(packet)) {
        world.changeDimension(dimension->mDimension, floorChunk(dimension->mPosition.x), floorChunk(dimension->mPosition.z));
        motionDimension = dimension->mDimension;
        motion.teleport({ dimension->mPosition.x, dimension->mPosition.y - EyeHeight, dimension->mPosition.z });
        motionStarted = false;
        actors.clear();
        runtimeByUnique.clear();
        uuidByRuntime.clear();
        std::lock_guard<std::mutex> guard(mutex);
        current.dimension = dimension->mDimension;
    }
}

void Session::scheduleMeshes()
{
    std::vector<world::SubChunkKey> dirty = world.store().takeDirty();
    if (!assets) {
        return;
    }

    static constexpr int32_t Offsets[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    for (const world::SubChunkKey& key : dirty) {
        uint64_t generation = ++meshGenerations[key];
        std::shared_ptr<const world::SubChunk> center = world.store().subChunk(key);
        if (!center) {
            auto existing = meshes.find(key);
            if (existing != meshes.end()) {
                meshQuads -= existing->second->quadCount();
                meshes.erase(existing);
                std::lock_guard<std::mutex> guard(mutex);
                pendingUpdates.push_back({ key, nullptr });
            }
            continue;
        }

        world::DimensionRange range;
        if (!world::vanillaDimensionRange(key.dimension, range)) {
            range = { key.y, 32 };
        }
        world::MeshInput input;
        input.center = std::move(center);
        for (size_t face = 0; face < 6; ++face) {
            input.neighbours[face] = world.store().subChunk({ key.dimension, key.x + Offsets[face][0], key.y + Offsets[face][1], key.z + Offsets[face][2] });
        }
        for (int32_t dz = -1; dz <= 1; ++dz) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                input.biomes[size_t((dz + 1) * 3 + (dx + 1))] = world.store().biomes({ key.dimension, key.x + dx, key.y, key.z + dz });
                for (int32_t dy = -1; dy <= 1; ++dy) {
                    input.around[size_t((dx + 1) * 9 + (dy + 1) * 3 + (dz + 1))] = world.store().subChunk({ key.dimension, key.x + dx, key.y + dy, key.z + dz });
                }
                for (int32_t y = key.y + 2; y < range.baseSubChunkY + range.subChunkCount; ++y) {
                    if (std::shared_ptr<const world::SubChunk> above = world.store().subChunk({ key.dimension, key.x + dx, y, key.z + dz })) {
                        input.above[size_t((dx + 1) * 3 + (dz + 1))].push_back(std::move(above));
                    }
                }
            }
        }
        input.skyLight = key.dimension == 0;
        input.blockEntities = world.store().blockEntities(key);
        input.origin = { key.x * 16, key.y * 16, key.z * 16 };
        mesher->submit(key, generation, std::move(input), assets, ids);
    }
}

void Session::collectMeshes()
{
    for (world::MeshResult& result : mesher->takeResults()) {
        auto generation = meshGenerations.find(result.key);
        if (generation == meshGenerations.end() || generation->second != result.generation) {
            continue;
        }
        auto existing = meshes.find(result.key);
        bool hadMesh = existing != meshes.end();
        if (hadMesh) {
            meshQuads -= existing->second->quadCount();
            meshes.erase(existing);
        }

        const std::vector<world::Material>& materials = assets->materials();
        auto word = [&](uint32_t id) {
            return (id < materials.size() ? materials[id] : materials.front()).gpuWord();
        };
        for (auto* cubes : { &result.mesh.cubes, &result.mesh.translucentCubes }) {
            for (world::PackedQuad& quad : *cubes) {
                quad.material = word(quad.material);
            }
        }
        for (auto* models : { &result.mesh.models, &result.mesh.translucentModels }) {
            for (world::ModelQuadGpu& quad : *models) {
                quad.words[10] = word(quad.words[10]);
            }
        }

        std::shared_ptr<const world::ChunkMesh> mesh;
        if (!result.mesh.empty()) {
            meshQuads += result.mesh.quadCount();
            mesh = std::make_shared<const world::ChunkMesh>(std::move(result.mesh));
            meshes.emplace(result.key, mesh);
        }
        if (mesh || hadMesh) {
            std::lock_guard<std::mutex> guard(mutex);
            pendingUpdates.push_back({ result.key, std::move(mesh) });
        }
    }
}

void Session::fail(const std::string& error)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (cancelled) {
        current.state = SessionState::Idle;
        return;
    }
    current.state = SessionState::Failed;
    current.error = error;
}

void Session::run(std::string target, MinecraftAuthentication* authentication, std::string offlineName)
{
    ClientConnectionSettings settings;
    settings.mProtocolVersion = ProtocolVersion;
    settings.mGameVersion = GameVersion;
    settings.mAuthentication = authentication;
    settings.mIdentity.mDisplayName = offlineName;
    settings.mChunkRadius = requestedRadius.load();
    settings.mTimeoutMs = TimeoutMs;
    settings.mCancel = &cancelled;
    packDecision = static_cast<int>(ResourcePackDecision::Pending);
    settings.mResourcePacks.mIsCached = [this](const ResourcePackOffer& offer) {
        std::filesystem::path path = packPath(offer.mPackId, offer.mPackVersion);
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return false;
        }
        bool needsTitle = false;
        {
            std::lock_guard<std::mutex> guard(mutex);
            needsTitle = !current.titleImage;
        }
        if (needsTitle) {
            std::string packError;
            if (std::shared_ptr<const world::PackFiles> pack = world::loadServerPack(path, offer.mContentKey, packError)) {
                if (std::shared_ptr<const std::vector<uint8_t>> title = packTitle(*pack)) {
                    std::lock_guard<std::mutex> guard(mutex);
                    current.titleImage = std::move(title);
                }
            }
        }
        return true;
    };
    settings.mResourcePacks.mOffer = [this](const std::vector<ResourcePackOffer>& offers) {
        std::lock_guard<std::mutex> guard(mutex);
        current.packPrompt = true;
        current.packCount = offers.size();
        current.packBytes = 0;
        for (const ResourcePackOffer& offer : offers) {
            current.packBytes += offer.mPackSize;
        }
    };
    settings.mResourcePacks.mDecision = [this]() {
        return static_cast<ResourcePackDecision>(packDecision.load());
    };
    settings.mResourcePacks.mProgress = [this](uint64_t received, uint64_t total) {
        std::lock_guard<std::mutex> guard(mutex);
        current.packDownloading = true;
        current.packReceived = received;
        current.packTotal = total;
    };

    if (target.rfind(RealmPrefix, 0) == 0) {
        if (!authentication) {
            fail("Sign in with Microsoft to join Realms");
            return;
        }
        long long realmId = std::strtoll(target.c_str() + std::char_traits<char>::length(RealmPrefix), nullptr, 10);
        RealmsService realms(*authentication);
        RealmAddress address;
        SessionConnectionTarget resolved;
        std::string error;
        if (!realms.requestAddress(realmId, TimeoutMs, &cancelled, address, error) || !address.toTarget(resolved, error)) {
            fail(error);
            return;
        }
        resolved.applyTo(settings);
        std::lock_guard<std::mutex> guard(mutex);
        current.state = SessionState::Connecting;
    } else if (!parseHostPort(target, settings.mHost, settings.mPort)) {
        fail("Invalid server address: " + target);
        return;
    }

    resetDebugLog();
    debugLog("dial " + settings.mHost + ":" + std::to_string(settings.mPort) + " radius " + std::to_string(settings.mChunkRadius));
    settings.mDeferSpawn = true;
    settings.mPacketObserver = [](MinecraftPacketIds id) {
        debugLog("dial packet " + std::to_string(static_cast<int>(id)));
    };
    ClientConnectionResult result = ClientNetworkSystem::dial(settings);
    {
        std::lock_guard<std::mutex> guard(mutex);
        current.packPrompt = false;
        current.packDownloading = false;
    }
    for (const DownloadedResourcePack& pack : result.mResourcePacks) {
        savePack(pack);
    }
    if (!result.mConnection) {
        debugLog("dial failed: " + result.mError);
        fail(result.mError.empty() ? "Could not connect" : result.mError);
        return;
    }
    debugLog("dial done, chunk radius " + std::to_string(result.mConnection->getChunkRadius()) + ", spawn " + (result.mConnection->isSpawnReceived() ? "received" : "pending"));
    blockDefinitions = BlockDefinitionRegistry {};
    itemDefinitions = ItemDefinitionRegistry {};
    if (const std::shared_ptr<ItemRegistryPacket>& registry = result.mConnection->getItemRegistry()) {
        for (const ItemComponentEntry& entry : registry->mEntries) {
            itemDefinitions.registerDefinition(std::make_shared<ItemDefinition>(entry.mIdentifier, entry.mRuntimeId, entry.mComponentBased, entry.mComponentData));
        }
    }
    codecContext = std::make_unique<PacketCodecContext>(blockDefinitions, itemDefinitions);
    result.mConnection->setCodecContext(codecContext.get());
    inventoryStacks = {};
    requestedSlot = -1;
    spawnInitialized = false;
    motion = PlayerMotion {};
    motionStarted = false;
    teleportHandled = false;
    clientTick = 0;
    nextMotionTick = 0.0;
    lastMotionInput = MotionInput {};
    if (result.mConnection->isSpawnReceived()) {
        initializeLocalPlayer(*result.mConnection, result.mConnection->getStartGame() ? result.mConnection->getStartGame()->mRuntimeActorId : 0);
    }

    {
        std::lock_guard<std::mutex> guard(mutex);
        connection = std::move(result.mConnection);
        current.state = SessionState::Joined;
        current.displayName = result.mIdentity.mDisplayName;
        current.chunkRadius = connection->getChunkRadius();
        current.joinCount = ++joins;
        pendingUpdates.clear();
        pendingSkins.clear();
        actors.clear();
        runtimeByUnique.clear();
        uuidByRuntime.clear();
        skinByUuid.clear();
        slotOwners = {};
        if (const std::shared_ptr<StartGamePacket>& startGame = connection->getStartGame()) {
            current.spawnX = startGame->mPlayerPosition.x;
            current.spawnY = startGame->mPlayerPosition.y;
            current.spawnZ = startGame->mPlayerPosition.z;
            current.spawnPitch = startGame->mRotation.x;
            current.spawnYaw = startGame->mRotation.y;
            current.hashedIds = startGame->mBlockNetworkIdsHashed;
            localRuntimeId = startGame->mRuntimeActorId;
            current.hud = HudState {};
            current.hud.gameType = static_cast<int32_t>(startGame->mPlayerGameType == GameType::Default ? startGame->mLevelGameType : startGame->mPlayerGameType);
            motionDimension = startGame->mDimensionId;
            motion.reset({ startGame->mPlayerPosition.x, startGame->mPlayerPosition.y - EyeHeight, startGame->mPlayerPosition.z });
            motion.setGameType(current.hud.gameType);
            MotionVector feet = motion.position();
            current.player = PlayerView {};
            current.player.previous = { feet.x, feet.y, feet.z };
            current.player.current = current.player.previous;
            current.player.tickTime = secondsNow();
            current.levelName = startGame->mLevelName;
            current.gameMode = gameModeName(startGame->mPlayerGameType);
            current.dimension = startGame->mDimensionId;
            current.worldTimeStamp = secondsNow();
            current.rainLevel = std::isfinite(startGame->mRainLevel) ? std::clamp(startGame->mRainLevel, 0.0f, 1.0f) : 0.0f;
            current.thunderLevel = std::isfinite(startGame->mLightningLevel) ? std::clamp(startGame->mLightningLevel, 0.0f, 1.0f) : 0.0f;
            for (const GameRuleData& rule : startGame->mGamerules) {
                if (rule.mName == "dodaylightcycle" && rule.mType == GameRuleData::Type::Bool) {
                    current.daylightCycle = rule.mBoolValue;
                }
            }
            current.worldTime = !current.daylightCycle && startGame->mDayCycleStopTime >= 0 ? startGame->mDayCycleStopTime : startGame->mCurrentTick;
            debugLog("start game time " + std::to_string(startGame->mCurrentTick) + ", lock time " + std::to_string(startGame->mDayCycleStopTime) + ", daylight cycle " + (current.daylightCycle ? "on" : "off"));
            char position[96];
            std::snprintf(position, sizeof(position), "%.1f, %.1f, %.1f", startGame->mPlayerPosition.x, startGame->mPlayerPosition.y, startGame->mPlayerPosition.z);
            current.position = position;
        }
    }

    if (const std::shared_ptr<StartGamePacket>& startGame = connection->getStartGame()) {
        world.reset(startGame->mDimensionId, floorChunk(startGame->mPlayerPosition.x), floorChunk(startGame->mPlayerPosition.z));
        hashedNetworkIds = startGame->mBlockNetworkIdsHashed;
    }
    world.setChunkRadius(connection->getChunkRadius());
    sentRadius = settings.mChunkRadius;

    std::string assetsError;
    std::vector<std::shared_ptr<const world::PackFiles>> packs;
    for (const ResourcePackOffer& offer : result.mOfferedPacks) {
        std::filesystem::path path = packPath(offer.mPackId, offer.mPackVersion);
        std::error_code exists;
        if (!std::filesystem::exists(path, exists)) {
            continue;
        }
        std::string key = offer.mContentKey;
        if (key.empty()) {
            std::ifstream keyFile(std::filesystem::path(path).replace_extension(".key"));
            std::getline(keyFile, key);
        }
        std::string packError;
        if (std::shared_ptr<const world::PackFiles> pack = world::loadServerPack(path, key, packError)) {
            std::shared_ptr<const std::vector<uint8_t>> title = packTitle(*pack);
            std::lock_guard<std::mutex> guard(mutex);
            if (title && !current.titleImage) {
                current.titleImage = std::move(title);
            }
            packs.push_back(std::move(pack));
        } else {
            assetsError = "Resource pack " + offer.mPackId + ": " + packError;
        }
    }
    std::vector<world::CustomBlock> customBlocks;
    if (const std::shared_ptr<StartGamePacket>& startGame = connection->getStartGame()) {
        for (const BlockPropertyData& block : startGame->mBlockProperties) {
            customBlocks.push_back({ block.mName, block.mProperties });
        }
    }
    std::string buildError;
    assets = world::BlockAssets::create(packs, customBlocks, buildError);
    if (!buildError.empty()) {
        assetsError = buildError;
    }
    ids = world::IdMapping {};
    ids.hashed = hashedNetworkIds;
    size_t customCount = 0;
    size_t customPermutationCount = 0;
    if (assets) {
        customCount = assets->customBlockCount();
        customPermutationCount = assets->customStateCount();
        ids.sequential = assets->sequentialMap();
    }
    if (!mesher) {
        mesher = std::make_unique<world::MeshScheduler>();
    }
    mesher->clear();
    meshGenerations.clear();
    meshes.clear();
    meshQuads = 0;
    {
        std::lock_guard<std::mutex> guard(mutex);
        current.assetsError = assetsError;
        current.customBlocks = customCount;
        current.customPermutations = customPermutationCount;
        current.assets = assets;
        current.packs = packs;
        if (assets) {
            current.materials = assets->materials().size();
            current.textureLayers = assets->textures().layers;
            debugLog("texture layers " + std::to_string(assets->textures().layers) + ", model templates " + std::to_string(assets->modelTemplates().size()));
            current.diagnosticVisuals = assets->diagnosticVisuals();
        }
    }

    std::string payload;
    while (!cancelled) {
        int waitMs = 50;
        if (spawnInitialized && nextMotionTick > 0.0) {
            waitMs = std::clamp(static_cast<int>((nextMotionTick - secondsNow()) * 1000.0), 1, 50);
        }
        bool received = connection->readRaw(payload, waitMs, &cancelled);
        if (received) {
            handleWorldPacket(payload);
        } else if (connection->isClosed()) {
            break;
        }

        for (const std::unique_ptr<SubChunkRequestPacket>& request : world.takeRequests(world::WorldStream::Clock::now())) {
            connection->send(*request);
        }
        if (int wanted = requestedRadius.load(); wanted != sentRadius) {
            RequestChunkRadiusPacket request;
            request.mRadius = wanted;
            request.mMaxRadius = wanted;
            connection->send(request);
            sentRadius = wanted;
            debugLog("requested chunk radius " + std::to_string(wanted));
        }
        if (int slot = requestedSlot.exchange(-1); slot >= 0) {
            sendSelectedSlot(slot);
        }
        tickMotion();
        scheduleMeshes();
        collectMeshes();

        std::lock_guard<std::mutex> guard(mutex);
        if (received) {
            ++current.packetsReceived;
        }
        current.world = world.stats();
        current.meshes = meshes.size();
        current.meshQuads = meshQuads;
        current.meshJobs = mesher->pending();
        current.cohortComplete = world.cohortLoaded();
        current.updatesPending = !pendingUpdates.empty();
        current.actors.clear();
        for (const auto& [runtime, actor] : actors) {
            current.actors.push_back(actor);
        }
        if (double now = secondsNow(); now - lastReadinessLog >= 1.0) {
            lastReadinessLog = now;
            debugLog("columns " + std::to_string(current.world.columns) + " subchunks " + std::to_string(current.world.subChunks) + " pending " + std::to_string(current.world.pendingSubChunks)
                + " meshes " + std::to_string(meshes.size()) + " jobs " + std::to_string(current.meshJobs) + " cohort " + (current.cohortComplete ? "complete" : "incomplete")
                + " updates " + (current.updatesPending ? "pending" : "none") + " radius " + std::to_string(current.chunkRadius) + " spawn " + (spawnInitialized ? "initialized" : "waiting") + " actors " + std::to_string(actors.size()) + " skins " + std::to_string(skinByUuid.size()) + " mesh ms " + std::to_string(mesher->averageMilliseconds()) + " workers " + std::to_string(mesher->workerCount()));
        }
        if (assets) {
            int32_t bx = static_cast<int32_t>(std::floor(current.spawnX));
            int32_t by = static_cast<int32_t>(std::floor(current.spawnY));
            int32_t bz = static_cast<int32_t>(std::floor(current.spawnZ));
            world::SubChunkKey probe { current.dimension, bx >> 4, by >> 4, bz >> 4 };
            if (std::shared_ptr<const world::SubChunk> sub = world.store().subChunk(probe)) {
                uint32_t value = sub->runtimeId(0, uint32_t(bx & 15), uint32_t(by & 15), uint32_t(bz & 15));
                current.blockAtPlayer = assets->describe(value, ids.hashed, ids.sequential.get());
            } else {
                current.blockAtPlayer = "no sub-chunk (air)";
            }
            current.targetBlock = traceTarget();
            current.cameraMedium = mediumAt(lookOrigin);
            current.airSequential = assets->airSequentialId();
            current.airHash = assets->airNetworkHash();
            current.unresolvedLookups = assets->unresolvedLookups();
            current.lastUnresolved = assets->lastUnresolvedValue();
        }
    }

    std::lock_guard<std::mutex> guard(mutex);
    if (cancelled) {
        connection->disconnect("Disconnected");
        current.state = SessionState::Idle;
    } else {
        current.state = SessionState::Disconnected;
        current.error = connection->getDisconnectReason();
    }
    connection.reset();
}

}
