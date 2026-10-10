#pragma once

#include "Protocol/Types/EntityDataMap.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace kestrel {

struct ActorHitbox {
    std::array<double, 3> pivot {};
    std::array<double, 3> half {};
};

using ActorHitboxList = std::shared_ptr<const std::vector<ActorHitbox>>;
inline constexpr int32_t HitboxMetadataId = 118;
inline constexpr size_t MaxActorHitboxes = 4096;

inline void applyActorHitboxes(const EntityDataEntry& entry, ActorHitboxList& stored)
{
    if (entry.mId != HitboxMetadataId || entry.mFormat != EntityDataFormat::Nbt || !entry.mNbtValue.isCompound()) return;
    const Tag* list = entry.mNbtValue.get("Hitboxes");
    if (!list || !list->isList() || list->getListType() != Tag::Type::Compound) return;
    std::shared_ptr<std::vector<ActorHitbox>> updated;
    constexpr const char* names[3][3] = {
        { "MinX", "MaxX", "PivotX" }, { "MinY", "MaxY", "PivotY" }, { "MinZ", "MaxZ", "PivotZ" },
    };
    for (const Tag& box : list->getList()) {
        if (!box.isCompound()) continue;
        ActorHitbox decoded;
        bool valid = true;
        for (size_t axis = 0; axis < 3; ++axis) {
            double values[3] {};
            for (size_t field = 0; field < 3; ++field) {
                const Tag* value = box.get(names[axis][field]);
                if (value && value->getType() == Tag::Type::Float) values[field] = value->asFloat();
                valid &= std::isfinite(values[field]);
            }
            decoded.pivot[axis] = values[2];
            decoded.half[axis] = std::abs(values[1] - values[0]) * 0.5;
        }
        if (!valid) continue;
        if (!updated) {
            if (stored && stored->size() >= MaxActorHitboxes) return;
            updated = stored ? std::make_shared<std::vector<ActorHitbox>>(*stored) : std::make_shared<std::vector<ActorHitbox>>();
        }
        if (updated->size() == MaxActorHitboxes) break;
        updated->push_back(decoded);
    }
    // Metadata updates append; an empty or unreadable update keeps the previous boxes.
    if (updated) stored = std::move(updated);
}

}
