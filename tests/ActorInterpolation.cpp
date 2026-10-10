#include "client/ActorInterpolation.h"
#include "client/ActorMotion.h"
#include "client/ActorPose.h"
#include "client/ProjectileMotion.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

bool near(double a, double b)
{
    return std::abs(a - b) < 1e-6;
}

void constantMovement(int framesPerTick)
{
    kestrel::ActorInterpolation motion;
    motion.retarget({}, {}, true);
    double previous = 0.0;
    double frameLength = 0.05 / framesPerTick;
    for (int tick = 1; tick <= 100; ++tick) {
        double stamp = 1.0 + tick * 0.05;
        motion.retarget({ tick * 0.25, 0.0, 0.0 }, {}, false);
        motion.tick(stamp);
        for (int frame = 0; frame < framesPerTick; ++frame) {
            auto position = motion.position(stamp + frame * frameLength);
            if (tick > 60) {
                require(near(position[0] - previous, 5.0 * frameLength),
                    "Steady movement must advance uniformly on packet frames and between ticks");
            }
            previous = position[0];
        }
    }
}

int main()
{
    const std::array<double, 3> validPosition { 10.0, 64.0, -20.0 };
    const std::array<float, 3> validTurn { 90.0f, 45.0f, -10.0f };
    require(kestrel::validActorPose(validPosition, validTurn), "Finite actor poses must remain accepted");
    for (size_t axis = 0; axis < 3; ++axis) {
        for (double invalid : { std::numeric_limits<double>::quiet_NaN(),
                 std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() }) {
            auto position = validPosition;
            auto turn = validTurn;
            position[axis] = invalid;
            require(!kestrel::validActorPose(position, validTurn), "Non-finite spawn and movement positions must be rejected");
            turn[axis] = static_cast<float>(invalid);
            require(!kestrel::validActorPose(validPosition, turn), "Non-finite actor rotations must be rejected");
        }
    }
    std::array<double, 3> arrow {};
    const std::array<double, 3> target { 6.0, 3.0, -3.0 };
    const std::array<float, 3> velocity { 2.0f, 1.0f, -1.0f };
    for (uint8_t remaining = 3; remaining > 0; --remaining) {
        const auto next = kestrel::projectileStep(arrow, target, velocity, remaining, true);
        require(near(next[0] - arrow[0], 2.0), "Arrow corrections must interpolate over three ticks");
        arrow = next;
    }
    for (int tick = 0; tick < 12; ++tick) {
        const auto next = kestrel::projectileStep(arrow, target, velocity, 0, true);
        require(near(next[0] - arrow[0], 2.0) && near(next[1] - arrow[1], 1.0) && near(next[2] - arrow[2], -1.0),
            "Airborne arrows must keep moving after their correction completes");
        arrow = next;
    }
    require(kestrel::projectileStep(arrow, target, velocity, 0, false) == arrow,
        "Grounded arrows and non-predicted projectiles must remain stationary");
    require(kestrel::projectileStep(arrow, arrow, {}, 0, true) == arrow,
        "Zero server velocity must stop extrapolation");
    const auto invalidMotion = kestrel::projectileStep(arrow, target,
        { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.0f }, 0, true);
    require(invalidMotion == arrow, "Non-finite velocity must not corrupt projectile positions");
    const auto corrected = kestrel::projectileStep(arrow, target, velocity, 1, false);
    require(corrected == target, "A grounded server correction must override extrapolated positions");
    kestrel::ActorMotion player;
    player.lastSample = 1.0;
    player.retarget({ 1.0, 0.0, 0.0 }, { 170.0f, 0.0f, 0.0f }, 1.05);
    player.advance(1.075);
    require(near(player.shown[0], 0.5), "Players must retain time-based interpolation between packets");
    player.retarget({ 2.0, 0.0, 0.0 }, { -170.0f, 0.0f, 0.0f }, 1.1);
    require(near(player.shown[0], 1.0), "Player retargeting must advance the old target without a packet-frame pause");
    player.advance(1.125);
    require(near(player.shown[0], 1.5), "Players must not switch to three-tick actor interpolation");
    require(std::abs(player.turnShown[0] + 180.0f) < 1e-4f, "Player turns must follow the shortest angular path");
    player.advance(2.0);
    require(player.shown == player.to, "Player interpolation must settle at its target");

    using kestrel::ActorInterpolation;
    constantMovement(1);
    constantMovement(3);
    constantMovement(6);
    constantMovement(12);

    ActorInterpolation motion;
    motion.retarget({ 10.0, 20.0, 30.0 }, { 170.0f, -170.0f, 0.0f }, true);
    motion.retarget({ 13.0, 26.0, 39.0 }, { -170.0f, 170.0f, 30.0f }, false);
    require(motion.current[0] == 10.0, "A packet must only replace the target, not move the entity");
    motion.tick(1.0);
    require(motion.current == std::array<double, 3> { 11.0, 22.0, 33.0 },
        "The first tick must move one third of the remaining distance");
    require(near(motion.position(1.025)[0], 10.5), "Render frames must interpolate previous and current ticks");
    require(std::abs(motion.rotation(1.025)[0] - 173.333333f) < 1e-4f, "Turns must take the shortest path across the angle boundary");
    motion.tick(1.05);
    require(motion.current == std::array<double, 3> { 12.0, 24.0, 36.0 }, "The second tick must move half the remaining distance");
    motion.tick(1.1);
    require(motion.current == motion.target && motion.currentTurn == motion.targetTurn,
        "The third tick must land exactly at the final position and rotation");
    require(motion.position(10.0) == motion.target, "A stopped stream must settle without extrapolating forever");
    motion.tick(1.15);
    require(motion.previous == motion.current, "Idle ticks must clear the last movement delta");

    ActorInterpolation batched, single;
    batched.retarget({}, {}, true);
    single.retarget({}, {}, true);
    for (int sample = 1; sample <= 10; ++sample) batched.retarget({ sample * 0.25, 0.0, 0.0 }, {}, false);
    single.retarget({ 2.5, 0.0, 0.0 }, {}, false);
    require(batched.current == single.current, "A burst must not advance simulation once per packet");
    for (int tick = 0; tick < 4; ++tick) {
        batched.tick(2.0 + tick * 0.05);
        single.tick(2.0 + tick * 0.05);
        for (int frame = 0; frame < 12; ++frame) {
            double now = 2.0 + tick * 0.05 + frame / 240.0;
            require(batched.position(now) == single.position(now), "A burst must render like its final target");
        }
    }

    ActorInterpolation interrupted;
    interrupted.retarget({}, {}, true);
    interrupted.retarget({ 3.0, 0.0, 0.0 }, {}, false);
    interrupted.tick(3.0);
    auto previous = interrupted.previous;
    auto current = interrupted.current;
    interrupted.retarget({ -2.0, 0.0, 0.0 }, {}, false);
    require(interrupted.previous == previous && interrupted.current == current,
        "A mid-tick target reversal must preserve both render endpoints");
    interrupted.tick(3.05);
    require(near(interrupted.current[0], 0.0), "An interrupted interpolation must restart three steps from the current tick");
    interrupted.retarget({ 100.0, 50.0, -20.0 }, { 90.0f, 45.0f, 20.0f }, true);
    require(interrupted.position(3.051) == interrupted.target && interrupted.rotation(3.051) == interrupted.targetTurn,
        "Teleports must reset both endpoints and all rotations immediately");
    interrupted.retarget({ 130.0, 50.0, -20.0 }, {}, false);
    interrupted.tick(3.1);
    require(interrupted.current[0] == 110.0, "Distance alone must not turn an ordinary move into a teleport");

    kestrel::ActorMoveQueue burst;
    ActorInterpolation replay;
    replay.retarget({}, {}, true);
    for (int sample = 1; sample <= 12; ++sample) {
        burst.push({ sample * 0.25, 0.0, 0.0 }, { float(sample), 0.0f, 0.0f });
    }
    for (int tick = 0; tick < 16; ++tick) {
        kestrel::ActorMoveTarget sample;
        if (burst.pop(sample)) {
            require(sample.position[0] == (tick + 1) * 0.25, "A burst must preserve every intermediate position in order");
            require(sample.turn[0] == float(tick + 1), "Rotations must remain paired with their positions");
            replay.retarget(sample.position, sample.turn, false);
        }
        replay.tick(4.0 + tick * 0.05);
        require(replay.current[0] - replay.previous[0] <= 0.250001,
            "A delayed burst must not compress twelve server ticks into a three-tick jump");
    }
    require(replay.current[0] == 3.0 && burst.count == 0, "A replayed burst must eventually settle at its latest target");

    for (int sample = 0; sample < 100; ++sample) burst.push({ double(sample), 0.0, 0.0 }, {});
    require(burst.count == 64, "A server sending too fast must not grow the move backlog without bound");
    kestrel::ActorMoveTarget sample;
    for (int expected = 36; expected < 100; ++expected) {
        require(burst.pop(sample) && sample.position[0] == expected,
            "Overflow must retain the newest bounded set of samples in order");
    }
    require(!burst.pop(sample), "An exhausted backlog must not replay an old move");
    std::puts("Actor interpolation regressions passed");
}
