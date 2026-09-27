#include "client/Client.h"

#include "platform/Window.h"
#include "render/Renderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace kestrel {

namespace {

constexpr double MaxActorDistance = 120.0;
constexpr uint32_t EntityQuadFlag = 1u << 5;
constexpr uint32_t FullSkyLight = 0xF0F0F0F0u;

int16_t roundToShort(float value)
{
    return static_cast<int16_t>(value >= 0.0f ? static_cast<int32_t>(value + 0.5f) : static_cast<int32_t>(value - 0.5f));
}

float wrapDegrees(float degrees)
{
    float wrapped = std::fmod(degrees + 180.0f, 360.0f);
    return (wrapped < 0.0f ? wrapped + 360.0f : wrapped) - 180.0f;
}

/**
 * Case insensitive match of a bone name against a lowercase pattern where '*'
 * stands for any run of characters.
 */
bool matchesPattern(const std::string& pattern, const std::string& name)
{
    size_t p = 0;
    size_t n = 0;
    size_t star = std::string::npos;
    size_t resume = 0;
    while (n < name.size()) {
        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(name[n])));
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            resume = n;
        } else if (p < pattern.size() && pattern[p] == c) {
            ++p;
            ++n;
        } else if (star != std::string::npos) {
            p = star + 1;
            n = ++resume;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

/**
 * The choice an entity's selector expression lands on, clamped into the
 * choice list; NoEntityChoice when there is none.
 */
uint32_t pickChoice(world::EntityAnimator& animator, const world::molang::Script& selector, const std::vector<uint32_t>& choices)
{
    if (choices.empty()) {
        return world::NoEntityChoice;
    }
    double value = animator.evaluate(selector);
    size_t index = std::isfinite(value) && value > 0.0 ? std::min(static_cast<size_t>(value), choices.size() - 1) : 0;
    return choices[index];
}

}

/**
 * The quads of every entity close enough to the camera, placed around origin
 * in 1/256 block: every bone posed by the entity's animations, then the model
 * scaled and turned to its body yaw. Entity quads set bit 5 of the shade word
 * so they sample the entity textures.
 */
std::vector<world::ModelQuadGpu> Client::buildActorQuads(const std::array<int32_t, 3>& origin)
{
    std::vector<world::ModelQuadGpu> out;
    if (!blockAssets) {
        animators.clear();
        return out;
    }
    double now = secondsNow();
    double worldTime = currentWorldTime(timeState);
    ++actorFrame;
    WorldView cullView;
    float aspect = static_cast<float>(window->width()) / static_cast<float>(std::max<uint32_t>(window->height(), 1));
    cullView.viewProjection = camera.viewProjection(aspect);
    cullView.cameraX = camera.x();
    cullView.cameraY = camera.y();
    cullView.cameraZ = camera.z();
    ChunkFrustum frustum(cullView);
    std::unordered_set<uint64_t> present;
    for (const ActorView& actor : actorViews) {
        present.insert(actor.runtimeId);
        double dx = actor.x - origin[0];
        double dy = actor.y - origin[1];
        double dz = actor.z - origin[2];
        if (std::abs(dx) > MaxActorDistance || std::abs(dy) > MaxActorDistance || std::abs(dz) > MaxActorDistance) {
            continue;
        }
        int32_t blockX = static_cast<int32_t>(std::floor(actor.x));
        int32_t blockY = static_cast<int32_t>(std::floor(actor.y));
        int32_t blockZ = static_cast<int32_t>(std::floor(actor.z));
        if (!frustum.contains(cullView, blockX - 8, blockY - 7, blockZ - 8)) {
            continue;
        }
        const world::EntityModel* model = actor.slim ? blockAssets->entityModel(actor.identifier + "#slim") : nullptr;
        if (!model) {
            model = blockAssets->entityModel(actor.identifier);
        }
        if (!model) {
            continue;
        }
        world::EntityAnimator& animator = animators[actor.runtimeId];
        world::AnimationInput input;
        input.x = actor.x;
        input.y = actor.y;
        input.z = actor.z;
        input.yaw = actor.yaw;
        input.headYaw = actor.headYaw;
        input.pitch = actor.pitch;
        input.now = now;
        input.worldTime = worldTime;
        input.flags = actor.flags;
        input.variant = actor.variant;
        input.markVariant = actor.markVariant;
        input.color = actor.color;
        input.skinId = actor.skinId;
        input.identifier = actor.identifier;
        input.name = actor.name;
        input.onGround = actor.onGround;
        if (model->rigs.empty()) {
            continue;
        }
        const world::EntityRenderController* controller = nullptr;
        for (const world::EntityRenderController& candidate : model->controllers) {
            if (candidate.condition.empty() || animator.evaluate(candidate.condition) != 0.0) {
                controller = &candidate;
                break;
            }
        }
        uint32_t rigIndex = controller ? pickChoice(animator, controller->geometry, controller->geometryChoices) : 0;
        const world::EntityRig* chosenRig = &model->rigs[rigIndex < model->rigs.size() ? rigIndex : 0];
        if (actor.skinSlot != NoSkin) {
            if (auto skinRig = skinRigs.find(actor.skinSlot); skinRig != skinRigs.end() && skinRig->second) {
                chosenRig = skinRig->second.get();
            }
        }
        const world::EntityRig& rig = *chosenRig;
        double cameraDistance = std::sqrt((actor.x - camera.x()) * (actor.x - camera.x()) + (actor.y - camera.y()) * (actor.y - camera.y()) + (actor.z - camera.z()) * (actor.z - camera.z()));
        uint64_t interval = cameraDistance < 16.0 ? 1 : cameraDistance < 32.0 ? 2 : cameraDistance < 64.0 ? 4 : 8;
        bool stale = animator.matrices().size() != rig.bones.size();
        if (stale || (actorFrame + actor.runtimeId) % interval == 0) {
            Profiler::Section section(profiler, "  animation");
            animator.update(model->scripts.get(), &blockAssets->animationLibrary(), rig.bones, input);
        }
        const std::vector<world::BoneMatrix>& matrices = animator.matrices();
        float scale = animator.scale() * actor.scale * 16.0f;
        uint32_t layer = model->layer;
        std::vector<uint8_t> hidden;
        if (controller) {
            uint32_t chosen = pickChoice(animator, controller->texture, controller->textureChoices);
            if (chosen != world::NoEntityChoice) {
                layer = chosen;
            }
            if (!controller->parts.empty()) {
                std::vector<uint8_t> visible(controller->parts.size(), 1);
                for (size_t rule = 0; rule < controller->parts.size(); ++rule) {
                    visible[rule] = animator.evaluate(controller->parts[rule].visible) != 0.0 ? 1 : 0;
                }
                std::vector<int32_t>& lastRule = partMatches[{ controller, &rig }];
                if (lastRule.size() != rig.bones.size()) {
                    lastRule.assign(rig.bones.size(), -1);
                    for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                        for (size_t rule = 0; rule < controller->parts.size(); ++rule) {
                            if (matchesPattern(controller->parts[rule].pattern, rig.bones[bone].name)) {
                                lastRule[bone] = static_cast<int32_t>(rule);
                            }
                        }
                    }
                }
                std::vector<uint8_t> own(rig.bones.size(), 0);
                for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                    if (lastRule[bone] >= 0) {
                        own[bone] = visible[lastRule[bone]] ? 0 : 1;
                    }
                }
                hidden.assign(rig.bones.size(), 0);
                for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                    int32_t walker = static_cast<int32_t>(bone);
                    for (size_t steps = 0; walker >= 0 && steps <= rig.bones.size(); ++steps) {
                        if (own[walker]) {
                            hidden[bone] = 1;
                            break;
                        }
                        walker = rig.bones[walker].parent;
                    }
                }
            }
        }
        if (actor.skinSlot != NoSkin) {
            layer = blockAssets->skinLayerBase() + actor.skinSlot;
        }
        float radians = (180.0f - wrapDegrees(actor.yaw)) * 3.14159265f / 180.0f;
        float cosine = std::cos(radians);
        float sine = std::sin(radians);
        float baseX = static_cast<float>(dx * 256.0);
        float baseY = static_cast<float>(dy * 256.0);
        float baseZ = static_cast<float>(dz * 256.0);
        out.reserve(out.size() + rig.quads.size());
        for (size_t index = 0; index < rig.quads.size(); ++index) {
            const world::ModelQuad& quad = rig.quads[index];
            size_t bone = index < rig.quadBones.size() ? rig.quadBones[index] : matrices.size();
            if (bone < hidden.size() && hidden[bone]) {
                continue;
            }
            const world::BoneMatrix* matrix = bone < matrices.size() ? &matrices[bone] : nullptr;
            world::ModelQuadGpu gpu;
            std::array<int16_t, 12> positions {};
            for (size_t corner = 0; corner < 4; ++corner) {
                float x = quad.positions[corner][0] / 16.0f;
                float y = quad.positions[corner][1] / 16.0f;
                float z = quad.positions[corner][2] / 16.0f;
                if (matrix) {
                    const world::BoneMatrix& m = *matrix;
                    float px = m[0] * x + m[1] * y + m[2] * z + m[3];
                    float py = m[4] * x + m[5] * y + m[6] * z + m[7];
                    float pz = m[8] * x + m[9] * y + m[10] * z + m[11];
                    x = px;
                    y = py;
                    z = pz;
                }
                x *= scale;
                y *= scale;
                z *= scale;
                float turnedX = cosine * x + sine * z;
                float turnedZ = -sine * x + cosine * z;
                positions[corner * 3 + 0] = roundToShort(baseX + turnedX);
                positions[corner * 3 + 1] = roundToShort(baseY + y);
                positions[corner * 3 + 2] = roundToShort(baseZ + turnedZ);
            }
            for (size_t word = 0; word < 6; ++word) {
                gpu.words[word] = uint32_t(uint16_t(positions[word * 2])) | (uint32_t(uint16_t(positions[word * 2 + 1])) << 16);
            }
            for (size_t corner = 0; corner < 4; ++corner) {
                gpu.words[6 + corner] = uint32_t(quad.uvs[corner][0]) | (uint32_t(quad.uvs[corner][1]) << 16);
            }
            gpu.words[10] = layer;
            gpu.words[11] = (quad.flags & world::QuadFaceMask) | EntityQuadFlag;
            gpu.words[12] = FullSkyLight;
            out.push_back(gpu);
        }
    }
    for (auto it = animators.begin(); it != animators.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = animators.erase(it);
        }
    }
    return out;
}

/**
 * Replaces every entity's network position and rotation with its smoothed
 * one. A new sample starts a glide from the displayed state toward it, lasting
 * as long as the gap since the previous sample (one to three ticks); rotations
 * take the shortest way round. New entities, teleports and jumps longer than
 * eight blocks snap.
 */
void Client::interpolateActors(double now)
{
    constexpr double MinGlide = 0.05;
    constexpr double MaxGlide = 0.15;
    constexpr double SnapDistance = 8.0;
    std::unordered_set<uint64_t> present;
    for (ActorView& actor : actorViews) {
        present.insert(actor.runtimeId);
        std::array<double, 3> target { actor.x, actor.y, actor.z };
        std::array<float, 3> turn { actor.yaw, actor.headYaw, actor.pitch };
        auto [entry, created] = motions.try_emplace(actor.runtimeId);
        ActorMotion& motion = entry->second;
        double jump = std::sqrt((target[0] - motion.shown[0]) * (target[0] - motion.shown[0]) + (target[1] - motion.shown[1]) * (target[1] - motion.shown[1]) + (target[2] - motion.shown[2]) * (target[2] - motion.shown[2]));
        if (created || actor.teleports != motion.teleports || jump > SnapDistance) {
            motion.from = target;
            motion.to = target;
            motion.shown = target;
            motion.turnFrom = turn;
            motion.turnTo = turn;
            motion.turnShown = turn;
            motion.start = now;
            motion.duration = 0.0;
            motion.lastSample = now;
            motion.moves = actor.moves;
            motion.teleports = actor.teleports;
        } else if (actor.moves != motion.moves) {
            motion.from = motion.shown;
            motion.to = target;
            motion.turnFrom = motion.turnShown;
            motion.turnTo = turn;
            motion.duration = std::clamp(now - motion.lastSample, MinGlide, MaxGlide);
            motion.start = now;
            motion.lastSample = now;
            motion.moves = actor.moves;
        }
        double t = motion.duration > 0.0 ? std::clamp((now - motion.start) / motion.duration, 0.0, 1.0) : 1.0;
        for (size_t axis = 0; axis < 3; ++axis) {
            motion.shown[axis] = motion.from[axis] + (motion.to[axis] - motion.from[axis]) * t;
            float delta = wrapDegrees(motion.turnTo[axis] - motion.turnFrom[axis]);
            motion.turnShown[axis] = wrapDegrees(motion.turnFrom[axis] + delta * static_cast<float>(t));
        }
        actor.x = motion.shown[0];
        actor.y = motion.shown[1];
        actor.z = motion.shown[2];
        actor.yaw = motion.turnShown[0];
        actor.headYaw = motion.turnShown[1];
        actor.pitch = motion.turnShown[2];
    }
    for (auto it = motions.begin(); it != motions.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = motions.erase(it);
        }
    }
}

}
