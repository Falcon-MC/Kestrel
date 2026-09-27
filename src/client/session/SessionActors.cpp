#include "SessionData.h"

#include "Protocol/Types/SerializedSkin.h"
#include "world/BlockAssets.h"

#include <cstring>

namespace kestrel {

namespace session {

float metadataScale(const EntityDataMap& metadata, float fallback)
{
    for (const EntityDataEntry& entry : metadata.mEntries) {
        if (entry.mId == ScaleDataId && entry.mFormat == EntityDataFormat::Float && entry.mFloatValue > 0.0f) {
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
    actor->second.y = y - (actor->second.identifier == "minecraft:player" ? session::PlayerEyeHeight : 0.0);
    actor->second.z = z;
    actor->second.yaw = yaw;
    actor->second.headYaw = headYaw;
    actor->second.pitch = pitch;
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

}
