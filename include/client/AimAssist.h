#pragma once

#include "client/ActorTarget.h"
#include "Protocol/Types/CameraAimAssistTypes.h"

#include <functional>
#include <map>
#include <span>
#include <string_view>
#include <tuple>

namespace kestrel {

struct AimAssistBlock {
    ActorTargetBox box;
    std::string_view name;
    std::span<const std::string> tags;
    bool liquid = false;
};

struct AimAssistContext {
    std::array<double, 3> origin {};
    float yaw = 0.0f, pitch = 0.0f;
    uint64_t localRuntimeId = 0;
    std::array<int32_t, 3> localIndices {-1, -1, -1};
    std::string_view item;
    std::span<const ActorView> actors;
    std::function<std::optional<AimAssistBlock>(const std::array<int32_t, 3>&)> block;
};

struct AimAssistTarget {
    std::array<double, 3> point {};
    uint64_t runtimeId = 0;
    std::array<int32_t, 3> cell {};
    bool entity = false;
};

class AimAssist {
public:
    void reset(bool keepPresets = false);
    void apply(const std::vector<std::shared_ptr<const Packet>>& events);
    std::optional<AimAssistTarget> select(const AimAssistContext& context) const;
    bool enabled() const { return active; }

private:
    bool active = false;
    float angleX = 0.0f, angleY = 0.0f, distance = 0.0f;
    bool distanceMode = false;
    std::string presetId;
    std::vector<CameraAimAssistCategory> categories;
    std::vector<CameraAimAssistPresetDefinition> presets;
    std::map<std::tuple<int32_t, int32_t, int32_t>, int32_t> actorPriorities;
};

}
