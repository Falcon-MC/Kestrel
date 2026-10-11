#pragma once

#include "Protocol/Types/EntityDataMap.h"
#include "mod/Types.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace kestrel {

inline void applyFishingOwner(const EntityDataEntry& entry, int64_t& owner)
{
    if (entry.mId == 5 && entry.mFormat == EntityDataFormat::Long) owner = entry.mLongValue;
}

inline std::vector<std::pair<mod::Vec3, mod::Vec3>> fishingLine(const mod::Vec3& tip, const mod::Vec3& hook)
{
    const double dx = hook.x - tip.x, dy = hook.y - tip.y, dz = hook.z - tip.z;
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(distance) || distance < 1.0e-6 || distance > 64.0) return {};
    constexpr int Segments = 24;
    const double sag = std::min(distance * 0.04, 0.5);
    auto at = [&](double t) {
        return mod::Vec3 { tip.x + dx * t, tip.y + dy * t - sag * 4.0 * t * (1.0 - t), tip.z + dz * t };
    };
    std::vector<std::pair<mod::Vec3, mod::Vec3>> result;
    result.reserve(Segments);
    for (int i = 0; i < Segments; ++i) result.emplace_back(at(double(i) / Segments), at(double(i + 1) / Segments));
    return result;
}

}
