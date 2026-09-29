#include "SessionData.h"

#include "Network/BedrockConnection.h"
#include "Protocol/Packets/CorrectPlayerMovePredictionPacket.h"
#include "Protocol/Packets/MovePlayerPacket.h"
#include "Protocol/Packets/NetworkStackLatencyPacket.h"
#include "Protocol/Packets/PlayerAuthInputPacket.h"
#include "Protocol/Packets/RespawnPacket.h"
#include "Protocol/Packets/SetActorMotionPacket.h"
#include "Protocol/Packets/SetPlayerGameTypePacket.h"
#include "Protocol/Packets/UpdateAbilitiesPacket.h"
#include "Protocol/Packets/UpdateAttributesPacket.h"
#include "client/DebugLog.h"
#include "world/BlockCollisions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kestrel {

namespace {

using session::EyeHeight;
using session::ScaleDataId;

constexpr double TickSeconds = 0.05;
constexpr size_t MotionHistoryTicks = 200;
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
        motionHistory.clear();
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
        motionHistory.clear();
        motion.reset(feet);
        debugLog("respawn at " + std::to_string(feet.x) + " " + std::to_string(feet.y) + " " + std::to_string(feet.z));
        bool dead = false;
        {
            std::lock_guard<std::mutex> guard(mutex);
            dead = current.dead;
        }
        if (connection && !dead) {
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
        replayCorrection(correction->mTick, { correction->mPosition.x, correction->mPosition.y - EyeHeight, correction->mPosition.z }, { correction->mDelta.x, correction->mDelta.y, correction->mDelta.z }, correction->mOnGround);
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
    if (tick.startSwimming) {
        flag(PlayerAuthInputData::StartSwimming);
    }
    if (tick.stopSwimming) {
        flag(PlayerAuthInputData::StopSwimming);
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
    tickBreaking(packet, tick);
    tickCracks();
    tickChestLids();
    connection->send(packet);
    tickHeldUse();
    connection->flush();
    lastMotionInput = input;

    motion.anchor({ tick.position.x, eyeY - EyeHeight, tick.position.z });
    motionHistory.push_back({ clientTick, input, motion });
    while (motionHistory.size() > MotionHistoryTicks) {
        motionHistory.pop_front();
    }
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
    current.player.onGround = tick.onGround;
    current.player.sprinting = tick.sprinting;
    current.player.swimming = tick.swimming;
    current.player.flying = tick.flying;
    current.player.movementSpeed = motion.speed();
}

/**
 * Takes the server's state for a tick already sent, then runs every tick
 * sent since then again with its own input, so the correction lands in the
 * present instead of pulling the player back to where they were.
 */
void Session::replayCorrection(uint64_t tick, const MotionVector& position, const MotionVector& velocity, bool onGround)
{
    auto corrected = std::find_if(motionHistory.begin(), motionHistory.end(), [tick](const SentMotionTick& sent) {
        return sent.tick == tick;
    });
    if (corrected == motionHistory.end()) {
        motion.correct(position, velocity, onGround);
        motionHistory.clear();
        return;
    }
    corrected->after.correct(position, velocity, onGround);
    PlayerMotion replay = corrected->after;
    PlayerMotion::CellLookup lookup = [this](int32_t x, int32_t y, int32_t z) {
        return motionCell(x, y, z);
    };
    for (auto sent = std::next(corrected); sent != motionHistory.end(); ++sent) {
        MotionTick result = replay.step(sent->input, lookup);
        float eyeY = result.position.y + EyeHeight;
        replay.anchor({ result.position.x, eyeY - EyeHeight, result.position.z });
        sent->after = replay;
    }
    motionHistory.erase(motionHistory.begin(), corrected);
    replay.keepPendingKnockback(motion);
    motion = replay;
    MotionVector feet = motion.position();
    std::lock_guard<std::mutex> guard(mutex);
    current.player.current = { feet.x, feet.y, feet.z };
}

}
