#pragma once

#include "client/ActorExtent.h"
#include "client/RayBox.h"
#include "client/Session.h"

namespace kestrel {

struct ActorTargetBox {
    std::array<double, 3> low {}, high {};
};

template<class Visitor>
inline bool visitActorTargetBoxes(const ActorView& actor, const std::array<double, 3>& feet, Visitor&& visit)
{
    if (actor.hitboxes && !actor.hitboxes->empty()) {
        double nativeOffset = actor.identifier == "minecraft:player" ? 1.62
            : actor.identifier == "minecraft:falling_block" || actor.identifier == "minecraft:tnt" ? 0.49 : 0.0;
        for (const ActorHitbox& box : *actor.hitboxes) {
            ActorTargetBox world;
            for (size_t axis = 0; axis < 3; ++axis) {
                double center = feet[axis] + box.pivot[axis] + (axis == 1 ? nativeOffset : 0.0);
                world.low[axis] = center - box.half[axis];
                world.high[axis] = center + box.half[axis];
            }
            if (!visit(world)) return false;
        }
        return true;
    }
    double half = actorExtent(actor.width, 0.6f, actor.scale) * 0.5;
    double height = actorExtent(actor.height, 1.8f, actor.scale);
    return visit(ActorTargetBox { { feet[0] - half, feet[1], feet[2] - half }, { feet[0] + half, feet[1] + height, feet[2] + half } });
}

inline std::optional<double> actorTargetDistance(const ActorView& actor, const std::array<double, 3>& origin,
    const std::array<double, 3>& direction, double reach, double margin)
{
    std::optional<double> result;
    visitActorTargetBoxes(actor, { actor.x, actor.y, actor.z }, [&](ActorTargetBox box) {
        for (size_t axis = 0; axis < 3; ++axis) {
            box.low[axis] -= margin;
            box.high[axis] += margin;
        }
        result = actorRayDistance(origin, direction, box.low, box.high, reach, margin);
        return !result;
    });
    return result;
}

}
