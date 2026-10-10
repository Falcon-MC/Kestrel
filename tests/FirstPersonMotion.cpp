#include "client/FirstPersonMotion.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using kestrel::FirstPersonMotion;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
}

bool near(float a, float b, float tolerance = 1.0e-6f)
{
    return std::abs(a - b) <= tolerance;
}

int main()
{
    FirstPersonMotion walking;
    walking.tick({0.2f, 0.0f, 0.0f}, true, true, false);
    check(near(walking.bob, 0.008f), "walking amplitude must use the capped speed and tick smoothing");
    check(near(walking.distance, 0.12f), "walk phase must advance with horizontal speed");
    walking.tick({}, true, true, false);
    check(near(walking.bob, 0.00736f) && near(walking.oldBob, 0.008f), "stopping must ease the previous amplitude");
    check(near(walking.oldDistance, walking.distance), "standing still must stop the walk phase");
    walking.tick({0.2f, -0.5f, 0.0f}, false, true, false);
    check(near(walking.tilt, std::atan(0.1f) * 6.0f), "falling tilt must use vertical velocity");
    check(near(walking.distance, 0.12f), "airborne movement must not advance walking phase");
    walking.tick({0.2f, 0.0f, 0.0f}, true, true, true);
    check(walking.bob < walking.oldBob, "swimming must suppress walking amplitude");
    walking.tick({0.2f, -0.5f, 0.0f}, false, false, false);
    check(walking.tilt < walking.oldTilt, "dead players must ease out the falling tilt");

    FirstPersonMotion wrap, ordinary;
    wrap.look(1.0, 0.0f, 359.0f);
    ordinary.look(1.0, 0.0f, -1.0f);
    wrap.look(1.01, 0.0f, 1.0f);
    ordinary.look(1.01, 0.0f, 1.0f);
    check(near(wrap.rotation[1], ordinary.rotation[1]), "yaw wrapping must not produce a full-turn impulse");
    check(wrap.rotation[1] > 0.0f, "turning must move the hand through the spring");
    for (int frame = 2; frame <= 300; ++frame) wrap.look(1.0 + frame * 0.01, 0.0f, 1.0f);
    check(std::abs(wrap.rotation[1]) < 0.0001f && std::abs(wrap.velocity[1]) < 0.0001f, "the hand must settle after turning stops");

    FirstPersonMotion stalled;
    stalled.look(1.0, 0.0f, 0.0f);
    stalled.look(6.0, 90.0f, 180.0f);
    check(stalled.angleVelocity[0] == 50.0f && stalled.angleVelocity[1] == -50.0f, "angular speed must be capped on both axes");
    check(std::isfinite(stalled.rotation[0]) && std::isfinite(stalled.rotation[1]), "a stalled frame must keep the spring finite");
    auto before = stalled.rotation;
    stalled.look(6.0, 90.0f, 180.0f);
    check(stalled.rotation == before, "a duplicate frame time must not advance the spring");
}
