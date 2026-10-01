#include "MotionMath.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

using namespace motion;

namespace {

/**
 * The value kept between zero and the limit, so a clipped step can only
 * shorten the requested movement, never lengthen or reverse it.
 */
float towardZero(float value, float limit)
{
    if (limit >= 0.0f) {
        return std::clamp(value, 0.0f, limit);
    }
    return std::clamp(value, limit, 0.0f);
}

}

void PlayerMotion::move()
{
    MotionVector requested = velocity;
    if (!boundedQuery(extend(boundingBox(), requested))) {
        velocity = {};
        collideX = collideY = collideZ = true;
        return;
    }
    if (isSneaking && !isCrawling && onGround && requested.y <= 0.0f) {
        requested = avoidEdge(boundingBox(), requested);
        velocity = requested;
    }

    world::CollisionBox original = boundingBox();
    std::vector<world::CollisionBox> nearby = collisionBoxes(extend(original, requested));

    auto resolve = [&](bool oneWay) {
        Resolution resolution;
        world::CollisionBox box = original;
        float penetration[3] = {};
        MotionVector yMovement = clipAll(nearby, box, { 0.0f, requested.y, 0.0f }, oneWay, penetration);
        box = offset(box, yMovement);
        MotionVector xMovement = clipAll(nearby, box, { requested.x, 0.0f, 0.0f }, oneWay, penetration);
        box = offset(box, xMovement);
        MotionVector zMovement = clipAll(nearby, box, { 0.0f, 0.0f, requested.z }, oneWay, penetration);
        box = offset(box, zMovement);
        resolution.box = box;
        resolution.movement = (yMovement + xMovement) + zMovement;
        float squared = penetration[0] * penetration[0] + penetration[1] * penetration[1] + penetration[2] * penetration[2];
        resolution.penetrated = squared >= PenetrationEpsilonSquared;
        return resolution;
    };

    auto autoStep = [&](bool oneWay) {
        std::vector<world::CollisionBox> filtered;
        filtered.reserve(nearby.size());
        for (const world::CollisionBox& box : nearby) {
            if (box.minY < original.maxY) {
                filtered.push_back(box);
            }
        }
        Resolution resolution;
        world::CollisionBox box = original;
        MotionVector up = clipAll(filtered, box, { 0.0f, StepHeight, 0.0f }, oneWay, nullptr);
        box = offset(box, up);
        MotionVector x = clipAll(filtered, box, { requested.x, 0.0f, 0.0f }, oneWay, nullptr);
        x = { towardZero(x.x, requested.x), 0.0f, 0.0f };
        box = offset(box, x);
        MotionVector z = clipAll(filtered, box, { 0.0f, 0.0f, requested.z }, oneWay, nullptr);
        z = { 0.0f, 0.0f, towardZero(z.z, requested.z) };
        box = offset(box, z);
        MotionVector down = clipAll(filtered, box, up.scaled(-1.0f), oneWay, nullptr);
        box = offset(box, down);
        resolution.box = box;
        resolution.movement = ((up + x) + z) + down;
        return resolution;
    };

    Resolution collision = resolve(stuckInCollider);
    MotionVector resolved = collision.movement;
    bool penetrated = collision.penetrated;
    stuckInCollider = penetratedLastFrame && penetrated;
    penetratedLastFrame = penetrated;

    bool xCollision = requested.x != resolved.x;
    bool yCollision = requested.y != resolved.y;
    bool zCollision = requested.z != resolved.z;
    bool mayStep = onGround || (yCollision && requested.y < 0.0f);
    if (mayStep && (xCollision || zCollision)) {
        Resolution stepped = autoStep(stuckInCollider);
        bool stepBlocked = anyCollision(stepped.box);
        if (!stepBlocked && resolved.horizontalLengthSquared() < stepped.movement.horizontalLengthSquared()) {
            collision = stepped;
            resolved = stepped.movement;
        }
    }

    feet = feetOf(collision.box);
    xCollision = std::abs(requested.x - resolved.x) >= CollisionEpsilon;
    yCollision = std::abs(requested.y - resolved.y) >= CollisionEpsilon;
    zCollision = std::abs(requested.z - resolved.z) >= CollisionEpsilon;
    collideX = xCollision;
    collideY = yCollision;
    collideZ = zCollision;
    onGround = (yCollision && requested.y < 0.0f) || (onGround && !yCollision && std::abs(requested.y) <= CollisionEpsilon);
    velocity = resolved;
    updateSupportingBlock(requested);
}

MotionVector PlayerMotion::avoidEdge(const world::CollisionBox& box, MotionVector movement) const
{
    world::CollisionBox support { box.minX + EdgeInset, box.minY, box.minZ + EdgeInset, box.maxX - EdgeInset, box.maxY, box.maxZ - EdgeInset };
    auto supported = [&](float x, float z) {
        world::CollisionBox moved = offset(support, { x, -StepHeight * 1.01f, z });
        return anyCollision(moved);
    };
    auto reduce = [](float value) {
        if (value < EdgeStep && value >= -EdgeStep) {
            return 0.0f;
        }
        float next = value > 0.0f ? value - EdgeStep : value + EdgeStep;
        return next == value ? 0.0f : next;
    };
    float x = movement.x;
    float z = movement.z;
    int remainingX = 256;
    int remainingZ = 256;
    int remainingBoth = 256;
    while (x != 0.0f && !supported(x, 0.0f)) {
        if (--remainingX == 0) { x = 0.0f; break; }
        x = reduce(x);
    }
    while (z != 0.0f && !supported(0.0f, z)) {
        if (--remainingZ == 0) { z = 0.0f; break; }
        z = reduce(z);
    }
    while (x != 0.0f && z != 0.0f && !supported(x, z)) {
        if (--remainingBoth == 0) { x = z = 0.0f; break; }
        x = reduce(x);
        z = reduce(z);
    }
    return { x, movement.y, z };
}

void PlayerMotion::updateSupportingBlock(const MotionVector& requested)
{
    if (!onGround) {
        hasSupportingBlock = false;
        return;
    }
    world::CollisionBox probe = extend(boundingBox(), { 0.0f, -1.0E-3f, 0.0f });
    std::array<int32_t, 3> found {};
    bool any = findSupportingBlock(probe, found);
    if (!any) {
        probe = offset(probe, { -requested.x, 0.0f, -requested.z });
        any = findSupportingBlock(probe, found);
    }
    hasSupportingBlock = any;
    if (any) {
        supportingBlock = found;
    }
}

}
