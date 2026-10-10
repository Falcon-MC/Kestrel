#include "client/FirstPersonMotion.h"
#include "client/FirstPersonPlacement.h"

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
    using namespace kestrel::first_person;
    Vec3 root = rigPoint({ 16.0f, 24.0f, 8.0f }, 1.0f, 1.62f);
    check(near(root[0], -1.0f) && near(root[1], 1.5f - 1.62f + 1.0f / 128.0f)
            && near(root[2], -0.5f), "The hand rig must retain its pose, use eye height and apply the root lift");
    Vec3 scaledRoot = rigPoint({}, 0.5f, 1.27f);
    check(near(scaledRoot[1], -1.27f + 0.5f / 128.0f), "Eye height must not scale with the model, but the root lift must");
    Mat4 block = offhandPlacement(true, false);
    Vec3 center = transformed(block, {});
    check(near(center[0], -0.56f) && near(center[1], -0.52f) && near(center[2], -0.72f),
        "Offhand blocks must use their own camera anchor");
    Vec3 edge = transformed(block, { 0.5f, 0.0f, 0.0f });
    check(near(edge[0] - center[0], -std::sqrt(0.02f))
            && near(edge[2] - center[2], -std::sqrt(0.02f)),
        "Offhand block presentation must rotate by 135 degrees at scale 0.4");
    check(block == offhandPlacement(true, true), "The hand-equipped sprite flag must not change block placement");
    Mat4 flat = offhandPlacement(false, false);
    Mat4 equipped = offhandPlacement(false, true);
    check(flat != equipped, "Flat and hand-equipped offhand items need distinct placement routes");
    for (const Mat4& pose : { flat, equipped }) {
        Vec3 a = transformed(pose, {});
        Vec3 b = transformed(pose, { 1.0f, 0.0f, 0.0f });
        check(near(std::hypot(b[0] - a[0], std::hypot(b[1] - a[1], b[2] - a[2])), 1.0f),
            "Offhand native sprite basis must preserve the normalized icon size");
    }

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
