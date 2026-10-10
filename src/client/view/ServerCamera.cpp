#include "client/ServerCamera.h"

#include "Protocol/Packets/CameraInstructionPacket.h"
#include "Protocol/Packets/CameraPresetsPacket.h"
#include "Protocol/Packets/CameraShakePacket.h"
#include "Protocol/Types/CameraPresets.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace kestrel {
namespace {
constexpr double Pi = 3.141592653589793;
float duration(float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 3600.0f) : 0.0f; }
float angle(float from, float to, float t) { return from + std::remainder(to - from, 360.0f) * t; }
bool finite(const Vector3f& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)
        && std::abs(v.x) <= 30000000.0f && std::abs(v.y) <= 30000000.0f && std::abs(v.z) <= 30000000.0f;
}
bool finite(const Vector2f& v) { return std::isfinite(v.x) && std::isfinite(v.y); }
std::array<double, 3> xyz(const Vector3f& v) { return { v.x, v.y, v.z }; }
bool standard(const std::string& name) {
    return name == "minecraft:first_person" || name == "minecraft:third_person"
        || name == "minecraft:third_person_front" || name == "minecraft:free"
        || name == "minecraft:follow_orbit" || name == "minecraft:fixed_boom";
}
float noise(uint32_t seed, float time) {
    auto sample = [&](uint32_t index) {
        uint32_t value = seed ^ (index * 0x9e3779b9u);
        value ^= value >> 16; value *= 0x7feb352du;
        value ^= value >> 15; value *= 0x846ca68bu; value ^= value >> 16;
        return float(value & 0xffffu) / 32767.5f - 1.0f;
    };
    float frame = time * 30.0f;
    uint32_t index = static_cast<uint32_t>(frame);
    float t = frame - float(index);
    t = t * t * (3.0f - 2.0f * t);
    return sample(index) * (1.0f - t) + sample(index + 1) * t;
}
}

void ServerCamera::reset(bool keepPresets)
{
    if (!keepPresets) { presets = standardCameraPresets(); overrides.clear(); }
    preset.reset(); set.reset(); transition.reset(); attached.reset(); target.reset();
    fade.reset(); shakes.clear(); lastPose.reset(); targetOffset = {}; shakeSequence = 0;
    snapTarget = false; targetBase = {}; orbitYawOffset = orbitPitchOffset = 0.0f;
}

std::optional<CameraPreset> ServerCamera::resolve(int32_t id) const
{
    if (id < 0 || size_t(id) >= presets.size()) return {};
    CameraPreset result;
    const CameraPreset* next = &presets[size_t(id)];
    std::set<std::string> seen;
    for (size_t depth = 0; next && depth < 32; ++depth) {
        if (!seen.insert(next->mIdentifier).second) return {};
#define INHERIT(Field) if (!result.mHas##Field && next->mHas##Field) { result.mHas##Field = true; result.m##Field = next->m##Field; }
        if (!result.mHasPosX && (next->mHasPos || next->mHasPosX)) { result.mHasPosX = true; result.mPos.x = next->mPos.x; }
        if (!result.mHasPosY && (next->mHasPos || next->mHasPosY)) { result.mHasPosY = true; result.mPos.y = next->mPos.y; }
        if (!result.mHasPosZ && (next->mHasPos || next->mHasPosZ)) { result.mHasPosZ = true; result.mPos.z = next->mPos.z; }
        result.mHasPos = result.mHasPosX && result.mHasPosY && result.mHasPosZ;
        INHERIT(Pitch) INHERIT(Yaw) INHERIT(ViewOffset) INHERIT(ActorOffset)
        INHERIT(Radius) INHERIT(Listener) INHERIT(PlayEffect) INHERIT(RotationSpeed)
        INHERIT(SnapToTarget) INHERIT(HorizontalRotationLimit) INHERIT(VerticalRotationLimit)
        INHERIT(ContinueTargeting) INHERIT(BlockListeningRadius) INHERIT(MinYawLimit) INHERIT(MaxYawLimit)
        INHERIT(StartingRotation) INHERIT(ControlScheme) INHERIT(AimAssistPreset)
#undef INHERIT
        result.mApplyInheritedStartingRotation |= next->mApplyInheritedStartingRotation;
        if (standard(next->mIdentifier)) { result.mIdentifier = next->mIdentifier; return result; }
        auto parent = std::find_if(presets.begin(), presets.end(), [&](const CameraPreset& p) { return p.mIdentifier == next->mParentPreset; });
        if (parent == presets.end() && standard(next->mParentPreset)) { result.mIdentifier = next->mParentPreset; return result; }
        next = parent == presets.end() ? nullptr : &*parent;
    }
    return {};
}

ServerCamera::Pose ServerCamera::desired(const ServerCameraContext& context) const
{
    Pose pose { context.base.eye, context.base.yaw, context.base.pitch };
    CameraSubject subject = context.player;
    if (attached) {
        if (auto actor = context.actor(*attached)) subject = *actor;
        else if (lastPose) return *lastPose;
    }
    if (preset && set) {
        const auto& p = *preset;
        std::optional<std::array<double, 3>> boomAnchor;
        bool free = p.mIdentifier == "minecraft:free";
        pose = { subject.eye, subject.yaw, subject.pitch };
        if (p.mIdentifier == "minecraft:follow_orbit" || p.mIdentifier == "minecraft:fixed_boom") pose.position = subject.center;
        if (p.mHasYaw && std::isfinite(p.mYaw)) pose.yaw = p.mYaw;
        if (p.mHasPitch && std::isfinite(p.mPitch)) pose.pitch = p.mPitch;
        if (p.mHasStartingRotation && finite(p.mStartingRotation)) {
            pose.pitch = p.mStartingRotation.x; pose.yaw = p.mStartingRotation.y;
        }
        if (set->mHasRot && finite(set->mRot)) { pose.pitch = set->mRot.x; pose.yaw = set->mRot.y; }
        if (p.mIdentifier == "minecraft:follow_orbit") {
            pose.yaw = context.player.yaw + orbitYawOffset;
            pose.pitch = context.player.pitch + orbitPitchOffset;
        }
        if (!free && p.mHasVerticalRotationLimit && finite(p.mVerticalRotationLimit))
            pose.pitch = std::clamp(pose.pitch, std::min(p.mVerticalRotationLimit.x, p.mVerticalRotationLimit.y), std::max(p.mVerticalRotationLimit.x, p.mVerticalRotationLimit.y));
        if (!free && p.mHasHorizontalRotationLimit && finite(p.mHorizontalRotationLimit)) {
            float offset = std::remainder(pose.yaw - subject.yaw, 360.0f);
            pose.yaw = subject.yaw + std::clamp(offset, std::min(p.mHorizontalRotationLimit.x, p.mHorizontalRotationLimit.y), std::max(p.mHorizontalRotationLimit.x, p.mHorizontalRotationLimit.y));
        }
        if (p.mHasMinYawLimit && p.mHasMaxYawLimit && std::isfinite(p.mMinYawLimit) && std::isfinite(p.mMaxYawLimit))
            pose.yaw = std::clamp(pose.yaw, std::min(p.mMinYawLimit, p.mMaxYawLimit), std::max(p.mMinYawLimit, p.mMaxYawLimit));
        std::array<double, 3> offset {};
        if (p.mHasActorOffset && finite(p.mActorOffset)) offset = xyz(p.mActorOffset);
        if (set->mHasActorOffset && finite(set->mActorOffset)) offset = xyz(set->mActorOffset);
        for (size_t axis = 0; axis < 3; ++axis) pose.position[axis] += offset[axis];
        if (free) {
            if (p.mHasPos && finite(p.mPos)) pose.position = xyz(p.mPos);
            if (set->mHasPos && finite(set->mPos)) pose.position = xyz(set->mPos);
        } else if (p.mIdentifier != "minecraft:first_person") {
            bool orbit = p.mIdentifier == "minecraft:follow_orbit" || p.mIdentifier == "minecraft:fixed_boom";
            double radius = p.mHasRadius && std::isfinite(p.mRadius) ? std::clamp(double(p.mRadius), 0.1, 100.0) : orbit ? 10.0 : 4.0;
            bool front = p.mIdentifier == "minecraft:third_person_front";
            double yaw = pose.yaw * Pi / 180.0, pitch = pose.pitch * Pi / 180.0;
            double direction = front ? 1.0 : -1.0;
            boomAnchor = pose.position;
            pose.position[0] += -std::sin(yaw) * std::cos(pitch) * radius * direction;
            pose.position[1] += -std::sin(pitch) * radius * direction;
            pose.position[2] += std::cos(yaw) * std::cos(pitch) * radius * direction;
            if (front) { pose.yaw += 180.0f; pose.pitch = -pose.pitch; }
        }
        if (set->mHasFacing && finite(set->mFacing)) {
            auto facing = xyz(set->mFacing);
            double dx = facing[0] - pose.position[0], dy = facing[1] - pose.position[1], dz = facing[2] - pose.position[2];
            if (dx * dx + dy * dy + dz * dz > 1e-12) {
                pose.yaw = float(std::atan2(-dx, dz) * 180.0 / Pi);
                pose.pitch = float(-std::atan2(dy, std::hypot(dx, dz)) * 180.0 / Pi);
            }
        }
        Vector2f view;
        bool hasView = false;
        if (p.mHasViewOffset && finite(p.mViewOffset)) { view = p.mViewOffset; hasView = true; }
        if (set->mHasViewOffset && finite(set->mViewOffset)) { view = set->mViewOffset; hasView = true; }
        if (hasView) {
            double yaw = pose.yaw * Pi / 180.0, pitch = pose.pitch * Pi / 180.0;
            pose.position[0] += -std::cos(yaw) * view.x - std::sin(yaw) * std::sin(pitch) * view.y;
            pose.position[1] += std::cos(pitch) * view.y;
            pose.position[2] += -std::sin(yaw) * view.x + std::cos(yaw) * std::sin(pitch) * view.y;
        }
        if (boomAnchor && context.obstruction) {
            const auto& anchor = *boomAnchor;
            std::array<double, 3> delta {};
            for (size_t axis = 0; axis < 3; ++axis) delta[axis] = pose.position[axis] - anchor[axis];
            double fraction = std::clamp(context.obstruction(anchor, delta), 0.0, 1.0);
            for (size_t axis = 0; axis < 3; ++axis) pose.position[axis] = anchor[axis] + delta[axis] * fraction;
        }
    } else if (attached) {
        pose = { subject.eye, subject.yaw, subject.pitch };
    }
    if (target) {
        auto rotation = focusRotation(context, pose);
        pose.yaw = rotation ? (*rotation)[0] : targetBase.yaw;
        pose.pitch = rotation ? (*rotation)[1] : targetBase.pitch;
    }
    return pose;
}

std::optional<std::array<float, 2>> ServerCamera::focusRotation(const ServerCameraContext& context, const Pose& pose) const
{
    if (!target) return {};
    auto focus = context.actor(*target);
    if (!focus) return {};
    double dx = focus->center[0] + targetOffset[0] - pose.position[0];
    double dy = focus->center[1] + targetOffset[1] - pose.position[1];
    double dz = focus->center[2] + targetOffset[2] - pose.position[2];
    double radius = preset && preset->mHasBlockListeningRadius && std::isfinite(preset->mBlockListeningRadius)
        ? std::max(0.0f, preset->mBlockListeningRadius) : 50.0;
    double distance = dx * dx + dy * dy + dz * dz;
    if (!std::isfinite(distance) || distance > radius * radius) return {};
    if (distance < 1e-12) return std::array<float, 2> { targetBase.yaw, targetBase.pitch };
    float yaw = float(std::atan2(-dx, dz) * 180.0 / Pi);
    float pitch = float(-std::atan2(dy, std::hypot(dx, dz)) * 180.0 / Pi);
    if (preset && preset->mHasHorizontalRotationLimit && finite(preset->mHorizontalRotationLimit)
        && preset->mHorizontalRotationLimit.x + preset->mHorizontalRotationLimit.y < 360.0f) {
        float offset = std::remainder(yaw - targetBase.yaw, 360.0f);
        if (offset < -preset->mHorizontalRotationLimit.x || offset > preset->mHorizontalRotationLimit.y) return {};
    }
    if (preset && preset->mHasVerticalRotationLimit && finite(preset->mVerticalRotationLimit)) {
        if (pitch < 90.0f - preset->mVerticalRotationLimit.y || pitch > 90.0f - preset->mVerticalRotationLimit.x) return {};
    }
    return std::array<float, 2> { yaw, pitch };
}

ServerCamera::Pose ServerCamera::current(const ServerCameraContext& context) const
{
    Pose pose = desired(context);
    if (transition && transition->duration > 0.0f) {
        float t = cameraEase(int(transition->ease), transition->elapsed / transition->duration);
        for (size_t axis = 0; axis < 3; ++axis) pose.position[axis] = transition->from.position[axis] + (pose.position[axis] - transition->from.position[axis]) * t;
        pose.yaw = angle(transition->from.yaw, pose.yaw, t);
        pose.pitch = transition->from.pitch + (pose.pitch - transition->from.pitch) * t;
    }
    return pose;
}

void ServerCamera::apply(const Packet& packet, const ServerCameraContext& context)
{
    if (auto registry = dynamic_cast<const CameraPresetsPacket*>(&packet)) {
        presets = registry->mPresets;
    } else if (auto shake = dynamic_cast<const CameraShakePacket*>(&packet)) {
        if (shake->mShakeAction == CameraShakePacket::ShakeAction::Stop) shakes.clear();
        else if (shake->mShakeAction == CameraShakePacket::ShakeAction::Add && std::isfinite(shake->mIntensity)
            && shake->mIntensity > 0.0f && duration(shake->mDuration) > 0.0f
            && (shake->mShakeType == CameraShakePacket::ShakeType::Positional || shake->mShakeType == CameraShakePacket::ShakeType::Rotational)) {
            if (shakes.size() == 32) shakes.erase(shakes.begin());
            shakes.push_back({ std::min(shake->mIntensity, 4.0f), duration(shake->mDuration), 0.0f,
                shake->mShakeType == CameraShakePacket::ShakeType::Rotational, ++shakeSequence });
        }
    } else if (auto instruction = dynamic_cast<const CameraInstructionPacket*>(&packet)) {
        Pose starting = current(context);
        if (instruction->mHasClear && instruction->mClear) {
            preset.reset(); set.reset(); transition.reset(); attached.reset(); target.reset(); lastPose.reset();
        }
        if (instruction->mHasDetachFromActor && instruction->mDetachFromActor) attached.reset();
        if (instruction->mHasRemoveTarget && instruction->mRemoveTarget) target.reset();
        if (instruction->mHasAttachInstruction) attached = instruction->mAttachInstruction.mUniqueActorId;
        if (instruction->mHasTargetInstruction) {
            const auto& focus = instruction->mTargetInstruction;
            target = focus.mUniqueActorId;
            targetOffset = focus.mHasTargetCenterOffset && finite(focus.mTargetCenterOffset) ? xyz(focus.mTargetCenterOffset) : std::array<double, 3> {};
            targetBase = starting;
            if (!lastPose) lastPose = starting;
            snapTarget = preset && preset->mSnapToTarget;
        }
        if (instruction->mHasSetInstruction) {
            const auto& requested = instruction->mSetInstruction;
            if (auto resolved = resolve(requested.mPresetRuntimeId)) {
                Pose from = starting;
                preset = std::move(resolved); set = requested;
                const auto& name = presets[size_t(requested.mPresetRuntimeId)].mIdentifier;
                if (requested.mHasDefaultPreset && requested.mDefaultPreset) overrides.erase(name);
                auto& stored = overrides[name];
                if (requested.mHasPos && finite(requested.mPos)) { stored.mHasPos = true; stored.mPos = requested.mPos; }
                if (requested.mHasRot && finite(requested.mRot)) { stored.mHasRot = true; stored.mRot = requested.mRot; }
                if (!set->mHasPos && stored.mHasPos) { set->mHasPos = true; set->mPos = stored.mPos; }
                if (!set->mHasRot && stored.mHasRot) { set->mHasRot = true; set->mRot = stored.mRot; }
                if (preset->mIdentifier == "minecraft:free") {
                    if (!set->mHasPos) {
                        set->mHasPos = true;
                        set->mPos = preset->mPos;
                    }
                    if (!set->mHasRot) {
                        set->mHasRot = true;
                        set->mRot.x = preset->mHasPitch ? preset->mPitch : 0.0f;
                        set->mRot.y = preset->mHasYaw ? preset->mYaw : 0.0f;
                        if (preset->mHasStartingRotation) set->mRot = preset->mStartingRotation;
                    }
                    if (set->mHasFacing && finite(set->mFacing) && finite(set->mPos)) {
                        double dx = double(set->mFacing.x) - set->mPos.x;
                        double dy = double(set->mFacing.y) - set->mPos.y;
                        double dz = double(set->mFacing.z) - set->mPos.z;
                        if (dx * dx + dy * dy + dz * dz > 1e-12) {
                            set->mRot.x = float(-std::atan2(dy, std::hypot(dx, dz)) * 180.0 / Pi);
                            set->mRot.y = float(std::atan2(-dx, dz) * 180.0 / Pi);
                            stored.mHasRot = true;
                            stored.mRot = set->mRot;
                        }
                        set->mHasFacing = false;
                    }
                }
                if (preset->mIdentifier == "minecraft:follow_orbit" || preset->mIdentifier == "minecraft:fixed_boom") {
                    float yaw = preset->mHasYaw ? preset->mYaw : context.player.yaw;
                    float pitch = preset->mHasPitch ? preset->mPitch : context.player.pitch;
                    if (preset->mHasStartingRotation) { yaw = preset->mStartingRotation.y; pitch = preset->mStartingRotation.x; }
                    if (set->mHasRot) { yaw = set->mRot.y; pitch = set->mRot.x; }
                    orbitYawOffset = yaw - context.player.yaw;
                    orbitPitchOffset = pitch - context.player.pitch;
                    if (preset->mIdentifier == "minecraft:fixed_boom") {
                        set->mHasRot = true; set->mRot.x = pitch; set->mRot.y = yaw;
                    }
                }
                transition = Transition { from, 0.0f, requested.mEase.mHasValue ? duration(requested.mEase.mTime) : 0.0f, requested.mEase.mEaseType };
                if (target) { auto savedTarget = target; target.reset(); targetBase = desired(context); target = savedTarget; snapTarget = preset->mSnapToTarget; }
            }
        }
        if (instruction->mHasFadeInstruction) {
            Fade next;
            const auto& requested = instruction->mFadeInstruction;
            if (requested.mHasTimeData) {
                next.in = std::min(duration(requested.mTimeData.mFadeInTime), 10.0f);
                next.hold = std::min(duration(requested.mTimeData.mWaitTime), 10.0f);
                next.out = std::min(duration(requested.mTimeData.mFadeOutTime), 10.0f);
            }
            if (requested.mHasColor) {
                const auto& c = requested.mColor;
                auto channel = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; };
                next.color = { channel(c.mRed), channel(c.mGreen), channel(c.mBlue) };
            }
            if (fade) {
                next.fromAlpha = float(fadeColor().a) / 255.0f;
                next.color = fade->color;
                if (fade->elapsed < fade->in) next.in = std::min(next.in, fade->in - fade->elapsed);
                else if (next.fromAlpha >= 1.0f) next.in = 0.0f;
                float outStart = std::max(next.in + next.hold, fade->in + fade->hold - fade->elapsed);
                float end = std::max(outStart + next.out, fade->in + fade->hold + fade->out - fade->elapsed);
                next.hold = outStart - next.in;
                next.out = end - outStart;
            }
            next.hold += std::max(0.0f, 0.5f - next.in - next.hold - next.out);
            fade = next;
        }
    }
}

void ServerCamera::update(FreeCamera& camera, const ServerCameraContext& context,
    const std::vector<std::shared_ptr<const Packet>>& events, float deltaSeconds)
{
    if (presets.empty()) presets = standardCameraPresets();
    camera.setServerRoll(0.0f);
    float dt = std::isfinite(deltaSeconds) ? std::max(deltaSeconds, 0.0f) : 0.0f;
    if (transition) transition->elapsed += dt;
    if (fade) { fade->elapsed += dt; if (fade->elapsed >= fade->in + fade->hold + fade->out) fade.reset(); }
    for (auto& shake : shakes) shake.elapsed += dt;
    std::erase_if(shakes, [](const Shake& shake) { return shake.elapsed >= shake.duration; });
    for (const auto& event : events) if (event) apply(*event, context);
    if (!preset && !attached && !target && shakes.empty()) {
        lastPose.reset();
        return;
    }
    Pose pose = current(context);
    if (target && !focusRotation(context, pose) && !(preset && preset->mContinueTargeting)) target.reset();
    if (target && snapTarget) {
        if (auto rotation = focusRotation(context, pose)) { pose.yaw = (*rotation)[0]; pose.pitch = (*rotation)[1]; }
    }
    if (target && preset && preset->mHasRotationSpeed && preset->mRotationSpeed > 0.0f && !snapTarget && lastPose && std::isfinite(preset->mRotationSpeed)) {
        float step = std::max(preset->mRotationSpeed, 0.0f) * dt;
        float yaw = std::remainder(pose.yaw - lastPose->yaw, 360.0f), pitch = pose.pitch - lastPose->pitch;
        float distance = std::hypot(yaw, pitch);
        float t = distance > step && distance > 0.0f ? step / distance : 1.0f;
        pose.yaw = lastPose->yaw + yaw * t;
        pose.pitch = lastPose->pitch + pitch * t;
    }
    lastPose = pose;
    snapTarget = false;
    float roll = 0.0f;
    for (const auto& shake : shakes) {
        float envelope = std::min(1.0f, (shake.duration - shake.elapsed) * 10.0f);
        float amplitude = shake.intensity * envelope;
        if (shake.rotational) {
            pose.yaw += noise(shake.seed, shake.elapsed) * amplitude * 2.0f;
            pose.pitch += noise(shake.seed + 17, shake.elapsed) * amplitude * 2.0f;
            roll += noise(shake.seed + 34, shake.elapsed) * amplitude * 2.0f;
        } else {
            for (size_t axis = 0; axis < 3; ++axis) pose.position[axis] += noise(shake.seed + uint32_t(axis) * 17, shake.elapsed) * amplitude * 0.1f;
        }
    }
    camera.setFacingSubject(false);
    camera.setPosition(pose.position[0], pose.position[1], pose.position[2]);
    camera.setRotation(pose.yaw, pose.pitch);
    camera.setServerRoll(roll);
}

bool ServerCamera::detached() const
{
    return attached.has_value() || (preset && preset->mIdentifier != "minecraft:first_person") || (transition && transition->elapsed < transition->duration);
}

int ServerCamera::renderPerspective(int fallback) const
{
    if (!preset) return fallback;
    if (preset->mIdentifier == "minecraft:first_person") return 0;
    if (preset->mIdentifier == "minecraft:third_person_front") return 2;
    if (preset->mIdentifier != "minecraft:free") return 1;
    return fallback;
}

bool ServerCamera::playerListener() const { return preset && preset->mHasListener && preset->mListener == CameraAudioListener::Player; }

bool ServerCamera::playerEffects() const
{
    return !preset || (preset->mHasPlayEffect ? preset->mPlayEffect : preset->mIdentifier != "minecraft:free");
}

ui::Color ServerCamera::fadeColor() const
{
    if (!fade) return { 0, 0, 0, 0 };
    float alpha = 1.0f;
    if (fade->elapsed < fade->in) alpha = fade->fromAlpha + (1.0f - fade->fromAlpha) * fade->elapsed / fade->in;
    else if (fade->elapsed >= fade->in + fade->hold)
        alpha = fade->out > 0.0f ? 1.0f - (fade->elapsed - fade->in - fade->hold) / fade->out : 0.0f;
    auto byte = [](float value) { return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return { byte(fade->color[0]), byte(fade->color[1]), byte(fade->color[2]), byte(alpha) };
}

}
