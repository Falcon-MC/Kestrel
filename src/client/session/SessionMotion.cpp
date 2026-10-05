#include "client/session/SessionData.h"

#include "Network/BedrockConnection.h"
#include "Protocol/Packets/CorrectPlayerMovePredictionPacket.h"
#include "Protocol/Packets/MovePlayerPacket.h"
#include "Protocol/Packets/NetworkStackLatencyPacket.h"
#include "Protocol/Packets/PlayerAuthInputPacket.h"
#include "Protocol/Packets/RespawnPacket.h"
#include "Protocol/Packets/SetActorMotionPacket.h"
#include "Protocol/Packets/UpdateAbilitiesPacket.h"
#include "Protocol/Packets/UpdateAttributesPacket.h"
#include "client/DebugLog.h"
#include "world/BlockCollisions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace kestrel {

namespace {

using session::EyeHeight;
using session::ScaleDataId;
using session::enchantmentLevel;

constexpr double TickSeconds = 0.05;
constexpr int32_t MaxCatchUpTicks = 8;
constexpr double MaxCatchUpSeconds = 1.0;
constexpr size_t MotionHistoryTicks = 32;
constexpr uint64_t NearbyRefreshTicks = 10;
constexpr uint64_t LoadedRefreshTicks = 20;
constexpr int32_t FlagsDataId = 0;
constexpr int SprintingFlag = 3;
constexpr int NoAiFlag = 16;
constexpr int HasGravityFlag = 49;
constexpr int32_t JumpBoostEffect = 8;
constexpr int32_t LevitationEffect = 24;
// Servers read the echoed probe time back in millions of the time they sent.
constexpr uint64_t LatencyEchoScale = 1000000;
constexpr int32_t SlowFallingEffect = 27;
constexpr int32_t WeavingEffect = 33;
constexpr int16_t DepthStriderEnchantment = 7;
constexpr int16_t SoulSpeedEnchantment = 36;
constexpr int16_t SwiftSneakEnchantment = 37;
constexpr uint32_t FlyingAbility = 1u << 9;
constexpr uint32_t MayFlyAbility = 1u << 10;
constexpr uint32_t NoClipAbility = 1u << 17;

}

void Session::setMotionInput(const MotionInput& input)
{
    std::lock_guard<std::mutex> guard(motionInputMutex);
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
        if (const world::CollisionState* custom = nullptr; assets->customCollision(value, ids.hashed, ids.sequential.get(), custom)) {
            return custom;
        }
        const world::BlockVisual& visual = assets->visual(value, ids.hashed, ids.sequential.get());
        return visual.flags & world::FlagAir ? nullptr : table.fullBlock();
    };
    cell.primary = resolve(0);
    cell.extra = resolve(1);
    return cell;
}

/**
 * Whether every column and sub-chunk the player can touch this tick has
 * arrived: a sub-chunk still asked for is unknown ground, not air, so the
 * player must not fall through it.
 */
bool Session::motionAreaLoaded(const MotionVector& feet)
{
    for (float coordinate : { feet.x, feet.y, feet.z }) {
        if (!std::isfinite(coordinate) || coordinate < -1.0e9f || coordinate > 1.0e9f) return false;
    }
    int32_t minX = static_cast<int32_t>(std::floor(feet.x - 1.0f)) >> 4;
    int32_t maxX = static_cast<int32_t>(std::floor(feet.x + 1.0f)) >> 4;
    int32_t minY = static_cast<int32_t>(std::floor(feet.y - 2.0f)) >> 4;
    int32_t maxY = static_cast<int32_t>(std::floor(feet.y + 3.0f)) >> 4;
    int32_t minZ = static_cast<int32_t>(std::floor(feet.z - 1.0f)) >> 4;
    int32_t maxZ = static_cast<int32_t>(std::floor(feet.z + 1.0f)) >> 4;
    for (int32_t x = minX; x <= maxX; ++x) {
        for (int32_t z = minZ; z <= maxZ; ++z) {
            if (!world.store().isLoaded({ motionDimension, x, z })) {
                if (world.settled()) {
                    continue;
                }
                return false;
            }
            for (int32_t y = minY; y <= maxY; ++y) {
                if (world.subChunkPending({ motionDimension, x, y, z })) {
                    return false;
                }
            }
        }
    }
    return true;
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
            answer.mTimestamp = latency->mTimestamp > std::numeric_limits<uint64_t>::max() / LatencyEchoScale ? std::numeric_limits<uint64_t>::max() : latency->mTimestamp * LatencyEchoScale;
            answer.mFromServer = false;
            transmit(answer);
            connection->flush();
        }
    } else if (auto move = std::dynamic_pointer_cast<MovePlayerPacket>(packet)) {
        if (static_cast<uint64_t>(move->mRuntimeActorId) != localRuntimeId) {
            return;
        }
        MotionVector was = motion.position();
        MotionVector target { move->mPosition.x, move->mPosition.y - EyeHeight, move->mPosition.z };
        bool hard = move->mMode == MovePlayerMode::Teleport || move->mMode == MovePlayerMode::Respawn || !motion.initialized();
        debugLog("server moved player at tick " + std::to_string(clientTick) + " mode " + std::to_string(static_cast<int>(move->mMode)) + " from " + std::to_string(was.x) + " " + std::to_string(was.y) + " " + std::to_string(was.z)
            + " to " + std::to_string(target.x) + " " + std::to_string(target.y) + " " + std::to_string(target.z));
        if (!hard) {
            replayCorrection(static_cast<uint64_t>(std::max<int64_t>(move->mTick, 0)), target, nullptr, move->mOnGround);
            return;
        }
        motionHistory.clear();
        serverMotions.clear();
        motion.teleport(target);
        teleportHandled = move->mMode == MovePlayerMode::Teleport;
        motionStarted = false;
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
        serverMotions.clear();
        motion.reset(feet);
        teleportHandled = false;
        motionStarted = false;
        debugLog("respawn at " + std::to_string(feet.x) + " " + std::to_string(feet.y) + " " + std::to_string(feet.z));
        bool dead = false;
        {
            std::lock_guard<std::mutex> guard(mutex);
            dead = current.dead;
        }
        if (connection && dead) {
            RespawnPacket ready;
            ready.mPosition = respawn->mPosition;
            ready.mState = RespawnPacket::State::ClientReady;
            ready.mRuntimeActorId = localRuntimeId;
            transmit(ready);
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
        MotionVector delta { correction->mDelta.x, correction->mDelta.y, correction->mDelta.z };
        replayCorrection(correction->mTick, { correction->mPosition.x, correction->mPosition.y - EyeHeight, correction->mPosition.z }, &delta, correction->mOnGround);
    } else if (auto push = std::dynamic_pointer_cast<SetActorMotionPacket>(packet)) {
        if (!std::isfinite(push->mMotion.x) || !std::isfinite(push->mMotion.y) || !std::isfinite(push->mMotion.z)) {
            return;
        }
        if (push->mRuntimeActorId != localRuntimeId) {
            setActorMotion(static_cast<uint64_t>(push->mRuntimeActorId), push->mMotion.x, push->mMotion.y, push->mMotion.z);
            return;
        }
        MotionVector impulse { push->mMotion.x, push->mMotion.y, push->mMotion.z };
        if (push->mTick == 0) {
            motion.knockback(impulse);
            return;
        }
        if (serverMotions.size() >= MotionHistoryTicks) {
            serverMotions.pop_front();
        }
        serverMotions.push_back({ push->mTick, impulse });
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
        if (abilities->mAbilities.mUniqueActorId != localUniqueId) {
            return;
        }
        bool mayFly = false;
        bool flying = false;
        bool noClip = false;
        float flySpeed = 0.0f;
        float verticalFlySpeed = 0.0f;
        for (const AbilityLayer& layer : abilities->mAbilities.mAbilityLayers) {
            if (layer.mAbilitiesSet & MayFlyAbility) {
                mayFly = layer.mAbilityValues & MayFlyAbility;
            }
            if (layer.mAbilitiesSet & FlyingAbility) {
                flying = layer.mAbilityValues & FlyingAbility;
            }
            if (layer.mAbilitiesSet & NoClipAbility) {
                noClip = layer.mAbilityValues & NoClipAbility;
            }
            if (std::isfinite(layer.mFlySpeed) && layer.mFlySpeed > 0.0f) {
                flySpeed = layer.mFlySpeed;
            }
            if (std::isfinite(layer.mVerticalFlySpeed) && layer.mVerticalFlySpeed > 0.0f) {
                verticalFlySpeed = layer.mVerticalFlySpeed;
            }
        }
        motion.setAbilities(mayFly, flying, noClip, flySpeed, verticalFlySpeed);
        std::lock_guard<std::mutex> guard(mutex);
        current.player.mayFly = mayFly;
        current.player.operatorCommands = abilities->mAbilities.mCommandPermission > 0;
    }
}

/**
 * Runs every movement tick that fell due since the last call, 50 ms apart,
 * catching up to eight ticks at once after a stall; ticks further behind
 * than that are dropped.
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
    if (now - nextMotionTick > MaxCatchUpSeconds) {
        nextMotionTick = now - TickSeconds * MaxCatchUpTicks;
    }
    int32_t due = 0;
    while (now >= nextMotionTick) {
        nextMotionTick += TickSeconds;
        ++due;
    }
    int32_t run = std::min(due, MaxCatchUpTicks);
    for (int32_t index = 0; index < run; ++index) {
        tickProjectiles(nextMotionTick - TickSeconds * (due - index));
        // stamp the tick with when it was due, not when this loop got around to it, or the camera hitches by the delay
        runMotionTick(nextMotionTick - TickSeconds * (run - index));
    }
    if (due > 0) {
        sendMapRequests();
    }
}

/**
 * Runs one movement tick once the player has spawned and the ground around
 * it has loaded, and sends the server that tick as a PlayerAuthInput: eye
 * position, rotation, the processed movement vector, the displacement the
 * tick resolved to and the input flags the server replays it with. While the
 * ground is missing the player is held in place and the tick says so.
 */
void Session::runMotionTick(double now)
{
    MotionInput input;
    {
        std::lock_guard<std::mutex> guard(motionInputMutex);
        input = motionInput;
    }
    {
        std::lock_guard<std::mutex> guard(mutex);
        input.usingItem = itemInUse.has_value();
        input.raining = current.rainLevel > 0.0f;
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
    const ItemStack& chest = inventoryModel.slots[inventory::Armor + 1];
    const ItemStack& legs = inventoryModel.slots[inventory::Armor + 2];
    const ItemStack& feetSlot = inventoryModel.slots[inventory::Armor + 3];
    input.elytra = !chest.isAir() && chest.mDefinition->getIdentifier() == "minecraft:elytra";
    input.depthStrider = enchantmentLevel(feetSlot, DepthStriderEnchantment);
    input.soulSpeed = enchantmentLevel(feetSlot, SoulSpeedEnchantment);
    input.swiftSneak = enchantmentLevel(legs, SwiftSneakEnchantment);
    input.riptide = pendingRiptide.exchange(0);

    bool waitingForWorld = false;
    if (!motionStarted) {
        if (!motionAreaLoaded(motion.position())) {
            waitingForWorld = true;
        } else {
            motionStarted = true;
            MotionVector start = motion.position();
            debugLog("movement started at " + std::to_string(start.x) + " " + std::to_string(start.y) + " " + std::to_string(start.z));
        }
    }

    MotionVector before = motion.position();
    MotionTick tick;
    bool frozen = waitingForWorld || !motionAreaLoaded(before);
    if (!frozen) {
        std::vector<std::pair<std::array<int32_t, 3>, std::shared_ptr<const world::SubChunk>>> subChunks;
        PlayerMotion::CellLookup lookup = [this, &subChunks](int32_t x, int32_t y, int32_t z) {
            std::array<int32_t, 3> key { x >> 4, y >> 4, z >> 4 };
            auto found = std::find_if(subChunks.begin(), subChunks.end(), [&](const auto& entry) { return entry.first == key; });
            std::shared_ptr<const world::SubChunk> sub;
            if (found != subChunks.end()) sub = found->second;
            else {
                sub = world.store().subChunk({ motionDimension, key[0], key[1], key[2] });
                if (subChunks.size() < 64) subChunks.emplace_back(key, sub);
            }
            if (!sub || !assets) return MotionCell {};
            const auto& table = world::BlockCollisions::shared();
            auto resolve = [&](uint32_t layer) -> const world::CollisionState* {
                uint32_t value = sub->runtimeId(layer, uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15));
                if (value == world::ImplicitAir) return nullptr;
                if (auto state = table.find(assets->stateHash(value, ids.hashed, ids.sequential.get()))) return state;
                if (const world::CollisionState* custom = nullptr; assets->customCollision(value, ids.hashed, ids.sequential.get(), custom)) return custom;
                return assets->visual(value, ids.hashed, ids.sequential.get()).flags & world::FlagAir ? nullptr : table.fullBlock();
            };
            return MotionCell { resolve(0), resolve(1) };
        };
        for (const ServerMotion& impulse : serverMotions) {
            if (impulse.tick == clientTick + 1) {
                motion.knockback(impulse.velocity);
            }
        }
        tick = motion.step(input, lookup);
        playMotionSounds(tick, before);
    } else {
        motion.hold();
        tick.position = before;
        tick.sneaking = motion.sneaking();
        tick.onGround = motion.grounded();
    }

    float yaw = std::remainder(input.yaw, 360.0f);
    float pitch = std::clamp(input.pitch, -90.0f, 90.0f);
    float rawSideways = frozen ? 0.0f : std::clamp(input.sideways, -1.0f, 1.0f);
    float rawForward = frozen ? 0.0f : std::clamp(input.forward, -1.0f, 1.0f);
    PlayerAuthInputPacket packet;
    float eyeY = tick.position.y + EyeHeight;
    packet.mPosition = Vector3f(tick.position.x, eyeY, tick.position.z);
    packet.mRotation = Vector3f(pitch, yaw, yaw);
    packet.mMotionX = tick.moveSideways;
    packet.mMotionY = tick.moveForward;
    packet.mAnalogMoveVectorX = 0.0f;
    packet.mAnalogMoveVectorY = 0.0f;
    float rawLength = std::sqrt(rawSideways * rawSideways + rawForward * rawForward);
    float rawScale = rawLength > 1.0f ? 1.0f / rawLength : 1.0f;
    packet.mRawMoveVectorX = rawSideways * rawScale;
    packet.mRawMoveVectorY = rawForward * rawScale;
    packet.mDelta = Vector3f(tick.velocity.x, tick.velocity.y, tick.velocity.z);
    packet.mTick = static_cast<int64_t>(++clientTick);
    packet.mInputMode = PlayerInputMode::Mouse;
    packet.mPlayMode = PlayerClientPlayMode::Normal;
    packet.mInputInteractionModel = PlayerInputInteractionModel::Crosshair;
    packet.mInteractRotationX = pitch;
    packet.mInteractRotationY = yaw;
    constexpr float Radians = 3.14159265f / 180.0f;
    packet.mCameraOrientation = Vector3f(-std::sin(yaw * Radians) * std::cos(pitch * Radians), -std::sin(pitch * Radians), std::cos(yaw * Radians) * std::cos(pitch * Radians));
    tickEye = { double(packet.mPosition.x), double(packet.mPosition.y), double(packet.mPosition.z) };
    tickDirection = { packet.mCameraOrientation.x, packet.mCameraOrientation.y, packet.mCameraOrientation.z };
    tickSneaking = tick.sneaking;
    tickSpeed = std::sqrt(double(tick.movement.x) * tick.movement.x + double(tick.movement.y) * tick.movement.y + double(tick.movement.z) * tick.movement.z) / TickSeconds;

    auto flag = [&](PlayerAuthInputData value) {
        packet.mInputData.push_back(static_cast<int32_t>(value));
    };
    if (rawForward > 0.0f) {
        flag(PlayerAuthInputData::Up);
    }
    if (rawForward < 0.0f) {
        flag(PlayerAuthInputData::Down);
    }
    if (rawSideways > 0.0f) {
        flag(PlayerAuthInputData::Left);
    }
    if (rawSideways < 0.0f) {
        flag(PlayerAuthInputData::Right);
    }
    if (rawForward > 0.0f && rawSideways > 0.0f) {
        flag(PlayerAuthInputData::UpLeft);
    }
    if (rawForward > 0.0f && rawSideways < 0.0f) {
        flag(PlayerAuthInputData::UpRight);
    }
    if (rawForward < 0.0f && rawSideways > 0.0f) {
        flag(PlayerAuthInputData::DownLeft);
    }
    if (rawForward < 0.0f && rawSideways < 0.0f) {
        flag(PlayerAuthInputData::DownRight);
    }
    bool jumpPressed = input.jump && !lastMotionInput.jump;
    if (input.jump) {
        flag(PlayerAuthInputData::JumpDown);
        flag(PlayerAuthInputData::JumpCurrentRaw);
        if (jumpPressed) {
            flag(PlayerAuthInputData::JumpPressedRaw);
        }
    } else if (lastMotionInput.jump) {
        flag(PlayerAuthInputData::JumpReleasedRaw);
    }
    if (tick.startedJump) {
        flag(PlayerAuthInputData::StartJumping);
    }
    if (input.jump) {
        flag(PlayerAuthInputData::Jumping);
    }
    if (input.sneak) {
        flag(PlayerAuthInputData::SneakCurrentRaw);
        if (!lastMotionInput.sneak) {
            flag(PlayerAuthInputData::SneakPressedRaw);
        }
    } else if (lastMotionInput.sneak) {
        flag(PlayerAuthInputData::SneakReleasedRaw);
    }
    if (tick.sneaking) {
        flag(PlayerAuthInputData::Sneaking);
        flag(PlayerAuthInputData::SneakDown);
    }
    if (tick.startSneaking) {
        flag(PlayerAuthInputData::StartSneaking);
    }
    if (tick.stopSneaking) {
        flag(PlayerAuthInputData::StopSneaking);
    }
    if (tick.forcedSneak) {
        flag(PlayerAuthInputData::PersistSneak);
    }
    if (input.sprint || tick.sprinting) {
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
    if (tick.flying && input.jump) {
        flag(PlayerAuthInputData::Ascend);
    }
    if (tick.flying && input.sneak) {
        flag(PlayerAuthInputData::Descend);
    }
    if (tick.startSwimming) {
        flag(PlayerAuthInputData::StartSwimming);
    }
    if (tick.stopSwimming) {
        flag(PlayerAuthInputData::StopSwimming);
    }
    if (tick.startGliding) {
        flag(PlayerAuthInputData::StartGliding);
    }
    if (tick.startSpinAttack) {
        flag(PlayerAuthInputData::StartSpinAttack);
    }
    if (tick.stopSpinAttack) {
        flag(PlayerAuthInputData::StopSpinAttack);
    }
    if (tick.stopGliding) {
        flag(PlayerAuthInputData::StopGliding);
    }
    if (tick.startCrawling) {
        flag(PlayerAuthInputData::StartCrawling);
    }
    if (tick.stopCrawling) {
        flag(PlayerAuthInputData::StopCrawling);
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
    if (missedSwing.exchange(false)) {
        flag(PlayerAuthInputData::MissedSwing);
    }
    if (useRequested.exchange(false)) {
        lastItemRepeatTick = clientTick;
        interact(true);
    } else {
        tickHeldUse();
    }
    tickBreaking(packet, tick);
    tickItemUse(packet);
    tickCracks();
    tickChestLids();
    tickFrameItems();
    transmit(packet);
    connection->flush();
    lastMotionInput = input;

    motion.anchor({ tick.position.x, eyeY - EyeHeight, tick.position.z });
    if (!frozen) {
        motionHistory.push_back({ clientTick, input, motion, tick.knockedBack, tick.knockback });
        while (motionHistory.size() > MotionHistoryTicks) {
            motionHistory.pop_front();
        }
    }
    MotionVector feet = motion.position();
    publishCameraBlocks();
    if (clientTick % 20 == 0) {
        char line[192];
        std::snprintf(line, sizeof(line), "tick %llu feet %.4f %.4f %.4f velocity %.4f %.4f %.4f ground %d jump %d sprint %d sneak %d", static_cast<unsigned long long>(clientTick), feet.x, feet.y, feet.z,
            tick.velocity.x, tick.velocity.y, tick.velocity.z, tick.onGround ? 1 : 0, tick.startedJump ? 1 : 0, tick.sprinting ? 1 : 0, tick.sneaking ? 1 : 0);
        debugLog(line);
    }
    if (clientTick % NearbyRefreshTicks == 0) {
        publishNearby();
    }
    if (clientTick % LoadedRefreshTicks == 0) {
        publishLoaded();
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
 * sent since then again with its own input and the knockback it took, so the
 * correction lands in the present instead of pulling the player back to where
 * they were. Without a velocity from the server, the one the tick ended with
 * is kept.
 */
void Session::replayCorrection(uint64_t tick, const MotionVector& position, const MotionVector* velocity, bool onGround)
{
    auto corrected = std::find_if(motionHistory.begin(), motionHistory.end(), [tick](const SentMotionTick& sent) {
        return sent.tick == tick;
    });
    if (corrected == motionHistory.end()) {
        motion.correct(position, velocity ? *velocity : motion.currentVelocity(), onGround);
        motionHistory.clear();
        serverMotions.clear();
        MotionVector feet = motion.position();
        std::lock_guard<std::mutex> guard(mutex);
        current.player.current = { feet.x, feet.y, feet.z };
        return;
    }
    MotionVector kept = velocity ? *velocity : corrected->after.currentVelocity();
    corrected->after.correct(position, kept, onGround);
    PlayerMotion replay = corrected->after;
    std::vector<std::pair<std::array<int32_t, 3>, std::shared_ptr<const world::SubChunk>>> subChunks;
    PlayerMotion::CellLookup lookup = [this, &subChunks](int32_t x, int32_t y, int32_t z) {
        std::array<int32_t, 3> key { x >> 4, y >> 4, z >> 4 };
        auto found = std::find_if(subChunks.begin(), subChunks.end(), [&](const auto& entry) { return entry.first == key; });
        std::shared_ptr<const world::SubChunk> sub;
        if (found != subChunks.end()) sub = found->second;
        else {
            sub = world.store().subChunk({ motionDimension, key[0], key[1], key[2] });
            if (subChunks.size() < 64) subChunks.emplace_back(key, sub);
        }
        if (!sub || !assets) return MotionCell {};
        const auto& table = world::BlockCollisions::shared();
        auto resolve = [&](uint32_t layer) -> const world::CollisionState* {
            uint32_t value = sub->runtimeId(layer, uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15));
            if (value == world::ImplicitAir) return nullptr;
            if (auto state = table.find(assets->stateHash(value, ids.hashed, ids.sequential.get()))) return state;
            if (const world::CollisionState* custom = nullptr; assets->customCollision(value, ids.hashed, ids.sequential.get(), custom)) return custom;
            return assets->visual(value, ids.hashed, ids.sequential.get()).flags & world::FlagAir ? nullptr : table.fullBlock();
        };
        return MotionCell { resolve(0), resolve(1) };
    };
    for (auto sent = std::next(corrected); sent != motionHistory.end(); ++sent) {
        replay.takeSettings(sent->after);
        for (const ServerMotion& impulse : serverMotions) {
            if (impulse.tick == sent->tick) {
                replay.knockback(impulse.velocity);
            }
        }
        subChunks.clear();
        MotionTick result = replay.step(sent->input, lookup);
        float eyeY = result.position.y + EyeHeight;
        replay.anchor({ result.position.x, eyeY - EyeHeight, result.position.z });
        sent->after = replay;
    }
    motionHistory.erase(motionHistory.begin(), corrected);
    replay.keepPendingKnockback(motion);
    replay.takeSettings(motion);
    motion = replay;
    MotionVector feet = motion.position();
    std::lock_guard<std::mutex> guard(mutex);
    current.player.current = { feet.x, feet.y, feet.z };
}

}
