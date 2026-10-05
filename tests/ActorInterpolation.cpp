#include "client/ActorMotion.h"

#include <cstdio>
#include <cstdlib>

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

void constantMovement(int framesPerTick)
{
    kestrel::ActorMotion motion;
    double previous = 0.0;
    double frameLength = 0.05 / framesPerTick;
    for (int frame = 1; frame <= 80 * framesPerTick; ++frame) {
        double now = frame * frameLength;
        if (frame % framesPerTick == 0) {
            motion.retarget({ now * 5.0, 0.0, 0.0 }, {}, now);
        }
        motion.advance(now);
        if (frame > 2 * framesPerTick) {
            require(std::abs(motion.shown[0] - previous - 5.0 * frameLength) < 1e-8,
                "Constant movement must advance on every frame, including packet frames");
        }
        previous = motion.shown[0];
    }
}

void unevenFrames(int framesPerSecond)
{
    kestrel::ActorMotion motion;
    int previousTick = 0;
    double previous = 0.0;
    for (int frame = 1; frame <= framesPerSecond * 4; ++frame) {
        double now = double(frame) / framesPerSecond;
        int tick = int(std::floor(now / 0.05 + 1e-8));
        if (tick != previousTick) {
            motion.retarget({ tick * 0.25, 0.0, 0.0 }, {}, now);
            previousTick = tick;
        }
        motion.advance(now);
        if (frame > framesPerSecond) {
            require(motion.shown[0] > previous,
                "Movement must not freeze when frame and packet intervals differ");
        }
        previous = motion.shown[0];
    }
}

int main()
{
    constantMovement(3);
    constantMovement(6);
    unevenFrames(30);
    unevenFrames(60);
    unevenFrames(144);

    kestrel::ActorMotion motion;
    motion.to = { 1.0, 2.0, 3.0 };
    motion.turnFrom = { 170.0f, -170.0f, 0.0f };
    motion.turnTo = { -170.0f, 170.0f, 20.0f };
    motion.duration = 0.1;
    motion.advance(0.025);
    motion.retarget({ 3.0, 4.0, 5.0 }, { 0.0f, 0.0f, 40.0f }, 0.05);
    require(motion.shown == std::array<double, 3> { 0.5, 1.0, 1.5 },
        "An interrupted glide must advance all axes to the current frame before retargeting");
    require(motion.turnShown == std::array<float, 3> { -180.0f, -180.0f, 10.0f },
        "Rotation must advance across the shortest angle before retargeting");
    motion.advance(0.05);
    require(motion.shown[0] == 0.5, "Retargeting must preserve the current-time position");
    motion.advance(1.0);
    require(motion.shown == motion.to, "A stopped stream must settle at its final position");
    std::puts("Actor interpolation regressions passed");
}
