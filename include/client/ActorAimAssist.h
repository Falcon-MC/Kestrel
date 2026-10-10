#pragma once

#include "Protocol/Types/EntityDataMap.h"

#include <array>
#include <cstdint>

namespace kestrel {

inline void applyActorAimAssist(const EntityDataEntry& entry, std::array<int32_t, 3>& indices)
{
    if (entry.mFormat == EntityDataFormat::Int && entry.mId >= 136 && entry.mId <= 138) {
        indices[size_t(entry.mId - 136)] = entry.mIntValue;
    }
}

}
