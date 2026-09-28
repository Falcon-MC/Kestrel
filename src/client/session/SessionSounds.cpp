#include "SessionData.h"

#include "Protocol/Packets/LevelEventPacket.h"
#include "Protocol/Packets/LevelSoundEventPacket.h"
#include "Protocol/Packets/PlaySoundPacket.h"
#include "Protocol/Packets/StopSoundPacket.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

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
        std::array<int32_t, 3> cell { int32_t(std::floor(at.x)), int32_t(std::floor(at.y)), int32_t(std::floor(at.z)) };
        if ((request.name == "hit" || request.name == "break") && locallyBroken(cell)) {
            return;
        }
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

}
