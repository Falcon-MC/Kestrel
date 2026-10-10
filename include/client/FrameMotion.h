#pragma once

#include "client/PlayerMotion.h"

#include <deque>
#include <functional>

namespace kestrel {

class FrameMotion {
public:
    static constexpr double TickSeconds = 0.05;
    static constexpr size_t MaxFrameTicks = 10;
    static constexpr size_t HistoryTicks = 32;

    struct Tick {
        uint64_t number = 0;
        double due = 0.0;
        MotionInput input;
        MotionTick result;
        bool frozen = false;
    };

    using InputProvider = std::function<MotionInput(uint64_t)>;
    using InputAdjuster = std::function<MotionInput(MotionInput, uint64_t)>;
    using AreaReady = std::function<bool(const MotionVector&)>;
    using TickConsumer = std::function<void(const Tick&, const PlayerMotion&)>;

    void reset(const PlayerMotion& seed, uint64_t tick, double now);
    void rebase(const PlayerMotion& seed, uint64_t tick);
    void acknowledge(uint64_t tick);
    void takeSettings(const PlayerMotion& settings);
    size_t advance(double now, const InputProvider& input, const PlayerMotion::CellLookup& lookup,
        const AreaReady& ready, const TickConsumer& consume, const InputAdjuster& adjust = {});
    void correct(uint64_t tick, const MotionVector& position, const MotionVector* velocity,
        bool grounded, const PlayerMotion::CellLookup& lookup);
    void knockback(uint64_t tick, const MotionVector& velocity, const PlayerMotion::CellLookup& lookup);
    MotionVector predict(const MotionInput& input, const PlayerMotion::CellLookup& lookup, const AreaReady& ready);

    const PlayerMotion& state() const { return motion; }
    uint64_t tick() const { return currentTick; }
    const MotionTick& lastResult() const { return latest; }
    double tickDelay(double now) const;

private:
    struct History {
        Tick tick;
        PlayerMotion after;
    };
    struct Impulse {
        uint64_t tick;
        MotionVector velocity;
    };
    struct PendingInput {
        uint64_t tick;
        MotionInput input;
    };

    void replay(size_t index, const PlayerMotion::CellLookup& lookup);
    void applyImpulse(PlayerMotion& target, uint64_t tick) const;
    static void anchor(PlayerMotion& target, const MotionTick& result);

    PlayerMotion motion;
    PlayerMotion preview;
    MotionTick latest;
    std::deque<History> history;
    std::deque<Impulse> impulses;
    std::deque<PendingInput> pendingInputs;
    uint64_t currentTick = 0;
    double nextTick = 0.0;
    double frameTime = 0.0;
    bool initialized = false;
};

}
