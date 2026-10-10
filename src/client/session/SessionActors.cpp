#include "client/session/SessionData.h"
#include "client/CrystalMetadata.h"
#include "client/ActorAimAssist.h"

#include "Protocol/Types/SerializedSkin.h"
#include "client/DebugLog.h"
#include "util/Text.h"
#include "world/BlockAssets.h"
#include "world/EntityAnimation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace kestrel {

namespace {

constexpr uint32_t ClassicSkinSide = 64;
constexpr uint8_t ClassicAlphaCutoff = 26;
constexpr float ClassicCoverageLimit = 0.6f;

/**
 * A 64-bit FNV-1a fingerprint of what a skin draws with: its image size and
 * pixels, its geometry and resource patch, its animation sheets and its cape.
 * Servers resend the player list every few seconds, and a skin that comes
 * back with the same fingerprint is not built again.
 */
uint64_t skinPrint(const SerializedSkin& skin)
{
    uint64_t hash = 14695981039346656037ull;
    auto mix = [&hash](const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t index = 0; index < size; ++index) {
            hash = (hash ^ bytes[index]) * 1099511628211ull;
        }
    };
    auto mixImage = [&mix](const SkinImageData& image) {
        int32_t size[2] = { int32_t(image.mWidth), int32_t(image.mHeight) };
        mix(size, sizeof(size));
        uint64_t length = image.mData.size();
        mix(&length, sizeof(length));
        mix(image.mData.data(), image.mData.size());
    };
    mixImage(skin.mSkinData);
    uint64_t geometryLength = skin.mGeometryData.size();
    mix(&geometryLength, sizeof(geometryLength));
    mix(skin.mGeometryData.data(), skin.mGeometryData.size());
    mix(skin.mSkinResourcePatch.data(), skin.mSkinResourcePatch.size());
    uint64_t animationCount = skin.mAnimations.size();
    mix(&animationCount, sizeof(animationCount));
    for (const SkinAnimationData& animation : skin.mAnimations) {
        mixImage(animation.mImage);
        mix(&animation.mTextureType, sizeof(animation.mTextureType));
        mix(&animation.mFrames, sizeof(animation.mFrames));
        mix(&animation.mExpressionType, sizeof(animation.mExpressionType));
    }
    mixImage(skin.mCapeData);
    uint8_t persona = skin.mPersona ? 1 : 0;
    mix(&persona, sizeof(persona));
    return hash;
}

/**
 * The game's alpha check of a classic skin, 64 or 128 texels square: each
 * region of the layout is made fully clear or fully opaque at the 26/255
 * cutoff, and a base body region more than 60% clear is filled in whole
 * unless the skin brings its own model. Persona skins keep their alpha.
 */
void normalizeClassicAlpha(std::vector<uint8_t>& pixels, uint32_t width, uint32_t height, const SerializedSkin& skin)
{
    if (skin.mPersona || width != height || (width != ClassicSkinSide && width != ClassicSkinSide * 2) || pixels.size() < size_t(width) * height * 4) {
        return;
    }
    std::string name = world::skinGeometryName(skin.mSkinResourcePatch);
    bool customGeometry = !name.empty() && name != "geometry.humanoid.custom" && name != "geometry.humanoid.customSlim";
    uint32_t scale = width / ClassicSkinSide;
    struct Region {
        std::array<uint32_t, 4> bounds;
        bool protect;
    };
    static constexpr Region Regions[] = {
        { { 0, 8, 32, 16 }, true },
        { { 8, 0, 24, 8 }, false },
        { { 0, 20, 56, 32 }, true },
        { { 4, 16, 12, 20 }, false },
        { { 20, 16, 36, 20 }, false },
        { { 44, 16, 52, 20 }, false },
        { { 16, 52, 48, 64 }, true },
        { { 20, 48, 28, 64 }, false },
        { { 36, 48, 44, 64 }, false },
        { { 32, 0, 64, 32 }, false },
        { { 0, 32, 16, 48 }, false },
        { { 16, 32, 40, 48 }, false },
        { { 40, 32, 56, 48 }, false },
        { { 0, 48, 16, 64 }, false },
        { { 48, 48, 64, 64 }, false },
    };
    for (const Region& region : Regions) {
        uint32_t x0 = region.bounds[0] * scale;
        uint32_t y0 = region.bounds[1] * scale;
        uint32_t x1 = region.bounds[2] * scale;
        uint32_t y1 = region.bounds[3] * scale;
        size_t clear = 0;
        for (uint32_t y = y0; y < y1; ++y) {
            for (uint32_t x = x0; x < x1; ++x) {
                uint8_t& alpha = pixels[(size_t(y) * width + x) * 4 + 3];
                clear += alpha < ClassicAlphaCutoff ? 1 : 0;
                alpha = alpha < ClassicAlphaCutoff ? 0 : 255;
            }
        }
        if (!region.protect || customGeometry || float(clear) / float((x1 - x0) * (y1 - y0)) <= ClassicCoverageLimit) {
            continue;
        }
        for (uint32_t y = y0; y < y1; ++y) {
            for (uint32_t x = x0; x < x1; ++x) {
                pixels[(size_t(y) * width + x) * 4 + 3] = 255;
            }
        }
    }
}

/**
 * A skin image as sent, or an empty one when its size and bytes disagree.
 */
SkinImage skinImage(const SkinImageData& image)
{
    SkinImage out;
    if (image.mWidth <= 0 || image.mHeight <= 0 || image.mData.size() != size_t(image.mWidth) * size_t(image.mHeight) * 4) {
        return out;
    }
    out.width = uint32_t(image.mWidth);
    out.height = uint32_t(image.mHeight);
    out.pixels.assign(reinterpret_cast<const uint8_t*>(image.mData.data()), reinterpret_cast<const uint8_t*>(image.mData.data()) + image.mData.size());
    return out;
}

/**
 * The animation sheets a skin sends that the player renderer draws, one per
 * kind with the last one sent winning, each with the model its resource
 * patch names for that kind. A sheet with a bad size or frame count, or
 * without its model, is left out.
 */
std::vector<SkinAnimationUpload> skinAnimations(const SerializedSkin& skin)
{
    std::vector<SkinAnimationUpload> out;
    for (const SkinAnimationData& animation : skin.mAnimations) {
        const char* key = nullptr;
        switch (animation.mTextureType) {
        case 1:
            key = "animated_face";
            break;
        case 2:
            key = "animated_32x32";
            break;
        case 3:
            key = "animated_128x128";
            break;
        default:
            break;
        }
        if (!key) {
            continue;
        }
        SkinImage image = skinImage(animation.mImage);
        float frames = animation.mFrames;
        if (image.empty() || !std::isfinite(frames) || frames < 1.0f || frames > float(image.height) || frames != std::floor(frames)) {
            continue;
        }
        std::shared_ptr<const world::EntityRig> rig = world::buildSkinRig(skin.mGeometryData, skin.mSkinResourcePatch, nullptr, key);
        if (!rig) {
            continue;
        }
        SkinAnimationKind kind = static_cast<SkinAnimationKind>(animation.mTextureType);
        std::erase_if(out, [kind](const SkinAnimationUpload& known) {
            return known.kind == kind;
        });
        SkinAnimationUpload& added = out.emplace_back();
        added.image = std::move(image);
        added.rig = std::move(rig);
        added.kind = kind;
        added.frames = uint32_t(frames);
        added.blinking = animation.mExpressionType == 1;
    }
    return out;
}

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
        applyActorHitboxes(entry, actor.hitboxes);
        applyActorAimAssist(entry, actor.aimAssistIndices);
        applyCrystalBeamTarget(entry, actor.crystalBeamTarget);
        switch (entry.mId) {
        case 0:
            if ((actor.flags[0] ^ static_cast<uint64_t>(entry.mLongValue)) & 1u) actor.fireChangedAt = secondsNow();
            actor.flags[0] = static_cast<uint64_t>(entry.mLongValue);
            break;
        case 92:
            actor.flags[1] = static_cast<uint64_t>(entry.mLongValue);
            break;
        case 2:
            if (entry.mFormat == EntityDataFormat::Int) actor.variant = entry.mIntValue;
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
        case 141:
            if (entry.mFormat == EntityDataFormat::Long) actor.fireworkShooterId = entry.mLongValue;
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
        static constexpr std::pair<int, const char*> Queries[] = {
            { 1, "structural_integrity" }, { 19, "swell_amount" }, { 21, "swelling_dir" },
            { 23, "is_carrying_block" }, { 48, "invulnerable_ticks" }, { 55, "fuse_time" },
            { 89, "sit_amount" }, { 93, "lie_amount" }, { 101, "trade_tier" },
        };
        if (entry.mId == 16 && entry.mFormat == EntityDataFormat::Long) actor.horseFlags = static_cast<uint64_t>(entry.mLongValue);
        if (entry.mId == 6 && entry.mFormat == EntityDataFormat::Long) actor.animationQueries["has_target"] = entry.mLongValue != 0 && entry.mLongValue != -1 ? 1.0 : 0.0;
        if (entry.mId == 15 && entry.mFormat == EntityDataFormat::Int && actor.identifier == "minecraft:xp_orb") {
            static constexpr int Bounds[] = { 2, 6, 16, 36, 72, 148, 306, 616, 1236, 2476 };
            actor.animationQueries["texture_frame_index"] = std::lower_bound(std::begin(Bounds), std::end(Bounds), entry.mIntValue) - std::begin(Bounds);
        }
        for (const auto& [id, name] : Queries) {
            if (entry.mId != id) continue;
            double value = 0.0;
            switch (entry.mFormat) {
            case EntityDataFormat::Byte: value = entry.mByteValue; break;
            case EntityDataFormat::Short: value = entry.mShortValue; break;
            case EntityDataFormat::Int: value = entry.mIntValue; break;
            case EntityDataFormat::Long: value = static_cast<double>(entry.mLongValue); break;
            case EntityDataFormat::Float: value = entry.mFloatValue; break;
            default: continue;
            }
            if (!std::isfinite(value)) continue;
            if (id == 19) value = std::max(value / 28.0, 0.0);
            if (id == 23) value = value != 0.0 ? 1.0 : 0.0;
            actor.animationQueries[name] = value;
        }
    }
}

}

std::vector<SkinUpload> Session::takeSkinUploads()
{
    std::unique_lock<std::mutex> guard(mutex, std::try_to_lock);
    if (!guard.owns_lock()) return {};
    std::vector<SkinUpload> uploads = std::move(pendingSkins);
    pendingSkins.clear();
    return uploads;
}

/**
 * Spawn packets give feet positions; player movement packets give eye positions.
 * Keep the last network position separate from the tick interpolation so
 * omitted delta-packet coordinates come from the last packet, not the pose.
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
    if (actor->second.identifier == "minecraft:arrow" && onGround && !actor->second.onGround && !teleport)
        actor->second.projectileShakeTicks = 7;
    actor->second.onGround = onGround;
    if (world::projectileEntity(actor->second.identifier)) {
        auto& view = actor->second;
        view.projectileTarget = { x, y, z };
        view.projectileTargetTurn = { yaw, headYaw, pitch };
        view.projectileTicksRemaining = teleport ? 0 : 3;
        if (!teleport) return;
        view.projectilePrevious = view.projectileTarget;
        view.projectilePositionDelta = {};
        view.projectileShakeTicks = 0;
        view.projectilePreviousTurn = view.projectileTargetTurn;
        view.projectileCurrentTurn = view.projectileTargetTurn;
        view.projectileTickTime = secondsNow();
    }
    actor->second.x = x;
    actor->second.y = y - (!feetPosition && actor->second.identifier == "minecraft:player" ? session::PlayerEyeHeight : 0.0);
    if (actor->second.identifier == "minecraft:falling_block" || actor->second.identifier == "minecraft:tnt") actor->second.y -= 0.49;
    actor->second.z = z;
    actor->second.yaw = yaw;
    actor->second.headYaw = headYaw;
    actor->second.pitch = pitch;
    if (!world::projectileEntity(actor->second.identifier)) {
        auto position = std::array<double, 3> { actor->second.x, actor->second.y, actor->second.z };
        auto turn = std::array<float, 3> { yaw, headYaw, pitch };
        if (teleport || !actor->second.interpolation.initialized) {
            actorMoveQueues.erase(runtimeId);
            actor->second.interpolation.retarget(position, turn, true);
        } else {
            actorMoveQueues[runtimeId].push(position, turn);
        }
    }
}

void Session::tickActors(double tickTime)
{
    for (auto& [id, actor] : actors) {
        if (world::projectileEntity(actor.identifier)) continue;
        if (auto queue = actorMoveQueues.find(id); queue != actorMoveQueues.end()) {
            ActorMoveTarget sample;
            if (queue->second.pop(sample)) actor.interpolation.retarget(sample.position, sample.turn, false);
        }
        actor.interpolation.tick(tickTime);
    }
    std::erase_if(actorMoveQueues, [&](const auto& entry) { return !actors.contains(entry.first); });
}

/**
 * Replaces a remote actor's velocity without moving it. An arrow that has
 * not turned yet takes its launch yaw and pitch from the motion, shown at
 * once rather than glided into.
 */
void Session::setActorMotion(uint64_t runtimeId, float x, float y, float z)
{
    auto actor = actors.find(runtimeId);
    if (actor == actors.end()) {
        return;
    }
    ActorView& view = actor->second;
    view.velocity = { x, y, z };
    if (view.identifier != "minecraft:arrow" || view.yaw != 0.0f || view.pitch != 0.0f) {
        return;
    }
    constexpr float Degrees = 180.0f / 3.14159265f;
    view.yaw = std::atan2(x, z) * Degrees;
    view.pitch = std::atan2(y, std::hypot(x, z)) * Degrees;
    view.projectilePreviousTurn[0] = view.yaw;
    view.projectilePreviousTurn[2] = view.pitch;
    view.projectileCurrentTurn = { view.yaw, view.headYaw, view.pitch };
    if (!view.projectileTicksRemaining) view.projectileTargetTurn = { view.yaw, view.headYaw, view.pitch };
    ++view.launchTurns;
}

void Session::tickProjectiles(double tickTime)
{
    for (auto& [id, view] : actors) {
        if (!world::projectileEntity(view.identifier)) continue;
        view.projectilePrevious = { view.x, view.y, view.z };
        view.projectilePreviousTurn = { view.yaw, view.headYaw, view.pitch };
        view.projectileCurrentTurn = view.projectilePreviousTurn;
        if (view.projectileTickTime == 0.0 && !view.projectileTicksRemaining) {
            view.projectileTarget = view.projectilePrevious;
            view.projectileTargetTurn = view.projectilePreviousTurn;
        }
        view.projectileTickTime = tickTime;
        if (view.projectileShakeTicks) --view.projectileShakeTicks;
        view.projectilePositionDelta = {};
        if (!view.projectileTicksRemaining) continue;
        const double divisor = view.projectileTicksRemaining--;
        std::array<double, 3> position;
        std::array<float, 3> turn;
        for (size_t axis = 0; axis < 3; ++axis) {
            position[axis] = view.projectilePrevious[axis] + (view.projectileTarget[axis] - view.projectilePrevious[axis]) / divisor;
            turn[axis] = view.projectilePreviousTurn[axis] + std::remainder(view.projectileTargetTurn[axis] - view.projectilePreviousTurn[axis], 360.0f) / float(divisor);
        }
        view.x = position[0]; view.y = position[1]; view.z = position[2];
        for (size_t axis = 0; axis < 3; ++axis) view.projectilePositionDelta[axis] = position[axis] - view.projectilePrevious[axis];
        view.yaw = turn[0]; view.headYaw = turn[1]; view.pitch = turn[2];
        view.projectileCurrentTurn = turn;
    }
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
    uint64_t print = skinPrint(skin);
    if (auto uploaded = uploadedSkinPrints.find(uuid); uploaded != uploadedSkinPrints.end() && uploaded->second == print && skinByUuid.contains(uuid)) {
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
 * Puts a remembered skin in a skin slot at its own size, with its cape,
 * animation sheets and model, and points the entities wearing it at that
 * slot. The slim arms come from the model the resource patch names.
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
    bool slim = util::lowercase(world::skinGeometryName(skin.mSkinResourcePatch)) == "geometry.humanoid.customslim";
    slotOwners[slot] = uuid;
    skinByUuid[uuid] = { slot, slim };
    uploadedSkinPrints[uuid] = skinPrint(skin);

    SkinUpload upload;
    upload.slot = slot;
    upload.rig = world::buildSkinRig(skin.mGeometryData, skin.mSkinResourcePatch, assets ? assets->geometryCatalog() : nullptr);
    upload.cape = skinImage(skin.mCapeData);
    upload.animations = skinAnimations(skin);
    uint32_t width = uint32_t(image.mWidth);
    uint32_t height = uint32_t(image.mHeight);
    bool squared = false;
    // Legacy 64x32 skins often come with geometry laid out for a square texture, which
    // then reads the missing lower half, so they are squared unless the geometry is flat too.
    bool squareGeometry = !upload.rig || upload.rig->textureAspect > 0.75f;
    if (squareGeometry && !skin.mPersona && width == height * 2 && width % 64 == 0) {
        upload.image.pixels = squareLegacySkin(image);
        height = width;
        squared = true;
    } else {
        const uint8_t* pixels = reinterpret_cast<const uint8_t*>(image.mData.data());
        upload.image.pixels.assign(pixels, pixels + size_t(width) * height * 4);
    }
    upload.image.width = width;
    upload.image.height = height;
    normalizeClassicAlpha(upload.image.pixels, width, height, skin);
    debugLog("skin " + uuid + " slot " + std::to_string(slot) + " image " + std::to_string(image.mWidth) + "x" + std::to_string(image.mHeight)
        + " patch " + skin.mSkinResourcePatch + " geometry bytes " + std::to_string(skin.mGeometryData.size())
        + (upload.rig ? " rig quads " + std::to_string(upload.rig->quads.size()) + " aspect " + std::to_string(upload.rig->textureAspect) : std::string(" no rig"))
        + (squared ? " squared" : "") + " animations " + std::to_string(upload.animations.size()) + (upload.cape.empty() ? "" : " cape"));
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
    uploadedSkinPrints.erase(uuid);
}

}
