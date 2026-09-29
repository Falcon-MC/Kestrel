#pragma once

#include "mod/Types.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kestrel::mod {

class World {
public:
    virtual ~World() = default;

    virtual ConnectionState state() const = 0;
    virtual std::string serverName() const = 0;
    virtual std::string serverAddress() const = 0;
    virtual std::string levelName() const = 0;
    virtual int dimension() const = 0;
    // Ticks, 24000 per day.
    virtual int64_t time() const = 0;
    virtual float rain() const = 0;
    virtual float thunder() const = 0;

    virtual std::vector<Entity> entities() const = 0;
    virtual std::optional<Entity> entity(uint64_t runtimeId) const = 0;
    virtual std::optional<TargetBlock> targetBlock() const = 0;
    // Names in the player list.
    virtual std::vector<std::string> players() const = 0;
    virtual Sidebar sidebar() const = 0;

    std::optional<Entity> nearestEntity(const Vec3& from, double radius, const std::function<bool(const Entity&)>& accept = {}) const
    {
        std::optional<Entity> best;
        double bestDistance = radius * radius;
        for (Entity& candidate : entities()) {
            double dx = candidate.position.x - from.x;
            double dy = candidate.position.y - from.y;
            double dz = candidate.position.z - from.z;
            double distance = dx * dx + dy * dy + dz * dz;
            if (distance <= bestDistance && (!accept || accept(candidate))) {
                bestDistance = distance;
                best = std::move(candidate);
            }
        }
        return best;
    }
};

}
