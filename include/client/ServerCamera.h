#pragma once

#include "client/Camera.h"
#include "Protocol/Types/CameraTypes.h"
#include "ui/Types.h"

#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <vector>

class Packet;

namespace kestrel {

float cameraEase(int type, float progress);

struct CameraSubject {
    std::array<double, 3> eye {};
    float yaw = 0.0f, pitch = 0.0f;
    std::array<double, 3> center {};
};

struct ServerCameraContext {
    CameraSubject player;
    CameraSubject base;
    std::function<std::optional<CameraSubject>(int64_t)> actor;
    std::function<double(const std::array<double, 3>&, const std::array<double, 3>&)> obstruction;
};

class ServerCamera {
public:
    void reset(bool keepPresets = false);
    void update(FreeCamera& camera, const ServerCameraContext& context,
        const std::vector<std::shared_ptr<const Packet>>& events, float deltaSeconds);
    bool detached() const;
    bool controlsPerspective() const { return preset.has_value(); }
    int renderPerspective(int fallback) const;
    bool orbital() const { return preset && preset->mIdentifier != "minecraft:free" && preset->mIdentifier != "minecraft:first_person"; }
    bool playerListener() const;
    bool playerEffects() const;
    ui::Color fadeColor() const;

private:
    struct Pose {
        std::array<double, 3> position {};
        float yaw = 0.0f, pitch = 0.0f;
    };
    struct Transition {
        Pose from;
        float elapsed = 0.0f, duration = 0.0f;
        CameraEase ease = CameraEase::Linear;
    };
    struct Fade {
        std::array<float, 3> color {};
        float in = 1.0f, hold = 0.5f, out = 1.0f, elapsed = 0.0f;
        float fromAlpha = 0.0f;
    };
    struct Shake {
        float intensity = 0.0f, duration = 0.0f, elapsed = 0.0f;
        bool rotational = false;
        uint32_t seed = 0;
    };
    std::vector<CameraPreset> presets;
    std::optional<CameraPreset> preset;
    std::optional<CameraSetInstruction> set;
    std::optional<Transition> transition;
    std::optional<int64_t> attached, target;
    std::array<double, 3> targetOffset {};
    std::optional<Fade> fade;
    std::vector<Shake> shakes;
    std::optional<Pose> lastPose;
    std::map<std::string, CameraSetInstruction> overrides;
    Pose targetBase;
    bool snapTarget = false;
    float orbitYawOffset = 0.0f, orbitPitchOffset = 0.0f;
    uint32_t shakeSequence = 0;

    std::optional<CameraPreset> resolve(int32_t id) const;
    Pose desired(const ServerCameraContext& context) const;
    Pose current(const ServerCameraContext& context) const;
    std::optional<std::array<float, 2>> focusRotation(const ServerCameraContext& context, const Pose& pose) const;
    void apply(const Packet& packet, const ServerCameraContext& context);
};

}
