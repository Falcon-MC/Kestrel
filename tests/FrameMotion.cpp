#include "client/FrameMotion.h"
#include "client/BodyRotation.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

using namespace kestrel;

const PlayerMotion::CellLookup Empty = [](int32_t, int32_t, int32_t) { return MotionCell {}; };
const FrameMotion::AreaReady Ready = [](const MotionVector&) { return true; };

void require(bool value, const char* message)
{
    if (value) return;
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
}

void near(const MotionVector& a, const MotionVector& b, const char* message)
{
    require((a - b).lengthSquared() < 1.0e-8f, message);
}

PlayerMotion flying()
{
    PlayerMotion seed;
    seed.reset({ 0.0f, 100.0f, 0.0f });
    seed.setGameType(1);
    seed.setAbilities(true, true, false, 0.05f, 1.0f);
    return seed;
}

void retainedDebt()
{
    FrameMotion frame;
    frame.reset(flying(), 80, 10.0);
    uint64_t expected = 81;
    double lastDue = 9.95;
    const FrameMotion::InputProvider input = [](uint64_t) { return MotionInput {}; };
    const FrameMotion::TickConsumer consume = [&](const FrameMotion::Tick& tick, const PlayerMotion&) {
        require(tick.number == expected++, "Catch-up must keep tick numbers ordered");
        require(std::abs(tick.due - lastDue - 0.05) < 1.0e-8, "Catch-up must keep due times ordered");
        lastDue = tick.due;
    };
    require(frame.advance(11.025, input, Empty, Ready, consume) == 10, "Catch-up must be bounded to ten ticks");
    require(frame.tickDelay(11.025) > 0.5, "Remaining debt must survive the catch-up cap");
    require(frame.advance(11.025, input, Empty, Ready, consume) == 10, "Next frame must consume retained debt");
    require(frame.advance(11.025, input, Empty, Ready, consume) == 1, "All elapsed ticks must eventually run");
    require(frame.tick() == 101, "A stall must not discard movement ticks");
}

void continuousPrediction()
{
    for (int fps : {30, 60, 144}) {
        FrameMotion frame;
        frame.reset(flying(), 0, 0.0);
        MotionInput input;
        input.forward = 1.0f;
        MotionVector previous = frame.state().position();
        for (int i = 0; i < fps * 5; ++i) {
            frame.advance(double(i) / fps, [&](uint64_t) { return input; }, Empty, Ready,
                [](const auto&, const auto&) {});
            const MotionVector visual = frame.interpolate();
            require(visual.z >= previous.z, "Steady movement must never move the camera backwards");
            previous = visual;
        }
    }
}

void prediction()
{
    FrameMotion frame;
    frame.reset(flying(), 0, 0.0);
    MotionInput input;
    input.forward = 1.0f;
    frame.advance(0.025, [&](uint64_t) { return input; }, Empty, Ready, [](const auto&, const auto&) {});
    const MotionVector authoritative = frame.state().position();
    const MotionVector velocity = frame.state().currentVelocity();
    const MotionVector visual = frame.interpolate();
    near(visual, authoritative - frame.lastResult().movement.scaled(0.5f), "Rendering must interpolate completed collision-tested motion");
    near(frame.state().position(), authoritative, "Prediction must not move authoritative physics");
    near(frame.state().currentVelocity(), velocity, "Prediction must not change authoritative velocity");
    require(frame.tick() == 1, "Prediction must not produce network ticks");
}

void releasedPrediction()
{
    FrameMotion frame;
    frame.reset(flying(), 0, 0.0);
    MotionInput input;
    input.forward = 1.0f;
    frame.advance(0.975, [&](uint64_t) { return input; }, Empty, Ready, [](const auto&, const auto&) {});
    frame.advance(0.999, [&](uint64_t) { return input; }, Empty, Ready, [](const auto&, const auto&) {});
    const MotionVector before = frame.interpolate();
    input.forward = 0.0f;
    frame.advance(0.9995, [&](uint64_t) { return input; }, Empty, Ready, [](const auto&, const auto&) {});
    const MotionVector after = frame.interpolate();
    require(after.z >= before.z, "Releasing forward must not rewind motion already shown within a tick");
    MotionVector previous = after;
    for (int i = 0; i < 200; ++i) {
        frame.advance(1.0 + double(i) / 144, [&](uint64_t) { return input; }, Empty, Ready,
            [](const auto&, const auto&) {});
        const MotionVector visual = frame.interpolate();
        require(visual.z >= previous.z, "A released input must remain continuous across later ticks");
        previous = visual;
    }
}

void rebasedVisualContinuity()
{
    FrameMotion frame;
    frame.reset(flying(), 0, 0.0);
    MotionInput input;
    input.forward = 1.0f;
    frame.advance(0.025, [&](uint64_t) { return input; }, Empty, Ready, [](const auto&, const auto&) {});
    const MotionVector before = frame.interpolate();
    PlayerMotion seed = frame.state().detached();
    seed.setHunger(19.0f);
    frame.rebase(seed, frame.tick());
    near(frame.interpolate(), before, "A settings update must preserve the visible movement segment");
}

void localBodyFollowsMotion()
{
    FrameMotion frame;
    frame.reset(flying(), 0, 0.0);
    MotionInput input;
    input.forward = 1.0f;
    input.sprint = true;
    input.yaw = 180.0f;
    float body = 0.0f;
    for (int i = 0; i < 120; ++i) {
        frame.advance(double(i) / 60, [&](uint64_t) { return input; }, Empty, Ready,
            [](const auto&, const auto&) {});
        const auto& velocity = frame.lastResult().velocity;
        body = actor::trailBody(body, input.yaw, velocity.x, velocity.z, 20.0f / 60.0f);
    }
    require(std::abs(actor::wrapDegrees(body - input.yaw)) < 0.1f,
        "Local body must align with a straight run despite identical current/previous render positions");
    for (int i = 0; i < 60; ++i) body = actor::trailBody(body, 180.0f, 0.0, 0.2, 20.0f / 60.0f);
    require(std::abs(actor::wrapDegrees(body - 180.0f)) < 0.1f,
        "Walking backwards must not turn the body away from the head");
}

void correctionReplay()
{
    FrameMotion frame;
    frame.reset(flying(), 0, 0.0);
    MotionInput input;
    input.forward = 1.0f;
    PlayerMotion corrected;
    frame.advance(0.225, [&](uint64_t number) {
        MotionInput result = input;
        result.sideways = number > 2 ? 0.5f : 0.0f;
        return result;
    }, Empty, Ready, [&](const auto& tick, const PlayerMotion& state) {
        if (tick.number == 2) corrected.copyState(state);
    });
    const MotionVector target { 4.0f, 101.0f, 3.0f };
    const MotionVector velocity { 0.1f, 0.0f, 0.2f };
    corrected.correct(target, velocity, false);
    input.sideways = 0.5f;
    for (int i = 0; i < 3; ++i) {
        const MotionTick tick = corrected.step(input, Empty);
        const float eye = tick.position.y + 1.62001f;
        corrected.anchor({ tick.position.x, eye - 1.62001f, tick.position.z });
    }
    frame.correct(2, target, &velocity, false, Empty);
    near(frame.state().position(), corrected.position(), "Correction must replay every later tick with its own input");
    near(frame.state().currentVelocity(), corrected.currentVelocity(), "Correction replay must preserve resulting velocity");
    require(frame.tick() == 5, "Correction must preserve client tick numbering");
}

void unavailableTerrain()
{
    FrameMotion frame;
    frame.reset(flying(), 0, 0.0);
    MotionInput input;
    input.forward = 1.0f;
    const MotionVector start = frame.state().position();
    frame.advance(0.075, [&](uint64_t) { return input; }, Empty, [](const MotionVector&) { return false; },
        [&](const auto& tick, const auto&) { require(tick.frozen, "Unloaded terrain must produce a frozen tick"); });
    near(frame.state().position(), start, "Missing terrain must never let physics move through absent collisions");
}

void rebasedUnsentTicks()
{
    const PlayerMotion seed = flying();
    FrameMotion frame;
    frame.reset(seed, 0, 0.0);
    const FrameMotion::InputProvider input = [](uint64_t) { return MotionInput {}; };
    frame.advance(0.225, input, Empty, Ready, [](const auto&, const auto&) {});
    require(frame.tick() == 5, "Precondition: five generated ticks");
    frame.rebase(seed, 2);
    uint64_t expected = 3;
    const size_t count = frame.advance(0.225, input, Empty, Ready, [&](const auto& tick, const auto&) {
        require(tick.number == expected++, "Rebased ticks must retain sequential numbering");
    });
    require(count == 3 && frame.tick() == 5, "Discarded unsent ticks must retain their time debt");
}

void predictedCollision()
{
    PlayerMotion seed = flying();
    seed.teleport({ 0.0f, 100.0f, 0.65f });
    const PlayerMotion::CellLookup wall = [](int32_t, int32_t, int32_t z) {
        return MotionCell { z >= 1 ? world::BlockCollisions::shared().fullBlock() : nullptr, nullptr };
    };
    FrameMotion frame;
    frame.reset(seed, 0, 0.0);
    MotionInput input;
    input.forward = 1.0f;
    frame.advance(0.04, [&](uint64_t) { return input; }, wall, Ready, [](const auto&, const auto&) {});
    const MotionVector before = frame.state().position();
    for (int i = 0; i < 10; ++i) {
        const MotionVector visual = frame.interpolate();
        require(visual.z <= 0.7001f, "Visual prediction must not pass through a solid wall");
        near(frame.state().position(), before, "Repeated prediction must not accumulate authoritative movement");
    }
}

void rebasedShortActions()
{
    const PlayerMotion seed = flying();
    FrameMotion frame;
    frame.reset(seed, 0, 0.0);
    size_t consumed = 0;
    const FrameMotion::InputProvider input = [&](uint64_t number) {
        ++consumed;
        MotionInput value;
        value.trace = number + 100;
        value.jump = number == 2;
        value.startFlying = number == 3;
        value.startGlide = number == 4;
        value.riptide = number == 5 ? 3 : 0;
        return value;
    };
    frame.advance(0.225, input, Empty, Ready, [](const auto&, const auto&) {});
    require(consumed == 5, "Precondition: five inputs consumed");
    frame.rebase(seed, 1);
    frame.rebase(seed, 1);
    uint64_t expected = 2;
    frame.advance(0.225, input, Empty, Ready, [&](const auto& tick, const auto&) {
        require(tick.number == expected++, "Replayed short actions must keep their original tick order");
        require(tick.input.trace == tick.number + 100, "Replayed input must retain its latency trace");
        require(tick.input.jump == (tick.number == 2), "A rejected short jump must survive repeated rebases");
        require(tick.input.startFlying == (tick.number == 3), "A rejected fly toggle must survive repeated rebases");
        require(tick.input.startGlide == (tick.number == 4), "A rejected glide toggle must survive repeated rebases");
        require(tick.input.riptide == (tick.number == 5 ? 3 : 0), "A rejected riptide must survive repeated rebases");
    });
    require(consumed == 5 && expected == 6, "Rebase must not consume new inputs or duplicate one-shot actions");
    frame.acknowledge(5);
    frame.advance(0.275, input, Empty, Ready, [](const auto&, const auto&) {});
    require(consumed == 6, "Acknowledged replay inputs must allow subsequent fresh input");
    frame.reset(seed, 0, 1.0);
    frame.advance(1.0, [](uint64_t) { return MotionInput {}; }, Empty, Ready, [&](const auto& tick, const auto&) {
        require(!tick.input.jump && !tick.input.startFlying && !tick.input.startGlide && tick.input.riptide == 0,
            "A hard reset must cancel pending actions");
    });
}

void replayUsesCurrentLocks()
{
    const PlayerMotion seed = flying();
    FrameMotion frame;
    frame.reset(seed, 0, 0.0);
    size_t consumed = 0;
    size_t adjusted = 0;
    bool lock = false;
    const FrameMotion::InputProvider input = [&](uint64_t) {
        ++consumed;
        MotionInput value;
        value.jump = true;
        return value;
    };
    const FrameMotion::InputAdjuster adjust = [&](MotionInput value, uint64_t) {
        ++adjusted;
        if (lock) value.jump = false;
        return value;
    };
    frame.advance(0.0, input, Empty, Ready, [&](const auto& tick, const auto&) {
        require(tick.input.jump, "Unrestricted original input must reach physics");
    }, adjust);
    lock = true;
    frame.rebase(seed, 0);
    frame.advance(0.0, input, Empty, Ready, [&](const auto& tick, const auto&) {
        require(!tick.input.jump, "Current server locks must apply to retained inputs before physics");
    }, adjust);
    require(consumed == 1 && adjusted == 2, "Regeneration must adjust input once without consuming another input");
    lock = false;
    frame.rebase(seed, 0);
    frame.advance(0.0, input, Empty, Ready, [&](const auto& tick, const auto&) {
        require(tick.input.jump, "The original one-shot input must remain available after transient locks");
    }, adjust);
    require(consumed == 1 && adjusted == 3, "Repeated rebase must preserve unfiltered input");
}

void pendingInputsOutlivePhysicsHistory()
{
    const PlayerMotion seed = flying();
    FrameMotion frame;
    frame.reset(seed, 0, 0.0);
    size_t consumed = 0;
    const FrameMotion::InputProvider input = [&](uint64_t number) {
        ++consumed;
        MotionInput value;
        value.trace = number;
        value.startFlying = number == 2;
        return value;
    };
    for (int i = 0; i < 4; ++i) frame.advance(1.975, input, Empty, Ready, [](const auto&, const auto&) {});
    require(frame.tick() == 40 && consumed == 40, "Precondition: pending inputs exceed physics history");
    frame.rebase(seed, 0);
    uint64_t expected = 1;
    for (int i = 0; i < 4; ++i) frame.advance(1.975, input, Empty, Ready, [&](const auto& tick, const auto&) {
        require(tick.number == expected++ && tick.input.trace == tick.number,
            "Every unacknowledged input must survive a full outbound backlog");
        require(tick.input.startFlying == (tick.number == 2), "An old pending fly toggle must survive physics-history eviction");
    });
    require(consumed == 40 && expected == 41, "Backlog regeneration must neither lose nor duplicate inputs");
}

}

int main()
{
    retainedDebt();
    continuousPrediction();
    prediction();
    releasedPrediction();
    rebasedVisualContinuity();
    localBodyFollowsMotion();
    correctionReplay();
    unavailableTerrain();
    rebasedUnsentTicks();
    predictedCollision();
    rebasedShortActions();
    replayUsesCurrentLocks();
    pendingInputsOutlivePhysicsHistory();
    return 0;
}
