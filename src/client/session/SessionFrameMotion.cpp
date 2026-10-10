#include "client/session/SessionData.h"
#include "world/ItemInfo.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

void Session::publishMotionFeed()
{
    if (!frameDriven || !spawnInitialized || !assets) return;
    auto previous = sharedSnapshot()->motionFeed;
    double now = secondsNow();
    if (now < nextMotionFeed && previous && previous->tick == clientTick && previous->revision == movementRevision) return;
    nextMotionFeed = now + 1.0 / 60.0;
    auto feed = std::make_shared<MotionFeed>();
    feed->tick = clientTick;
    feed->revision = movementRevision;
    feed->hardRevision = hardMovementRevision;
    feed->locks = movementInputLocks;
    feed->glideStart = glideBoost.start;
    feed->glideEnd = glideBoost.end;
    feed->dolphinStart = dolphinBoost.start;
    feed->dolphinEnd = dolphinBoost.end;
    for (const auto& impulse : serverMotions) feed->impulses.emplace_back(impulse.tick, impulse.velocity);
    {
        std::lock_guard guard(mutex);
        feed->equipment.usingItem = itemInUse.has_value();
        feed->equipment.raining = current.rainLevel > 0.0f;
        int32_t jump = 0, levitation = 0;
        bool slow = false, weaving = false;
        for (const auto& effect : current.hud.effects) {
            if (effect.expires >= 0.0 && effect.expires < secondsNow()) continue;
            if (effect.id == 8) jump = effect.amplifier + 1;
            else if (effect.id == 24) levitation = effect.amplifier + 1;
            else if (effect.id == 27) slow = true;
            else if (effect.id == 33) weaving = true;
        }
        motion.setEffects(jump, levitation, slow, weaving);
        motion.setHunger(current.hud.hunger);
    }
    const ItemStack& chest = inventoryModel.slots[inventory::Armor + 1];
    const ItemStack& legs = inventoryModel.slots[inventory::Armor + 2];
    const ItemStack& boots = inventoryModel.slots[inventory::Armor + 3];
    if (!chest.isAir() && chest.mDefinition->getIdentifier() == "minecraft:elytra") {
        int damage = chest.mTag.isCompound() ? chest.mTag.getInt("Damage", chest.mDamage) : chest.mDamage;
        feed->equipment.elytra = damage < world::itemMaxDurability("minecraft:elytra") - 1;
    }
    feed->equipment.leatherBoots = !boots.isAir() && boots.mDefinition->getIdentifier() == "minecraft:leather_boots";
    feed->equipment.depthStrider = session::enchantmentLevel(boots, 7);
    feed->equipment.soulSpeed = session::enchantmentLevel(boots, 36);
    feed->equipment.swiftSneak = session::enchantmentLevel(legs, 37);
    if (auto target = modBreakHit()) feed->breakTarget = target->point;
    feed->seed.copyState(motion);
    publishLoaded();
    std::lock_guard guard(mutex);
    current.player.active = true;
    current.motionFeed = std::move(feed);
}

void Session::drainFrameMotion()
{
    if (!frameDriven) return;
    std::deque<PreparedMotion> outgoing;
    {
        std::lock_guard guard(frameOutgoingMutex);
        outgoing.swap(frameOutgoing);
    }
    for (const auto& prepared : outgoing) {
        if (prepared.revision != movementRevision || prepared.tick.number != clientTick + 1) continue;
        tickProjectiles(prepared.tick.due);
        tickActors(prepared.tick.due);
        runMotionTick(prepared.tick.due, &prepared.tick, &prepared.after, prepared.trace);
    }
    if (!outgoing.empty()) sendMapRequests();
}

PlayerView Session::advanceFrameMotion(const MotionInput& input, uint64_t trace, bool captured)
{
    auto snapshot = sharedSnapshot();
    PlayerView view = snapshot->player;
    const auto& feed = snapshot->motionFeed;
    const auto& blocks = snapshot->loaded;
    if (snapshot->state != SessionState::Joined || snapshot->dead || !feed || !blocks || !view.active) {
        frameJoin = 0;
        frameStaged.clear();
        std::lock_guard guard(motionInputMutex);
        bufferedMotionInput.reset();
        return view;
    }
    PlayerMotion::CellLookup lookup = [blocks](int32_t x, int32_t y, int32_t z) { return blocks->motionCell(x, y, z); };
    FrameMotion::AreaReady ready = [blocks](const MotionVector& feet) {
        for (float coordinate : { feet.x, feet.y, feet.z }) {
            if (!std::isfinite(coordinate) || std::abs(coordinate) > 1.0e9f) return false;
        }
        int32_t minX = int32_t(std::floor(feet.x - 1.0f)) >> 4;
        int32_t maxX = int32_t(std::floor(feet.x + 1.0f)) >> 4;
        int32_t minZ = int32_t(std::floor(feet.z - 1.0f)) >> 4;
        int32_t maxZ = int32_t(std::floor(feet.z + 1.0f)) >> 4;
        int32_t minY = int32_t(std::floor(feet.y - 2.0f)) >> 4;
        int32_t maxY = int32_t(std::floor(feet.y + 3.0f)) >> 4;
        for (int32_t x = minX; x <= maxX; ++x) {
            for (int32_t z = minZ; z <= maxZ; ++z) {
                if (!std::binary_search(blocks->columns.begin(), blocks->columns.end(), std::array<int32_t, 2>{x, z}) && !blocks->settled) return false;
                for (int32_t y = minY; y <= maxY; ++y) {
                    if (std::binary_search(blocks->pending.begin(), blocks->pending.end(), world::SubChunkKey{blocks->dimension, x, y, z})) return false;
                }
            }
        }
        return true;
    };
    const double now = secondsNow();
    frameMotion.acknowledge(feed->tick);
    if (frameJoin != snapshot->joinCount) {
        frameJoin = snapshot->joinCount;
        frameRevision = feed->revision;
        frameHardRevision = feed->hardRevision;
        frameMotion.reset(feed->seed, feed->tick, now);
    } else if (frameRevision != feed->revision) {
        frameRevision = feed->revision;
        if (frameHardRevision != feed->hardRevision) {
            frameHardRevision = feed->hardRevision;
            frameMotion.reset(feed->seed, feed->tick, now);
            std::lock_guard guard(motionInputMutex);
            bufferedMotionInput.reset(input);
        } else frameMotion.rebase(feed->seed, feed->tick);
        for (const auto& [tick, impulse] : feed->impulses) frameMotion.knockback(tick, impulse, lookup);
    }
    frameMotion.takeSettings(feed->seed);
    auto prepare = [&](MotionInput value, uint64_t tick) {
        if (feed->breakTarget && !value.rotationOverridden) {
            MotionVector feet = frameMotion.state().position();
            auto rotation = lookRotation({feet.x, feet.y + session::EyeHeight, feet.z}, *feed->breakTarget);
            value.yaw = rotation[0];
            value.pitch = rotation[1];
        }
        value.usingItem = feed->equipment.usingItem;
        value.raining = feed->equipment.raining;
        value.elytra = feed->equipment.elytra;
        value.leatherBoots = feed->equipment.leatherBoots;
        value.depthStrider = feed->equipment.depthStrider;
        value.soulSpeed = feed->equipment.soulSpeed;
        value.swiftSneak = feed->equipment.swiftSneak;
        value.glideBoost = tick >= feed->glideStart && tick < feed->glideEnd;
        value.dolphinBoost = tick >= feed->dolphinStart && tick < feed->dolphinEnd;
        uint32_t locks = feed->locks;
        if (locks & ((1u << 2) | (1u << 4))) value.forward = value.sideways = 0.0f, value.sprint = false;
        if ((value.forward > 0 && (locks & (1u << 9))) || (value.forward < 0 && (locks & (1u << 10)))) value.forward = 0;
        if ((value.sideways > 0 && (locks & (1u << 11))) || (value.sideways < 0 && (locks & (1u << 12)))) value.sideways = 0;
        if (locks & ((1u << 2) | (1u << 6))) value.jump = false;
        if (locks & ((1u << 2) | (1u << 5))) value.sneak = false;
        else if (value.swimDown && view.inWater) value.sneak = true;
        return value;
    };
    if (!captured) {
        std::lock_guard guard(motionInputMutex);
        bufferedMotionInput.reset(input);
    }
    bool queueAvailable;
    {
        std::lock_guard guard(frameOutgoingMutex);
        queueAvailable = frameOutgoing.size() <= 64 - FrameMotion::MaxFrameTicks;
    }
    if (queueAvailable) frameMotion.advance(now, [&](uint64_t tick) {
        std::lock_guard guard(motionInputMutex);
        MotionInput next = bufferedMotionInput.consume();
        if (!next.trace) next.trace = trace;
        next.riptide = pendingRiptide.exchange(0);
        return next;
    }, lookup, ready, [&](const FrameMotion::Tick& tick, const PlayerMotion& after) {
        inputLatency.tick(now - tick.due);
        frameStaged.push_back({tick, after, feed->revision, tick.input.trace});
    }, [&](MotionInput value, uint64_t tick) {
        inputLatency.mark(value.trace, InputLatency::Physics);
        return prepare(value, tick);
    });
    const auto& result = frameMotion.lastResult();
    MotionVector visual = frameMotion.interpolate();
    view.current = view.previous = {visual.x, visual.y, visual.z};
    view.tickTime = now;
    view.velocity = {result.velocity.x, result.velocity.y, result.velocity.z};
    view.onGround = result.onGround;
    view.sneaking = result.sneaking;
    view.sprinting = result.sprinting;
    view.swimming = result.swimming;
    view.gliding = result.gliding;
    view.flying = result.flying;
    view.inWater = result.inWater;
    view.inLava = result.inLava;
    view.onClimbable = result.onClimbable;
    view.bbWidth = result.width;
    view.bbHeight = result.height;
    return view;
}

void Session::submitFrameMotion()
{
    if (frameStaged.empty()) return;
    std::lock_guard guard(frameOutgoingMutex);
    for (auto& prepared : frameStaged) frameOutgoing.push_back(std::move(prepared));
    frameStaged.clear();
}

}
