#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace kestrel::world {

struct MeshPriority {
    unsigned tier = 2;
    double distanceBand = 0;
    double facing = 0;
    double distanceSquared = 0;

    bool operator<(const MeshPriority& other) const
    {
        if (tier != other.tier) return tier < other.tier;
        if (distanceBand != other.distanceBand) return distanceBand < other.distanceBand;
        if (facing != other.facing) return facing > other.facing;
        return distanceSquared < other.distanceSquared;
    }
};

class MeshViewPriority {
public:
    MeshViewPriority(std::array<double, 3> position = {}, std::array<float, 3> direction = {}, bool startup = false)
        : position(position), startup(startup)
    {
        for (double& value : this->position) {
            if (!std::isfinite(value)) value = 0;
        }
        double length = std::hypot(double(direction[0]), double(direction[1]), double(direction[2]));
        if (std::isfinite(length) && length > 0) {
            for (std::size_t i = 0; i < 3; ++i) forward[i] = direction[i] / length;
        }
    }

    bool isStartup(int32_t x, int32_t z) const
    {
        return startup && std::abs(double(x) - std::floor(position[0] / 16)) <= 1
            && std::abs(double(z) - std::floor(position[2] / 16)) <= 1;
    }

    MeshPriority rank(int32_t x, int32_t y, int32_t z, bool urgent = false, bool refresh = false) const
    {
        double dx = double(x) * 16 + 8 - position[0];
        double dy = double(y) * 16 + 8 - position[1];
        double dz = double(z) * 16 + 8 - position[2];
        return rankOffset(dx, dy, dz, urgent, isStartup(x, z), refresh);
    }

    MeshPriority rankColumn(int32_t x, int32_t z) const
    {
        return rankOffset(double(x) * 16 + 8 - position[0], 0, double(z) * 16 + 8 - position[2], false, isStartup(x, z));
    }

private:
    MeshPriority rankOffset(double dx, double dy, double dz, bool urgent, bool startupTerrain, bool refresh = false) const
    {
        double distanceSquared = dx * dx + dy * dy + dz * dz;
        double distance = std::sqrt(distanceSquared);
        // One-section distance bands favour the view without starving closer terrain behind the player.
        double facing = distance > 0 ? (dx * forward[0] + dy * forward[1] + dz * forward[2]) / distance : 0;
        return { urgent ? 0u : startupTerrain ? 1u : refresh ? 3u : 2u,
            urgent ? 0 : std::floor(distance / 16), urgent ? 0 : facing, distanceSquared };
    }

    std::array<double, 3> position {};
    std::array<double, 3> forward {};
    bool startup = false;
};

}
