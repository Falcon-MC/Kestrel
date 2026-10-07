#pragma once

#include "Protocol/Types/EntityDataMap.h"

#include <array>

namespace kestrel {

inline void applyCrystalBeamTarget(const EntityDataEntry& entry, std::array<int32_t, 3>& target)
{
    if (entry.mId == 47 && entry.mFormat == EntityDataFormat::Vector3i) {
        target = { entry.mVector3iValue.x, entry.mVector3iValue.y, entry.mVector3iValue.z };
    }
}

}
