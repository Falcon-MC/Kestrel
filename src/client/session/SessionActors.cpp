#include "SessionData.h"

#include "Protocol/Types/SerializedSkin.h"
#include "client/DebugLog.h"
#include "world/BlockAssets.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace kestrel {

namespace {

/**
 * Old 64x32 skins only paint the right arm and leg. The player model wants a
 * square texture, so the lower half is filled the way Java does it, with the
 * right limbs copied over mirrored into the left limb slots.
 */
std::vector<uint8_t> squareLegacySkin(const SkinImageData& image)
{
    uint32_t width = uint32_t(image.mWidth);
    uint32_t scale = width / 64;
    std::vector<uint8_t> pixels(size_t(width) * width * 4, 0);
    std::memcpy(pixels.data(), image.mData.data(), size_t(width) * uint32_t(image.mHeight) * 4);
    static constexpr int32_t Copies[12][6] = {
        { 4, 16, 16, 32, 4, 4 }, { 8, 16, 16, 32, 4, 4 }, { 0, 20, 24, 32, 4, 12 }, { 4, 20, 16, 32, 4, 12 },
        { 8, 20, 8, 32, 4, 12 }, { 12, 20, 16, 32, 4, 12 }, { 44, 16, -8, 32, 4, 4 }, { 48, 16, -8, 32, 4, 4 },
        { 40, 20, 0, 32, 4, 12 }, { 44, 20, -8, 32, 4, 12 }, { 48, 20, -16, 32, 4, 12 }, { 52, 20, -8, 32, 4, 12 },
    };
    for (const auto& [x, y, dx, dy, w, h] : Copies) {
        uint32_t spanX = uint32_t(w) * scale;
        for (uint32_t row = 0; row < uint32_t(h) * scale; ++row) {
            for (uint32_t column = 0; column < spanX; ++column) {
                size_t source = (size_t(uint32_t(y) * scale + row) * width + uint32_t(x) * scale + column) * 4;
                size_t target = (size_t(uint32_t(y + dy) * scale + row) * width + uint32_t(x + dx) * scale + spanX - 1 - column) * 4;
                std::memcpy(pixels.data() + target, pixels.data() + source, 4);
            }
        }
    }
    return pixels;
}

}

namespace session {

float metadataScale(const EntityDataMap& metadata, float fallback)
{
    for (const EntityDataEntry& entry : metadata.mEntries) {
        if (entry.mId == ScaleDataId && entry.mFormat == EntityDataFormat::Float && entry.mFloatValue >= 0.0f) {
            return entry.mFloatValue;
        }
    }
    return fallback;
}

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
        case 53:
            actor.width = entry.mFloatValue;
            break;
        case 54:
            actor.height = entry.mFloatValue;
            break;
        case 79:
            actor.poseIndex = entry.mIntValue;
            break;
        case 81:
            actor.alwaysShowName = entry.mByteValue != 0;
            break;
        case 84:
            actor.scoreTag = entry.mStringValue;
            break;
        case 143:
            if (std::isfinite(entry.mFloatValue)) {
                actor.nameplateDistance = entry.mFloatValue;
            }
            break;
        case 8:
            actor.effectColor = static_cast<uint32_t>(entry.mIntValue);
            break;
        default:
            break;
        }
    }
}

}

std::vector<SkinUpload> Session::takeSkinUploads()
{
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<SkinUpload> uploads = std::move(pendingSkins);
    pendingSkins.clear();
    return uploads;
}

/**
 * Spawn packets give feet positions; player movement packets give eye positions.
 * Every move counts as a new sample for
 * the renderer to glide toward, and a teleport tells it to jump instead.
 */
void Session::moveActor(uint64_t runtimeId, double x, double y, double z, float yaw, float headYaw, float pitch, bool teleport, bool onGround, bool feetPosition)
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
    actor->second.y = y - (!feetPosition && actor->second.identifier == "minecraft:player" ? session::PlayerEyeHeight : 0.0);
    actor->second.z = z;
    actor->second.yaw = yaw;
    actor->second.headYaw = headYaw;
    actor->second.pitch = pitch;
}

/**
 * Remembers a player's skin. Proxies list every player of the network, far
 * more than there are skin slots, so a skin only takes a slot while an entity
 * in the world wears it, or when it is the local player's own.
 */
void Session::storeSkin(const std::string& uuid, const SerializedSkin& skin)
{
    const SkinImageData& image = skin.mSkinData;
    if (image.mWidth <= 0 || image.mHeight <= 0 || image.mData.size() < size_t(image.mWidth) * size_t(image.mHeight) * 4) {
        return;
    }
    knownSkins[uuid] = skin;
    if (uuid == localUuid || skinByUuid.contains(uuid) || skinWorn(uuid)) {
        assignSkin(uuid);
    }
}

bool Session::skinWorn(const std::string& uuid) const
{
    return std::any_of(uuidByRuntime.begin(), uuidByRuntime.end(), [&](const auto& entry) { return entry.second == uuid; });
}

/**
 * Puts a remembered skin in a skin slot, scaled to the entity texture size,
 * and points the entities wearing it at that slot.
 */
void Session::assignSkin(const std::string& uuid)
{
    auto known = knownSkins.find(uuid);
    if (known == knownSkins.end()) {
        return;
    }
    const SerializedSkin& skin = known->second;
    const SkinImageData& image = skin.mSkinData;
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
    const uint8_t* pixels = reinterpret_cast<const uint8_t*>(image.mData.data());
    uint32_t width = uint32_t(image.mWidth);
    uint32_t height = uint32_t(image.mHeight);
    std::vector<uint8_t> squared;
    // Legacy 64x32 skins often come with geometry laid out for a square texture, which
    // then reads the missing lower half, so they are squared unless the geometry is flat too.
    bool squareGeometry = !upload.rig || upload.rig->textureAspect > 0.75f;
    if (squareGeometry && width == height * 2 && width % 64 == 0) {
        squared = squareLegacySkin(image);
        pixels = squared.data();
        height = width;
    }
    debugLog("skin " + uuid + " slot " + std::to_string(slot) + " image " + std::to_string(image.mWidth) + "x" + std::to_string(image.mHeight)
        + " patch " + skin.mSkinResourcePatch + " geometry bytes " + std::to_string(skin.mGeometryData.size())
        + (upload.rig ? " rig quads " + std::to_string(upload.rig->quads.size()) + " aspect " + std::to_string(upload.rig->textureAspect) : std::string(" no rig"))
        + (squared.empty() ? "" : " squared"));
    upload.pixels.resize(size_t(world::EntityTextureSize) * world::EntityTextureSize * 4);
    for (uint32_t y = 0; y < world::EntityTextureSize; ++y) {
        for (uint32_t x = 0; x < world::EntityTextureSize; ++x) {
            size_t source = (size_t(y * height / world::EntityTextureSize) * width + x * width / world::EntityTextureSize) * 4;
            std::memcpy(upload.pixels.data() + (size_t(y) * world::EntityTextureSize + x) * 4, pixels + source, 4);
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

/**
 * Frees the skin slot of a player once nothing in the world wears it, and
 * forgets the skin once the player is off the list too. Servers put their
 * NPCs on the player list just long enough to send the skin and take them off
 * again, so an entity still wearing the skin keeps it until it leaves.
 */
void Session::releaseSkin(const std::string& uuid)
{
    if (!playerNames.contains(uuid) && !skinWorn(uuid)) {
        knownSkins.erase(uuid);
    }
    auto skin = skinByUuid.find(uuid);
    if (skin == skinByUuid.end() || uuid == localUuid || skinWorn(uuid)) {
        return;
    }
    slotOwners[skin->second.first].clear();
    skinByUuid.erase(skin);
}

}
