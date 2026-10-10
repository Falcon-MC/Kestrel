#include "client/FrameMotion.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

void FrameMotion::reset(const PlayerMotion& seed, uint64_t tick, double now)
{
    motion.copyState(seed);
    preview.copyState(seed);
    history.clear();
    impulses.clear();
    pendingInputs.clear();
    currentTick = tick;
    nextTick = now;
    frameTime = now;
    initialized = std::isfinite(now) && seed.initialized();
    latest = {};
    latest.position = seed.position();
    latest.velocity = seed.currentVelocity();
    latest.onGround = seed.grounded();
    latest.sneaking = seed.sneaking();
    latest.swimming = seed.swimming();
}

void FrameMotion::rebase(const PlayerMotion& seed, uint64_t tick)
{
    acknowledge(tick);
    if (tick < currentTick) nextTick -= static_cast<double>(currentTick - tick) * TickSeconds;
    else nextTick += static_cast<double>(tick - currentTick) * TickSeconds;
    motion.copyState(seed);
    history.clear();
    impulses.clear();
    currentTick = tick;
    latest.position = seed.position();
    latest.velocity = seed.currentVelocity();
    latest.onGround = seed.grounded();
    latest.sneaking = seed.sneaking();
    latest.swimming = seed.swimming();
}

void FrameMotion::acknowledge(uint64_t tick)
{
    while (!pendingInputs.empty() && pendingInputs.front().tick <= tick) pendingInputs.pop_front();
}

void FrameMotion::takeSettings(const PlayerMotion& settings)
{
    motion.takeSettings(settings);
}

void FrameMotion::anchor(PlayerMotion& target, const MotionTick& result)
{
    constexpr float EyeHeight = 1.62001f;
    const float eyeY = result.position.y + EyeHeight;
    target.anchor({ result.position.x, eyeY - EyeHeight, result.position.z });
}

void FrameMotion::applyImpulse(PlayerMotion& target, uint64_t tick) const
{
    for (const Impulse& impulse : impulses) {
        if (impulse.tick == tick) target.knockback(impulse.velocity);
    }
}

size_t FrameMotion::advance(double now, const InputProvider& input, const PlayerMotion::CellLookup& lookup,
    const AreaReady& ready, const TickConsumer& consume, const InputAdjuster& adjust)
{
    if (!initialized || !std::isfinite(now)) return 0;
    frameTime = std::max(frameTime, now);
    size_t count = 0;
    // Keep the remaining debt when a stalled frame reaches the catch-up limit.
    while (now >= nextTick && count < MaxFrameTicks) {
        constexpr size_t MaxPendingInputs = 128;
        auto cached = std::find_if(pendingInputs.begin(), pendingInputs.end(), [&](const PendingInput& entry) {
            return entry.tick == currentTick + 1;
        });
        if (cached == pendingInputs.end() && pendingInputs.size() >= MaxPendingInputs) break;
        Tick tick;
        tick.number = ++currentTick;
        tick.due = nextTick;
        nextTick += TickSeconds;
        // Inputs survive rejected outbound ticks, including one-shot actions.
        if (cached != pendingInputs.end()) tick.input = cached->input;
        else {
            tick.input = input(tick.number);
            pendingInputs.push_back({ tick.number, tick.input });
        }
        if (adjust) tick.input = adjust(tick.input, tick.number);
        tick.frozen = !ready(motion.position());
        if (tick.frozen) {
            motion.hold();
            tick.result.position = motion.position();
            tick.result.sneaking = motion.sneaking();
            tick.result.onGround = motion.grounded();
        } else {
            applyImpulse(motion, tick.number);
            tick.result = motion.step(tick.input, lookup);
            anchor(motion, tick.result);
        }
        latest = tick.result;
        history.push_back({ tick, motion });
        while (history.size() > HistoryTicks) history.pop_front();
        while (!impulses.empty() && impulses.front().tick + HistoryTicks < currentTick) impulses.pop_front();
        consume(tick, motion);
        ++count;
    }
    return count;
}

void FrameMotion::correct(uint64_t tick, const MotionVector& position, const MotionVector* velocity,
    bool grounded, const PlayerMotion::CellLookup& lookup)
{
    auto found = std::find_if(history.begin(), history.end(), [tick](const History& entry) {
        return entry.tick.number == tick;
    });
    if (found == history.end()) {
        motion.correct(position, velocity ? *velocity : motion.currentVelocity(), grounded);
        history.clear();
        impulses.clear();
        return;
    }
    found->after.correct(position, velocity ? *velocity : found->after.currentVelocity(), grounded);
    const size_t index = static_cast<size_t>(found - history.begin());
    replay(index, lookup);
    history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(index));
}

void FrameMotion::replay(size_t index, const PlayerMotion::CellLookup& lookup)
{
    PlayerMotion restored = history[index].after;
    for (size_t i = index + 1; i < history.size(); ++i) {
        History& entry = history[i];
        restored.takeSettings(entry.after);
        if (entry.tick.frozen) restored.hold();
        else {
            applyImpulse(restored, entry.tick.number);
            entry.tick.result = restored.step(entry.tick.input, lookup);
            anchor(restored, entry.tick.result);
        }
        entry.after = restored;
    }
    restored.keepPendingKnockback(motion);
    restored.takeSettings(motion);
    motion = restored;
}

void FrameMotion::knockback(uint64_t tick, const MotionVector& velocity, const PlayerMotion::CellLookup& lookup)
{
    auto found = std::find_if(impulses.begin(), impulses.end(), [tick](const Impulse& impulse) { return impulse.tick == tick; });
    if (found != impulses.end()) found->velocity = velocity;
    else {
        auto place = std::upper_bound(impulses.begin(), impulses.end(), tick,
            [](uint64_t number, const Impulse& impulse) { return number < impulse.tick; });
        impulses.insert(place, { tick, velocity });
    }
    // A late impulse can only be replayed if the preceding state is still retained.
    for (size_t i = 0; i + 1 < history.size(); ++i) {
        if (history[i + 1].tick.number == tick) {
            replay(i, lookup);
            break;
        }
    }
    while (impulses.size() > HistoryTicks) impulses.pop_front();
}

MotionVector FrameMotion::predict(const MotionInput& input, const PlayerMotion::CellLookup& lookup, const AreaReady& ready)
{
    if (!initialized || !ready(motion.position())) return motion.position();
    const float fraction = static_cast<float>(std::clamp((frameTime - (nextTick - TickSeconds)) / TickSeconds, 0.0, 1.0));
    preview.copyState(motion);
    applyImpulse(preview, currentTick + 1);
    const MotionTick predicted = preview.step(input, lookup);
    return motion.position() + (predicted.position - motion.position()).scaled(fraction);
}

double FrameMotion::tickDelay(double now) const
{
    return initialized && std::isfinite(now) ? std::max(0.0, now - nextTick) : 0.0;
}

}
