#include "client/PlayerMotion.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

const kestrel::PlayerMotion::CellLookup EmptyWorld = [](int32_t, int32_t, int32_t) {
    return kestrel::MotionCell {};
};

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

void near(float actual, float expected, const char* message)
{
    if (std::abs(actual - expected) > 1.0e-4f) {
        std::fprintf(stderr, "%s: expected %.8f, got %.8f\n", message, expected, actual);
        std::exit(1);
    }
}

kestrel::PlayerMotion flying(int32_t gameType = 1)
{
    kestrel::PlayerMotion motion;
    motion.reset({ 0.0f, 100.0f, 0.0f });
    motion.setGameType(gameType);
    motion.setAbilities(true, true, gameType == 6, 0.05f, 1.0f);
    return motion;
}

void flightSpeeds()
{
    auto ascending = flying();
    auto descending = flying();
    kestrel::MotionInput up;
    up.jump = true;
    kestrel::MotionInput down;
    down.sneak = true;
    for (int i = 0; i < 100; ++i) {
        auto upTick = ascending.step(up, EmptyWorld);
        auto downTick = descending.step(down, EmptyWorld);
        if (i == 0) {
            near(upTick.movement.y, 0.15f, "First ascent tick");
            near(downTick.movement.y, -0.22f, "First descent tick");
        }
        if (i == 99) {
            near(upTick.movement.y * 20.0f, 7.5f, "Terminal ascent in blocks/s");
            near(downTick.movement.y * 20.0f, -11.0f, "Terminal descent in blocks/s");
        }
    }
}

void idleFlight()
{
    for (float velocity : { -1.0f, 1.0f }) {
        auto motion = flying();
        motion.correct(motion.position(), { 0.0f, velocity, 0.0f }, false);
        auto tick = motion.step({}, EmptyWorld);
        near(tick.movement.y, velocity * 0.375f, "Idle displacement uses hover damping before moving");
        near(motion.currentVelocity().y, velocity * 0.225f, "Idle vertical retention");
    }
    for (bool sideways : { false, true }) {
        auto motion = flying();
        motion.correct(motion.position(), { 0.0f, 1.0f, 0.0f }, false);
        kestrel::MotionInput input;
        input.forward = sideways ? 0.0f : 1.0f;
        input.sideways = sideways ? 1.0f : 0.0f;
        auto tick = motion.step(input, EmptyWorld);
        near(tick.movement.y, 1.0f, "Horizontal input preserves vertical displacement");
        near(motion.currentVelocity().y, 0.6f, "Horizontal input preserves normal vertical drag");
    }
    auto spectator = flying(6);
    spectator.correct(spectator.position(), { 0.0f, 1.0f, 0.0f }, false);
    spectator.step({}, EmptyWorld);
    near(spectator.currentVelocity().y, 0.6f, "Spectator retains its existing idle drag");
    auto opposing = flying();
    opposing.correct(opposing.position(), { 0.0f, 1.0f, 0.0f }, false);
    kestrel::MotionInput both;
    both.jump = true;
    both.sneak = true;
    near(opposing.step(both, EmptyWorld).movement.y, 0.0f, "Opposing vertical inputs stop vertical flight");
}

void sprintHunger()
{
    kestrel::MotionInput input;
    input.forward = 1.0f;
    input.sprint = true;
    for (int32_t mode : { 0, 1, 2, 6 }) {
        for (float hunger : { 0.0f, 6.0f, 7.0f }) {
            kestrel::PlayerMotion motion;
            motion.reset({ 0.0f, 100.0f, 0.0f });
            motion.setGameType(mode);
            motion.setHunger(hunger);
            bool expected = mode == 1 || mode == 6 || hunger > 6.0f;
            require(motion.step(input, EmptyWorld).sprinting == expected,
                "Creative and spectator bypass the sprint hunger threshold");
        }
    }
    auto motion = flying();
    motion.setHunger(0.0f);
    require(motion.step(input, EmptyWorld).sprinting, "Hungry creative starts sprinting");
    input.usingItem = true;
    require(!motion.step(input, EmptyWorld).sprinting, "Creative hunger exemption preserves item-use restriction");
}

void heldJumpOverObstacle()
{
    for (int obstacle = 1; obstacle <= 12; ++obstacle) {
        kestrel::PlayerMotion motion;
        motion.reset({ 0.5f, 1.0f, 0.5f });
        motion.setGameType(0);
        motion.setHunger(20.0f);
        const kestrel::PlayerMotion::CellLookup world = [obstacle](int32_t, int32_t y, int32_t z) {
            return kestrel::MotionCell { y < 1 || (y == 1 && z == obstacle)
                ? kestrel::world::BlockCollisions::shared().fullBlock() : nullptr, nullptr };
        };
        kestrel::MotionInput input;
        input.forward = 1.0f;
        input.sprint = true;
        input.jump = true;
        int previousJump = -10;
        int jumps = 0;
        for (int tick = 0; tick < 100; ++tick) {
            const auto result = motion.step(input, world);
            if (result.startedJump) {
                require(tick - previousJump >= 10, "Held jump preserves cooldown when landing on an obstacle");
                previousJump = tick;
                ++jumps;
            }
        }
        require(jumps > 2, "Held jump continues after passing an obstacle");
    }
}

void heldJumpIntoCorner()
{
    const kestrel::PlayerMotion::CellLookup world = [](int32_t x, int32_t y, int32_t z) {
        return kestrel::MotionCell { y < 120 || x >= 10 || z >= 745
            ? kestrel::world::BlockCollisions::shared().fullBlock() : nullptr, nullptr };
    };
    for (float yaw : { 280.0f, 315.0f, 350.0f }) {
        kestrel::PlayerMotion motion;
        motion.reset({ 9.7001f, 120.0f, 744.7001f });
        motion.setGameType(0);
        motion.setHunger(20.0f);
        kestrel::MotionInput input;
        input.yaw = yaw;
        input.forward = 1.0f;
        input.sprint = input.jump = true;
        for (int tick = 0; tick < 100; ++tick) {
            auto result = motion.step(input, world);
            require(result.position.x <= 9.7002f && result.position.z <= 744.7002f,
                "Diagonal held jumps cannot cross either face of a solid corner");
        }
    }
}

}

int main()
{
    flightSpeeds();
    idleFlight();
    sprintHunger();
    heldJumpOverObstacle();
    heldJumpIntoCorner();
}
