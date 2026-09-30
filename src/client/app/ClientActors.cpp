#include "client/Client.h"

#include "platform/Window.h"
#include "render/Renderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <unordered_set>

namespace kestrel {

namespace {

constexpr double MaxActorDistance = 120.0;
constexpr uint32_t EntityQuadFlag = 1u << 5;
constexpr uint32_t AdditiveQuadFlag = 1u << 6;
constexpr uint32_t FullSkyLight = 0xF0F0F0F0u;
constexpr int SwimmingFlag = 57;
constexpr double MaxNameTagDistance = 64.0;
constexpr double NameTagLift = 0.5;
constexpr float NameTagPixelSize = 0.025f;
constexpr uint64_t SneakingFlag = 1ull << 1;
constexpr uint64_t UsingItemFlag = 1ull << 4;
constexpr double TicksPerSecond = 20.0;
constexpr uint64_t InvisibleFlag = 1ull << 5;
constexpr uint64_t CanShowNameFlag = 1ull << 14;
constexpr uint64_t AlwaysShowNameFlag = 1ull << 15;

int16_t roundToShort(float value)
{
    return static_cast<int16_t>(value >= 0.0f ? static_cast<int32_t>(value + 0.5f) : static_cast<int32_t>(value - 0.5f));
}

struct QuadCorner {
    std::array<float, 3> position {};
    std::array<float, 2> uv {};
};

world::ModelQuadGpu packCorners(const std::array<QuadCorner, 4>& corners, uint32_t layer, uint32_t shadeWord)
{
    world::ModelQuadGpu gpu;
    std::array<int16_t, 12> positions {};
    for (size_t corner = 0; corner < 4; ++corner) {
        for (size_t axis = 0; axis < 3; ++axis) {
            positions[corner * 3 + axis] = roundToShort(corners[corner].position[axis]);
        }
    }
    for (size_t word = 0; word < 6; ++word) {
        gpu.words[word] = uint32_t(uint16_t(positions[word * 2])) | (uint32_t(uint16_t(positions[word * 2 + 1])) << 16);
    }
    for (size_t corner = 0; corner < 4; ++corner) {
        uint32_t u = static_cast<uint32_t>(std::clamp(corners[corner].uv[0] * 4096.0f + 0.5f, 0.0f, 65535.0f));
        uint32_t v = static_cast<uint32_t>(std::clamp(corners[corner].uv[1] * 4096.0f + 0.5f, 0.0f, 65535.0f));
        gpu.words[6 + corner] = u | (v << 16);
    }
    gpu.words[10] = layer;
    gpu.words[11] = shadeWord;
    gpu.words[12] = FullSkyLight;
    return gpu;
}

/**
 * Pushes a quad whose texture may be spread over a grid of layers. Such a
 * quad is cut along the tile edges it crosses, each piece sampling the one
 * layer under it with its UVs moved into that tile.
 */
void appendTiled(std::array<QuadCorner, 4> corners, uint32_t layer, const world::EntityTileGrid& grid, uint32_t shadeWord, std::vector<world::ModelQuadGpu>& out)
{
    if (grid.single()) {
        out.push_back(packCorners(corners, layer, shadeWord));
        return;
    }
    uint32_t tilesX = grid.tilesX;
    uint32_t tilesY = grid.tilesY;
    for (QuadCorner& corner : corners) {
        corner.uv[0] *= grid.coverX;
        corner.uv[1] *= grid.coverY;
    }
    auto cuts = [&](const QuadCorner& from, const QuadCorner& to) {
        std::vector<float> list { 0.0f, 1.0f };
        for (size_t axis = 0; axis < 2; ++axis) {
            float a = from.uv[axis];
            float b = to.uv[axis];
            float tiles = static_cast<float>(axis == 0 ? tilesX : tilesY);
            if (std::abs(b - a) < 1.0e-6f) {
                continue;
            }
            float high = std::max(a, b) * tiles;
            for (float edge = std::floor(std::min(a, b) * tiles) + 1.0f; edge < high; edge += 1.0f) {
                float t = (edge / tiles - a) / (b - a);
                if (t > 1.0e-4f && t < 1.0f - 1.0e-4f) {
                    list.push_back(t);
                }
            }
        }
        std::sort(list.begin(), list.end());
        return list;
    };
    std::vector<float> across = cuts(corners[0], corners[1]);
    std::vector<float> down = cuts(corners[0], corners[3]);
    auto at = [&](float s, float t) {
        QuadCorner point;
        float weights[4] = { (1.0f - s) * (1.0f - t), s * (1.0f - t), s * t, (1.0f - s) * t };
        for (size_t corner = 0; corner < 4; ++corner) {
            for (size_t axis = 0; axis < 3; ++axis) {
                point.position[axis] += corners[corner].position[axis] * weights[corner];
            }
            point.uv[0] += corners[corner].uv[0] * weights[corner];
            point.uv[1] += corners[corner].uv[1] * weights[corner];
        }
        return point;
    };
    for (size_t row = 0; row + 1 < down.size(); ++row) {
        for (size_t column = 0; column + 1 < across.size(); ++column) {
            std::array<QuadCorner, 4> piece {
                at(across[column], down[row]), at(across[column + 1], down[row]),
                at(across[column + 1], down[row + 1]), at(across[column], down[row + 1]),
            };
            float centerU = (piece[0].uv[0] + piece[2].uv[0]) * 0.5f;
            float centerV = (piece[0].uv[1] + piece[2].uv[1]) * 0.5f;
            uint32_t tileX = std::min(static_cast<uint32_t>(std::max(centerU, 0.0f) * float(tilesX)), tilesX - 1);
            uint32_t tileY = std::min(static_cast<uint32_t>(std::max(centerV, 0.0f) * float(tilesY)), tilesY - 1);
            for (QuadCorner& corner : piece) {
                corner.uv[0] = std::clamp(corner.uv[0] * float(tilesX) - float(tileX), 0.0f, 1.0f);
                corner.uv[1] = std::clamp(corner.uv[1] * float(tilesY) - float(tileY), 0.0f, 1.0f);
            }
            out.push_back(packCorners(piece, layer + tileY * tilesX + tileX, shadeWord));
        }
    }
}

/**
 * Clip space of a point relative to the camera, x and y in normalized device
 * coordinates with y up, camera depth and buffer depth, or nothing outside
 * the camera's near/far planes.
 */
std::optional<std::array<double, 4>> project(const Mat4& matrix, double x, double y, double z)
{
    auto row = [&](size_t r) {
        return double(matrix[r]) * x + double(matrix[4 + r]) * y + double(matrix[8 + r]) * z + double(matrix[12 + r]);
    };
    double w = row(3);
    if (w <= 0.05) {
        return std::nullopt;
    }
    double depth = row(2) / w;
    if (depth < 0.0 || depth > 1.0) return std::nullopt;
    return std::array<double, 4> { row(0) / w, row(1) / w, w, depth };
}

std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

float wrapDegrees(float degrees)
{
    float wrapped = std::fmod(degrees + 180.0f, 360.0f);
    return (wrapped < 0.0f ? wrapped + 360.0f : wrapped) - 180.0f;
}

/**
 * A player's body trailing its head the way the game turns it, advanced by
 * ticks while it moves stepX and stepZ blocks a tick: it swings toward the
 * walking direction (facing forward while backing up), never lets the head
 * turn more than 75 degrees away, and creeps after the head once it is past
 * 50. Servers only send where players look, so every client works this out.
 */
float trailBody(float body, float head, double stepX, double stepZ, float ticks)
{
    if (stepX * stepX + stepZ * stepZ > 0.0025) {
        float heading = static_cast<float>(std::atan2(-stepX, stepZ) * 180.0 / 3.14159265358979);
        if (std::abs(wrapDegrees(head - heading)) > 95.0f) {
            heading += 180.0f;
        }
        body += wrapDegrees(heading - body) * (1.0f - std::pow(0.7f, ticks));
    }
    float turn = std::clamp(wrapDegrees(head - body), -75.0f, 75.0f);
    body = head - turn;
    if (std::abs(turn) > 50.0f) {
        body += turn * (1.0f - std::pow(0.8f, ticks));
    }
    return wrapDegrees(body);
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

world::BoneMatrix compose(const world::BoneMatrix& a, const world::BoneMatrix& b)
{
    world::BoneMatrix out {};
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            float sum = column == 3 ? a[row * 4 + 3] : 0.0f;
            for (size_t k = 0; k < 3; ++k) {
                sum += a[row * 4 + k] * b[k * 4 + column];
            }
            out[row * 4 + column] = sum;
        }
    }
    return out;
}

std::optional<world::BoneMatrix> invert(const world::BoneMatrix& m)
{
    const float a = m[0], b = m[1], c = m[2];
    const float d = m[4], e = m[5], f = m[6];
    const float g = m[8], h = m[9], i = m[10];
    float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(determinant) < 1.0e-8f) {
        return std::nullopt;
    }
    float s = 1.0f / determinant;
    world::BoneMatrix out {
        (e * i - f * h) * s, (c * h - b * i) * s, (b * f - c * e) * s, 0.0f,
        (f * g - d * i) * s, (a * i - c * g) * s, (c * d - a * f) * s, 0.0f,
        (d * h - e * g) * s, (b * g - a * h) * s, (a * e - b * d) * s, 0.0f,
    };
    for (size_t row = 0; row < 3; ++row) {
        out[row * 4 + 3] = -(out[row * 4] * m[3] + out[row * 4 + 1] * m[7] + out[row * 4 + 2] * m[11]);
    }
    return out;
}

}

/**
 * The quads of every entity close enough to the camera, placed around origin
 * in 1/256 block: every bone posed by the entity's animations, then the model
 * scaled and turned to its body yaw. Entity quads set bit 5 of the shade word
 * so they sample the entity textures, and bit 6 when their material adds
 * light; the rest take the light around the entity unless their render
 * controller ignores lighting. Quads whose material
 * blends go to blended. Zero scale entities draw
 * nothing; invisible ones hide their body but keep their armor and held item.
 */
void Client::buildActorQuads(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out, std::vector<world::ModelQuadGpu>& blended)
{
    if (!blockAssets) {
        animators.clear();
        return;
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
    bool facing = perspective == PerspectiveFront;
    float viewYaw = wrapDegrees(camera.minecraftYaw() + (facing ? 180.0f : 0.0f));
    float viewPitch = facing ? -camera.minecraftPitch() : camera.minecraftPitch();
    std::unordered_set<uint64_t> present;
    for (const ActorView& actor : actorViews) {
        present.insert(actor.runtimeId);
        if (actor.scale <= 0.0f) {
            continue;
        }
        bool invisible = (actor.flags[0] & InvisibleFlag) != 0;
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
        uint32_t light = lightCorners(actor.x, actor.y, actor.z);
        if (actor.identifier == "minecraft:item") {
            if (!invisible) {
                size_t first = out.size();
                appendDroppedItem(actor, origin, now, out);
                lightQuads(out, first, light);
            }
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
        input.hurtTime = actor.lastHurt > 0.0 ? static_cast<float>(std::clamp(10.0 - (now - actor.lastHurt) * 20.0, 0.0, 10.0)) : 0.0f;
        input.worldTime = worldTime;
        input.flags = actor.flags;
        input.variant = actor.variant;
        input.markVariant = actor.markVariant;
        input.color = actor.color;
        input.skinId = actor.skinId;
        input.identifier = actor.identifier;
        input.name = actor.name;
        input.onGround = actor.onGround;
        input.cameraX = camera.x();
        input.cameraY = camera.y();
        input.cameraZ = camera.z();
        input.cameraYaw = viewYaw;
        input.cameraPitch = viewPitch;
        if (actor.identifier == "minecraft:armor_stand") {
            input.engineVariables = { { "armor_stand.pose_index", double(actor.poseIndex) } };
        }
        input.itemUseTicks = actorItemUseTicks(actor, now);
        if (input.itemUseTicks > 0.0) {
            input.flags[0] |= UsingItemFlag;
        }
        if (actor.runtimeId == LocalActorId) {
            const HudItem& held = hudState.inventory[static_cast<size_t>(std::clamp(hudState.selectedSlot, 0, 8))];
            input.mainHandItem = held.empty() ? std::string() : held.identifier;
            input.engineVariables = {
                { "attack_time", swingProgress() },
                { "is_holding_right", held.empty() ? 0.0 : 1.0 },
                { "is_first_person", 0.0 },
            };
        } else {
            input.mainHandItem = actor.held.empty() ? std::string() : actor.held.identifier;
            input.engineVariables.push_back({ "is_holding_right", actor.held.empty() ? 0.0 : 1.0 });
            if (actor.lastSwing > 0.0) {
                input.engineVariables.push_back({ "attack_time", swingProgressSince(actor.lastSwing, now) });
            }
        }
        float& swimAmount = swimAmounts[actor.runtimeId];
        bool swimmingFlag = (actor.flags[SwimmingFlag / 64] >> (SwimmingFlag % 64)) & 1;
        float swimStep = static_cast<float>(std::min(now - lastActorTime, 0.25) * 2.0);
        swimAmount = std::clamp(swimAmount + (swimmingFlag ? swimStep : -swimStep), 0.0f, 1.0f);
        input.engineVariables.push_back({ "swim_amount", swimAmount });
        if (model->rigs.empty()) {
            continue;
        }
        bool combined = !model->combined.quads.empty() && actor.skinSlot == NoSkin;
        const world::EntityRenderController* controller = nullptr;
        for (const world::EntityRenderController& candidate : model->controllers) {
            if (candidate.condition.empty() || animator.evaluate(candidate.condition) != 0.0) {
                controller = &candidate;
                break;
            }
        }
        uint32_t rigIndex = controller ? pickChoice(animator, controller->geometry, controller->geometryChoices) : 0;
        const world::EntityRig* chosenRig = &model->rigs[rigIndex < model->rigs.size() ? rigIndex : 0];
        if (combined) {
            chosenRig = &model->combined;
        } else if (actor.skinSlot != NoSkin) {
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
        auto hiddenBones = [&](const world::EntityRenderController& source) {
            std::vector<uint8_t> hidden;
            if (source.parts.empty()) {
                return hidden;
            }
            std::vector<uint8_t> visible(source.parts.size(), 1);
            for (size_t rule = 0; rule < source.parts.size(); ++rule) {
                visible[rule] = animator.evaluate(source.parts[rule].visible) != 0.0 ? 1 : 0;
            }
            std::vector<int32_t>& lastRule = partMatches[{ &source, &rig }];
            if (lastRule.size() != rig.bones.size()) {
                lastRule.assign(rig.bones.size(), -1);
                for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                    for (size_t rule = 0; rule < source.parts.size(); ++rule) {
                        if (matchesPattern(source.parts[rule].pattern, rig.bones[bone].name)) {
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
            return hidden;
        };
        auto textureOf = [&](const world::EntityRenderController* source) {
            uint32_t chosen = source ? pickChoice(animator, source->texture, source->textureChoices) : world::NoEntityChoice;
            return chosen != world::NoEntityChoice ? chosen : model->layer;
        };
        float radians = (180.0f - wrapDegrees(actor.yaw)) * 3.14159265f / 180.0f;
        float cosine = std::cos(radians);
        float sine = std::sin(radians);
        float baseX = static_cast<float>(dx * 256.0);
        float baseY = static_cast<float>(dy * 256.0);
        float baseZ = static_cast<float>(dz * 256.0);
        std::array<float, 3> cameraLocal {
            static_cast<float>((camera.x() - origin[0]) * 256.0),
            static_cast<float>((camera.y() - origin[1]) * 256.0),
            static_cast<float>((camera.z() - origin[2]) * 256.0),
        };
        auto emit = [&](size_t index, uint32_t layer, const std::vector<uint8_t>& hidden, world::EntityBlend blend, bool oneSided, bool lit) {
            const world::ModelQuad& quad = rig.quads[index];
            size_t bone = index < rig.quadBones.size() ? rig.quadBones[index] : matrices.size();
            if (bone < hidden.size() && hidden[bone]) {
                return;
            }
            const world::BoneMatrix* matrix = bone < matrices.size() ? &matrices[bone] : nullptr;
            std::array<QuadCorner, 4> corners;
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
                corners[corner].position = { baseX + turnedX, baseY + y, baseZ + turnedZ };
                corners[corner].uv = { quad.uvs[corner][0] / 4096.0f, quad.uvs[corner][1] / 4096.0f };
            }
            bool inward = (quad.flags & world::QuadInward) != 0;
            if (inward || oneSided) {
                std::array<float, 3> edgeA {}, edgeB {}, toCamera {};
                for (size_t axis = 0; axis < 3; ++axis) {
                    edgeA[axis] = corners[1].position[axis] - corners[0].position[axis];
                    edgeB[axis] = corners[3].position[axis] - corners[0].position[axis];
                    toCamera[axis] = cameraLocal[axis] - corners[0].position[axis];
                }
                float facing = (edgeA[1] * edgeB[2] - edgeA[2] * edgeB[1]) * toCamera[0] + (edgeA[2] * edgeB[0] - edgeA[0] * edgeB[2]) * toCamera[1] + (edgeA[0] * edgeB[1] - edgeA[1] * edgeB[0]) * toCamera[2];
                // Quads wind with their normal into the cube, so an outer face shows
                // when the camera sits on the other side of it.
                if (inward ? facing <= 0.0f : facing >= 0.0f) {
                    return;
                }
            }
            uint32_t shadeWord = (quad.flags & world::QuadFaceMask) | EntityQuadFlag | (blend == world::EntityBlend::Additive ? AdditiveQuadFlag : 0u);
            if (actor.lastHurt > 0.0 && now - actor.lastHurt < 0.5) shadeWord |= 1u << 7;
            std::vector<world::ModelQuadGpu>& target = blend == world::EntityBlend::Opaque ? out : blended;
            size_t first = target.size();
            appendTiled(corners, layer, blockAssets->entityTileGrid(layer), shadeWord, target);
            if (lit) {
                lightQuads(target, first, light);
            }
        };
        if (!invisible && combined) {
            for (size_t index = 0; index < model->controllers.size(); ++index) {
                const world::EntityRenderController& source = model->controllers[index];
                if (!source.condition.empty() && animator.evaluate(source.condition) == 0.0) {
                    continue;
                }
                uint32_t picked = pickChoice(animator, source.geometry, source.geometryChoices);
                uint32_t layer = textureOf(&source);
                std::vector<uint8_t> hidden = hiddenBones(source);
                for (size_t quad = 0; quad < rig.quads.size(); ++quad) {
                    const world::CombinedQuadSource& from = model->combinedSources[quad];
                    if (from.controller == index && from.rig == picked) {
                        emit(quad, layer, hidden, source.blend, source.oneSided, !source.ignoreLighting);
                    }
                }
            }
        } else if (!invisible) {
            uint32_t layer = actor.skinSlot != NoSkin ? blockAssets->skinLayerBase() + actor.skinSlot : textureOf(controller);
            std::vector<uint8_t> hidden = controller ? hiddenBones(*controller) : std::vector<uint8_t> {};
            world::EntityBlend blend = controller ? controller->blend : world::EntityBlend::Opaque;
            bool oneSided = controller && controller->oneSided;
            bool lit = !controller || !controller->ignoreLighting;
            for (size_t index = 0; index < rig.quads.size(); ++index) {
                emit(index, layer, hidden, blend, oneSided, lit);
            }
        }
        size_t firstWorn = out.size();
        auto toWorld = [&](const std::array<float, 3>& posed) {
            std::array<float, 3> p { posed[0] * scale, posed[1] * scale, posed[2] * scale };
            return std::array<float, 3> { baseX + cosine * p[0] + sine * p[2], baseY + p[1], baseZ - sine * p[0] + cosine * p[2] };
        };
        if (actor.identifier == "minecraft:player") {
            bool hurt = actor.lastHurt > 0.0 && now - actor.lastHurt < 0.5;
            appendArmor(actor.armor, rig, matrices, toWorld, hurt ? 1u << 7 : 0u, out);
        }
        if (actor.runtimeId == LocalActorId) {
            appendThirdPersonItem(hudState.inventory[static_cast<size_t>(std::clamp(hudState.selectedSlot, 0, 8))], input.itemUseTicks, bodyAttachable, rig, matrices, toWorld, out);
        } else if (actor.identifier == "minecraft:player" && !actor.held.empty()) {
            appendThirdPersonItem(actor.held, input.itemUseTicks, actorAttachables[actor.runtimeId], rig, matrices, toWorld, out);
        }
        lightQuads(out, firstWorn, light);
    }
    for (auto it = animators.begin(); it != animators.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = animators.erase(it);
        }
    }
    for (auto it = swimAmounts.begin(); it != swimAmounts.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = swimAmounts.erase(it);
        }
    }
    for (auto it = actorAttachables.begin(); it != actorAttachables.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = actorAttachables.erase(it);
        }
    }
    for (auto it = actorItemUseSince.begin(); it != actorItemUseSince.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = actorItemUseSince.erase(it);
        }
    }
    lastActorTime = now;
}

/**
 * The armor a humanoid wears, drawn with the vanilla armor models. Their bones
 * share the humanoid's names, so each armor bone follows the pose of the
 * wearer's bone of the same name, whatever geometry the skin brings.
 */
void Client::appendArmor(const std::array<std::string, 4>& armor, const world::EntityRig& rig, const std::vector<world::BoneMatrix>& matrices, const std::function<std::array<float, 3>(const std::array<float, 3>&)>& toWorld, uint32_t shadeFlags, std::vector<world::ModelQuadGpu>& out, const std::vector<uint8_t>* shownBones)
{
    for (size_t slot = 0; slot < armor.size(); ++slot) {
        if (armor[slot].empty()) {
            continue;
        }
        world::ArmorLook look = blockAssets->armorLook(slot, armor[slot]);
        if (!look.rig) {
            continue;
        }
        std::vector<int32_t>& wearer = armorBoneMatches[{ look.rig, &rig }];
        if (wearer.size() != look.rig->bones.size()) {
            wearer.assign(look.rig->bones.size(), -1);
            for (size_t piece = 0; piece < look.rig->bones.size(); ++piece) {
                std::string name = look.rig->bones[piece].name;
                for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                    if (matchesPattern(lowercase(name), rig.bones[bone].name)) {
                        wearer[piece] = static_cast<int32_t>(bone);
                        break;
                    }
                }
            }
        }
        world::EntityTileGrid grid = blockAssets->entityTileGrid(look.layer);
        for (size_t index = 0; index < look.rig->quads.size(); ++index) {
            size_t piece = index < look.rig->quadBones.size() ? look.rig->quadBones[index] : wearer.size();
            int32_t bone = piece < wearer.size() ? wearer[piece] : -1;
            if (bone < 0 || size_t(bone) >= matrices.size() || (shownBones && !(*shownBones)[size_t(bone)])) {
                continue;
            }
            const world::BoneMatrix& m = matrices[size_t(bone)];
            const world::ModelQuad& quad = look.rig->quads[index];
            std::array<QuadCorner, 4> corners;
            for (size_t corner = 0; corner < 4; ++corner) {
                float x = quad.positions[corner][0] / 16.0f;
                float y = quad.positions[corner][1] / 16.0f;
                float z = quad.positions[corner][2] / 16.0f;
                corners[corner].position = toWorld({
                    m[0] * x + m[1] * y + m[2] * z + m[3],
                    m[4] * x + m[5] * y + m[6] * z + m[7],
                    m[8] * x + m[9] * y + m[10] * z + m[11],
                });
                corners[corner].uv = { quad.uvs[corner][0] / 4096.0f, quad.uvs[corner][1] / 4096.0f };
            }
            appendTiled(corners, look.layer, grid, (quad.flags & world::QuadFaceMask) | EntityQuadFlag | shadeFlags, out);
        }
    }
}

/**
 * How long the local player has been using the selected item, in ticks with
 * the fraction of the current one.
 */
double Client::localItemUseTicks() const
{
    // A use that just started still counts as under way.
    return hudState.itemUseStarted > 0.0 ? std::max((secondsNow() - hudState.itemUseStarted) * TicksPerSecond, 1.0e-3) : 0.0;
}

/**
 * How long an entity has been using its held item. Other players only come
 * with the using item flag, so their use is timed from when it came on.
 */
double Client::actorItemUseTicks(const ActorView& actor, double now)
{
    if (actor.runtimeId == LocalActorId) {
        return localItemUseTicks();
    }
    if (!(actor.flags[0] & UsingItemFlag)) {
        actorItemUseSince.erase(actor.runtimeId);
        return 0.0;
    }
    double since = actorItemUseSince.try_emplace(actor.runtimeId, now).first->second;
    return std::max((now - since) * TicksPerSecond, 1.0e-3);
}

/**
 * The held item's attachable from a server pack, drawn on the holder the way
 * the game binds it: the bone with a binding hangs from the holder's right
 * item bone instead of its own parents, and the bones under it follow. An
 * attachable without a binding hangs from that bone as a whole. Returns false
 * when the item has no attachable, so the caller draws it as usual.
 */
bool Client::appendAttachable(const HudItem& held, double itemUseTicks, const world::EntityRig& holder, const std::vector<world::BoneMatrix>& holderMatrices, bool firstPerson, HeldAttachable& state, const std::function<std::array<float, 3>(const std::array<float, 3>&)>& toWorld, std::vector<world::ModelQuadGpu>& out)
{
    const world::EntityModel* model = held.empty() || !blockAssets ? nullptr : blockAssets->attachableModel(held.identifier);
    if (!model || model->rigs.empty()) {
        return false;
    }
    int32_t itemBone = -1;
    for (size_t bone = 0; bone < holder.bones.size() && bone < holderMatrices.size(); ++bone) {
        if (lowercase(holder.bones[bone].name) == "rightitem") {
            itemBone = static_cast<int32_t>(bone);
        }
    }
    if (itemBone < 0) {
        return false;
    }
    if (state.identifier != held.identifier) {
        state = HeldAttachable {};
        state.identifier = held.identifier;
    }
    state.bones = model->rigs.front().bones;
    const std::array<float, 3>& holderPivot = holder.bones[size_t(itemBone)].pivot;
    for (world::EntityBone& bone : state.bones) {
        if (bone.anchoredToHolder) {
            bone.pivot = holderPivot;
        }
    }
    world::AnimationInput input;
    input.now = secondsNow();
    input.worldTime = currentWorldTime(timeState);
    input.identifier = held.identifier;
    input.mainHandItem = held.identifier;
    input.itemUseTicks = itemUseTicks;
    input.contextVariables = { { "is_first_person", firstPerson ? 1.0 : 0.0 }, { "item_slot", 0.0 } };
    state.animator.update(model->scripts.get(), &blockAssets->animationLibrary(), state.bones, input);
    const std::vector<world::BoneMatrix>& matrices = state.animator.matrices();
    if (matrices.size() != state.bones.size()) {
        return true;
    }
    // The render controller picks the frame, like the bow's pull stages, by geometry and texture.
    const world::EntityRig* chosenRig = &model->rigs.front();
    uint32_t layer = model->layer;
    for (const world::EntityRenderController& controller : model->controllers) {
        if (!controller.condition.empty() && state.animator.evaluate(controller.condition) == 0.0) {
            continue;
        }
        uint32_t rigIndex = pickChoice(state.animator, controller.geometry, controller.geometryChoices);
        if (rigIndex < model->rigs.size() && model->rigs[rigIndex].bones.size() == state.bones.size()) {
            chosenRig = &model->rigs[rigIndex];
        }
        if (uint32_t chosen = pickChoice(state.animator, controller.texture, controller.textureChoices); chosen != world::NoEntityChoice) {
            layer = chosen;
        }
        break;
    }
    const world::EntityRig& rig = *chosenRig;

    int32_t bound = -1;
    for (size_t bone = 0; bone < rig.bones.size() && bound < 0; ++bone) {
        if (rig.bones[bone].bound) {
            bound = static_cast<int32_t>(bone);
        }
    }
    world::BoneMatrix detach { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    if (bound >= 0 && rig.bones[size_t(bound)].parent >= 0) {
        std::optional<world::BoneMatrix> inverse = invert(matrices[size_t(rig.bones[size_t(bound)].parent)]);
        if (!inverse) {
            return true;
        }
        detach = *inverse;
    }
    world::BoneMatrix hand = compose(holderMatrices[size_t(itemBone)], detach);
    std::vector<uint8_t> attached(rig.bones.size(), bound < 0 ? 1 : 0);
    for (size_t bone = 0; bone < rig.bones.size() && bound >= 0; ++bone) {
        for (int32_t walker = static_cast<int32_t>(bone), steps = 0; walker >= 0 && steps <= int32_t(rig.bones.size()); walker = rig.bones[size_t(walker)].parent, ++steps) {
            if (walker == bound) {
                attached[bone] = 1;
                break;
            }
        }
    }
    world::EntityTileGrid grid = blockAssets->entityTileGrid(layer);
    for (size_t index = 0; index < rig.quads.size(); ++index) {
        size_t bone = index < rig.quadBones.size() ? rig.quadBones[index] : rig.bones.size();
        if (bone >= rig.bones.size() || !attached[bone]) {
            continue;
        }
        world::BoneMatrix m = compose(hand, matrices[bone]);
        const world::ModelQuad& quad = rig.quads[index];
        std::array<float, 3> anchor = state.bones[bone].anchoredToHolder ? holderPivot : std::array<float, 3> {};
        std::array<QuadCorner, 4> corners;
        for (size_t corner = 0; corner < 4; ++corner) {
            float x = quad.positions[corner][0] / 16.0f + anchor[0];
            float y = quad.positions[corner][1] / 16.0f + anchor[1];
            float z = quad.positions[corner][2] / 16.0f + anchor[2];
            corners[corner].position = toWorld({
                m[0] * x + m[1] * y + m[2] * z + m[3],
                m[4] * x + m[5] * y + m[6] * z + m[7],
                m[8] * x + m[9] * y + m[10] * z + m[11],
            });
            corners[corner].uv = { quad.uvs[corner][0] / 4096.0f, quad.uvs[corner][1] / 4096.0f };
        }
        appendTiled(corners, layer, grid, (quad.flags & world::QuadFaceMask) | EntityQuadFlag, out);
    }
    return true;
}

/**
 * Replaces every entity's network position and rotation with its smoothed
 * one. A new sample starts a glide from the displayed state toward it, lasting
 * as long as the gap since the previous sample (one to three ticks); rotations
 * take the shortest way round. New entities, teleports and jumps longer than
 * eight blocks snap.
 */
/**
 * Names over entities the way the game floats them: players whose name may
 * show, and anything flagged to always show its name, which is how servers
 * put floating text in the world. Each sits half a block over the entity's
 * box. Glyphs have a fixed world size on a camera-facing plane, sorted back
 * to front, with the same projected depth as that plane.
 */
std::vector<menu::NameTag> Client::buildNameTags() const
{
    std::vector<std::pair<double, menu::NameTag>> placed;
    float scale = guiScale();
    float width = static_cast<float>(window->width());
    float height = static_cast<float>(window->height());
    Mat4 matrix = camera.viewProjection(width / std::max(height, 1.0f));
    float focalPixels = height / (2.0f * camera.halfVerticalTangent(width / std::max(height, 1.0f)));
    for (const ActorView& actor : actorViews) {
        if (actor.name.empty()) {
            continue;
        }
        bool player = actor.identifier == "minecraft:player";
        bool sneaking = player && (actor.flags[0] & SneakingFlag) != 0;
        if (player && (actor.flags[0] & InvisibleFlag) != 0) continue;
        bool always = actor.alwaysShowName || (actor.flags[0] & AlwaysShowNameFlag) != 0;
        if (!always && (!player || (actor.flags[0] & CanShowNameFlag) == 0 || (actor.flags[0] & InvisibleFlag) != 0)) {
            continue;
        }
        std::array<double, 3> feet { actor.x, actor.y, actor.z };
        if (auto motion = motions.find(actor.runtimeId); motion != motions.end()) {
            feet = motion->second.shown;
        }
        double box = actor.height > 0.0f ? actor.height : player ? 1.8 * actor.scale : 0.0;
        double dx = feet[0] - camera.x();
        double dy = feet[1] + box + NameTagLift - camera.y();
        double dz = feet[2] - camera.z();
        double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance > (sneaking ? 32.0 : MaxNameTagDistance)) {
            continue;
        }
        auto anchor = project(matrix, dx, dy, dz);
        if (!anchor) {
            continue;
        }
        menu::NameTag tag;
        tag.text = actor.name;
        tag.x = static_cast<float>(((*anchor)[0] + 1.0) * 0.5 * width) / scale;
        tag.y = static_cast<float>((1.0 - (*anchor)[1]) * 0.5 * height) / scale;
        tag.magnify = NameTagPixelSize * focalPixels / (static_cast<float>((*anchor)[2]) * scale);
        tag.depth = static_cast<float>((*anchor)[3]);
        tag.sneaking = sneaking;
        placed.emplace_back((*anchor)[2], std::move(tag));
    }
    std::sort(placed.begin(), placed.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<menu::NameTag> tags;
    tags.reserve(placed.size());
    for (auto& [distance, tag] : placed) {
        tags.push_back(std::move(tag));
    }
    return tags;
}

/**
 * The local player as the third person views draw it, its body trailing the
 * head like every other player's.
 */
ActorView Client::localActorView(float deltaSeconds)
{
    ActorView self;
    self.runtimeId = LocalActorId;
    self.lastHurt = hudState.lastHurt;
    self.identifier = "minecraft:player";
    self.x = eyePosition[0];
    self.y = eyePosition[1] - playerView.eyeHeight();
    self.z = eyePosition[2];
    self.headYaw = camera.minecraftYaw();
    self.pitch = camera.minecraftPitch();
    self.flags[0] = (playerView.sneaking ? 1ull << 1 : 0) | (playerView.sprinting ? 1ull << 3 : 0) | (playerView.swimming ? 1ull << SwimmingFlag : 0);
    self.skinSlot = localSkinSlot;
    self.slim = localSlim;
    for (size_t slot = 0; slot < self.armor.size(); ++slot) {
        self.armor[slot] = hudState.armor[slot].empty() ? std::string() : hudState.armor[slot].identifier;
    }

    double dx = playerView.current[0] - playerView.previous[0];
    double dz = playerView.current[2] - playerView.previous[2];
    localBodyYaw = trailBody(localBodyYaw, self.headYaw, dx, dz, deltaSeconds * 20.0f);
    self.yaw = localBodyYaw;
    return self;
}

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
            motion.bodyYaw = actor.yaw;
            motion.lastFrame = now;
            motion.lastShown = target;
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
        float ticks = static_cast<float>(std::min(now - motion.lastFrame, 0.25) * 20.0);
        if (actor.identifier == "minecraft:player") {
            if (ticks > 0.0f) {
                double stepX = (motion.shown[0] - motion.lastShown[0]) / ticks;
                double stepZ = (motion.shown[2] - motion.lastShown[2]) / ticks;
                motion.bodyYaw = trailBody(motion.bodyYaw, actor.headYaw, stepX, stepZ, ticks);
            }
            actor.yaw = motion.bodyYaw;
        }
        motion.lastShown = motion.shown;
        motion.lastFrame = now;
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
