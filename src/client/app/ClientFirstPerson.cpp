#include "client/Client.h"


#include "render/Renderer.h"
#include "world/BlockAssets.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace kestrel {

namespace {

constexpr float Pi = 3.14159265f;
constexpr double SwingSeconds = 0.3;
constexpr double EquipSeconds = 0.15;
constexpr uint32_t EntityQuadFlag = 1u << 5;
constexpr uint32_t FullSkyLight = 0xF0F0F0F0u;
constexpr float HandFovDegrees = 70.0f;
constexpr float HeldCubeSize = 0.5f;
constexpr float HeldItemSize = 0.8f;
constexpr std::array<float, 3> HeldOffset { 0.12f, -0.23f, 0.0f };
constexpr uint32_t ItemGrid = world::ItemIconSize;

using Vec3 = std::array<float, 3>;

int16_t roundToShort(float value)
{
    return static_cast<int16_t>(value >= 0.0f ? static_cast<int32_t>(value + 0.5f) : static_cast<int32_t>(value - 0.5f));
}

std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

Vec3 add(const Vec3& a, const Vec3& b)
{
    return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
}

Vec3 scaled(const Vec3& a, float s)
{
    return { a[0] * s, a[1] * s, a[2] * s };
}

/**
 * The camera's right, up and backward axes in world space for a Minecraft yaw
 * and pitch in degrees.
 */
std::array<Vec3, 3> cameraAxes(float yawDegrees, float pitchDegrees)
{
    float yaw = yawDegrees * Pi / 180.0f;
    float pitch = pitchDegrees * Pi / 180.0f;
    Vec3 forward { -std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch) };
    Vec3 right { -std::cos(yaw), 0.0f, -std::sin(yaw) };
    Vec3 up {
        right[1] * forward[2] - right[2] * forward[1],
        right[2] * forward[0] - right[0] * forward[2],
        right[0] * forward[1] - right[1] * forward[0],
    };
    return { right, up, scaled(forward, -1.0f) };
}

/**
 * Rotates a point by Euler angles in degrees, x first, then y, then z.
 */
Vec3 rotate(const Vec3& point, const Vec3& degrees)
{
    Vec3 p = point;
    float ax = degrees[0] * Pi / 180.0f;
    float ay = degrees[1] * Pi / 180.0f;
    float az = degrees[2] * Pi / 180.0f;
    p = { p[0], p[1] * std::cos(ax) - p[2] * std::sin(ax), p[1] * std::sin(ax) + p[2] * std::cos(ax) };
    p = { p[0] * std::cos(ay) + p[2] * std::sin(ay), p[1], -p[0] * std::sin(ay) + p[2] * std::cos(ay) };
    p = { p[0] * std::cos(az) - p[1] * std::sin(az), p[0] * std::sin(az) + p[1] * std::cos(az), p[2] };
    return p;
}

/**
 * Packs a quad whose corners are in 1/256 block around the draw origin.
 */
world::ModelQuadGpu packQuad(const std::array<Vec3, 4>& corners, const std::array<std::array<uint16_t, 2>, 4>& uvs, uint32_t material, uint32_t shadeWord)
{
    world::ModelQuadGpu gpu;
    std::array<int16_t, 12> positions {};
    for (size_t corner = 0; corner < 4; ++corner) {
        for (size_t axis = 0; axis < 3; ++axis) {
            positions[corner * 3 + axis] = roundToShort(corners[corner][axis]);
        }
    }
    for (size_t word = 0; word < 6; ++word) {
        gpu.words[word] = uint32_t(uint16_t(positions[word * 2])) | (uint32_t(uint16_t(positions[word * 2 + 1])) << 16);
    }
    for (size_t corner = 0; corner < 4; ++corner) {
        gpu.words[6 + corner] = uint32_t(uvs[corner][0]) | (uint32_t(uvs[corner][1]) << 16);
    }
    gpu.words[10] = material;
    gpu.words[11] = shadeWord;
    gpu.words[12] = FullSkyLight;
    return gpu;
}

}

uint32_t Client::heldItemLayer() const
{
    return blockAssets ? blockAssets->skinLayerBase() + world::SkinSlots : 0;
}

/**
 * The local player's arm and held item as the game draws them in first
 * person: the player model runs its own first person animations (arm pose,
 * swing, walking bob, equip dip, breathing), shows its right arm only while
 * the hand is empty, and the held item hangs from the right item bone,
 * blocks as a cube and other items as their texture extruded one pixel deep.
 */
void Client::appendFirstPerson(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out)
{
    if (!blockAssets || !playerView.active || !worldShown || perspective != PerspectiveFirst) {
        return;
    }
    const world::EntityModel* model = localSlim ? blockAssets->entityModel("minecraft:player#slim") : nullptr;
    if (!model) {
        model = blockAssets->entityModel("minecraft:player");
    }
    if (!model || model->rigs.empty()) {
        return;
    }
    const world::EntityRig* chosenRig = &model->rigs.front();
    uint32_t skinLayer = model->layer;
    if (localSkinSlot != NoSkin) {
        if (auto skinRig = skinRigs.find(localSkinSlot); skinRig != skinRigs.end() && skinRig->second) {
            chosenRig = skinRig->second.get();
        }
        skinLayer = blockAssets->skinLayerBase() + localSkinSlot;
    }
    const world::EntityRig& rig = *chosenRig;
    double now = secondsNow();
    const InputState& keys = window->input();
    if (menu.capturesMouse() && keys.mousePressed) {
        swingStart = now;
    }
    double swing = swingStart >= 0.0 ? (now - swingStart) / SwingSeconds : 1.0;
    float attackTime = swing < 1.0 ? static_cast<float>(swing) : 0.0f;

    int32_t slot = std::clamp(hudState.selectedSlot, 0, 8);
    const HudItem& held = hudState.inventory[static_cast<size_t>(slot)];
    std::string heldName = held.empty() ? std::string() : held.identifier;
    std::string heldIdentity = heldName + "#" + std::to_string(held.aux);
    if (heldIdentity != lastHeldIdentity) {
        lastHeldIdentity = heldIdentity;
        heldChangedAt = now;
    }
    float armHeight = static_cast<float>(std::clamp((now - heldChangedAt) / EquipSeconds, 0.0, 1.0));

    float yaw = camera.minecraftYaw();
    float pitch = camera.minecraftPitch();
    double eye = playerView.sneaking ? 1.54 : 1.62;
    world::AnimationInput input;
    input.x = camera.x();
    input.y = camera.y() - eye;
    input.z = camera.z();
    input.yaw = yaw;
    input.headYaw = yaw;
    input.pitch = pitch;
    input.now = now;
    input.worldTime = currentWorldTime(timeState);
    input.identifier = "minecraft:player";
    input.onGround = true;
    input.mainHandItem = heldName;
    input.offHandItem = hudState.offhand.empty() ? std::string() : hudState.offhand.identifier;
    input.engineVariables = {
        { "is_first_person", 1.0 },
        { "attack_time", attackTime },
        { "player_arm_height", armHeight },
        { "is_holding_right", heldName.empty() ? 0.0 : 1.0 },
        { "is_holding_left", 0.0 },
        { "bob_animation", 1.0 },
        { "is_using_vr", 0.0 },
        { "is_paperdoll", 0.0 },
        { "map_face_icon", 0.0 },
        { "short_arm_offset_right", 0.0 },
        { "short_arm_offset_left", 0.0 },
        { "player_x_rotation", pitch },
        { "is_horizontal_splitscreen", 0.0 },
        { "is_vertical_splitscreen", 0.0 },
    };
    handAnimator.update(model->scripts.get(), &blockAssets->animationLibrary(), rig.bones, input);
    const std::vector<world::BoneMatrix>& matrices = handAnimator.matrices();
    if (matrices.size() != rig.bones.size()) {
        return;
    }

    int32_t itemBone = -1;
    int32_t bodyBone = -1;
    std::vector<uint8_t> shown(rig.bones.size(), 0);
    for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
        std::string name = lowercase(rig.bones[bone].name);
        if (name == "rightitem") {
            itemBone = static_cast<int32_t>(bone);
        }
        if (name == "body") {
            bodyBone = static_cast<int32_t>(bone);
        }
        if (heldName.empty() && (name == "rightarm" || name == "rightsleeve")) {
            shown[bone] = 1;
        }
    }
    if (bodyBone < 0) {
        return;
    }
    const world::BoneMatrix& body = matrices[static_cast<size_t>(bodyBone)];
    std::array<float, 9> inverse {};
    {
        const float a = body[0], b = body[1], c = body[2];
        const float d = body[4], e = body[5], f = body[6];
        const float g = body[8], h = body[9], i = body[10];
        float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
        if (std::abs(determinant) < 1.0e-8f) {
            return;
        }
        float s = 1.0f / determinant;
        inverse = {
            (e * i - f * h) * s, (c * h - b * i) * s, (b * f - c * e) * s,
            (f * g - d * i) * s, (a * i - c * g) * s, (c * d - a * f) * s,
            (d * h - e * g) * s, (b * g - a * h) * s, (a * e - b * d) * s,
        };
    }
    const Vec3& neck = rig.bones[static_cast<size_t>(bodyBone)].pivot;
    std::array<Vec3, 3> axes = cameraAxes(yaw, pitch);
    Vec3 eyePoint {
        static_cast<float>((camera.x() - origin[0]) * 256.0),
        static_cast<float>((camera.y() - origin[1]) * 256.0),
        static_cast<float>((camera.z() - origin[2]) * 256.0),
    };
    float unit = handAnimator.scale() * 16.0f;
    float aspect = static_cast<float>(window->width()) / static_cast<float>(std::max<uint32_t>(window->height(), 1));
    float handZoom = camera.halfVerticalTangent(aspect) / std::tan(HandFovDegrees * 0.5f * Pi / 180.0f);
    auto modelToWorld = [&](const world::BoneMatrix& m, const Vec3& pixels) {
        float x = pixels[0] / 16.0f;
        float y = pixels[1] / 16.0f;
        float z = pixels[2] / 16.0f;
        Vec3 posed {
            m[0] * x + m[1] * y + m[2] * z + m[3] - body[3],
            m[4] * x + m[5] * y + m[6] * z + m[7] - body[7],
            m[8] * x + m[9] * y + m[10] * z + m[11] - body[11],
        };
        Vec3 local {
            inverse[0] * posed[0] + inverse[1] * posed[1] + inverse[2] * posed[2] - neck[0],
            inverse[3] * posed[0] + inverse[4] * posed[1] + inverse[5] * posed[2] - neck[1],
            inverse[6] * posed[0] + inverse[7] * posed[1] + inverse[8] * posed[2] - neck[2],
        };
        Vec3 offset = add(add(scaled(axes[0], -local[0] * handZoom), scaled(axes[1], local[1] * handZoom)), scaled(axes[2], -local[2]));
        return add(eyePoint, scaled(offset, unit));
    };
    for (size_t index = 0; index < rig.quads.size(); ++index) {
        size_t bone = index < rig.quadBones.size() ? rig.quadBones[index] : rig.bones.size();
        if (bone >= rig.bones.size() || !shown[bone]) {
            continue;
        }
        const world::ModelQuad& quad = rig.quads[index];
        std::array<Vec3, 4> corners;
        for (size_t corner = 0; corner < 4; ++corner) {
            corners[corner] = modelToWorld(matrices[bone], { float(quad.positions[corner][0]), float(quad.positions[corner][1]), float(quad.positions[corner][2]) });
        }
        out.push_back(packQuad(corners, quad.uvs, skinLayer,(quad.flags & world::QuadFaceMask) | EntityQuadFlag));
    }
    if (heldName.empty() || itemBone < 0) {
        return;
    }

    const world::EntityBone& anchor = rig.bones[static_cast<size_t>(itemBone)];
    Vec3 center = modelToWorld(matrices[static_cast<size_t>(itemBone)], scaled(anchor.pivot, 16.0f));
    float root = std::sqrt(attackTime);
    Vec3 swingTurn {
        -std::sin(root * 25.0f * Pi / 180.0f) * 70.0f,
        -std::sin(root * 75.0f * Pi / 180.0f) * 15.0f,
        -std::sin(root * 80.0f * Pi / 180.0f) * 35.0f,
    };
    const world::BlockVisual* cube = blockAssets->itemCube(heldName);
    Vec3 pose = cube ? Vec3 { 12.0f, -35.0f, 0.0f } : Vec3 { 0.0f, -70.0f, 20.0f };
    auto place = [&](const Vec3& local) {
        Vec3 turned = add(rotate(rotate(local, pose), swingTurn), HeldOffset);
        Vec3 world = add(add(scaled(axes[0], turned[0] * handZoom), scaled(axes[1], turned[1] * handZoom)), scaled(axes[2], turned[2]));
        return add(center, scaled(world, 256.0f));
    };
    auto emit = [&](const std::array<Vec3, 4>& local, const std::array<std::array<uint16_t, 2>, 4>& uvs, uint32_t material, uint32_t shadeWord) {
        std::array<Vec3, 4> corners;
        std::array<Vec3, 4> reversed;
        std::array<std::array<uint16_t, 2>, 4> reversedUvs;
        for (size_t corner = 0; corner < 4; ++corner) {
            corners[corner] = place(local[corner]);
            reversed[3 - corner] = corners[corner];
            reversedUvs[3 - corner] = uvs[corner];
        }
        out.push_back(packQuad(corners, uvs, material, shadeWord));
        out.push_back(packQuad(reversed, reversedUvs, material, shadeWord));
    };
    const std::array<std::array<uint16_t, 2>, 4> fullUv { { { 0, 0 }, { 4096, 0 }, { 4096, 4096 }, { 0, 4096 } } };

    if (cube) {
        float h = HeldCubeSize * 0.5f;
        struct Side {
            world::Face face;
            std::array<Vec3, 4> corners;
        };
        const Side sides[] = {
            { world::Face::PositiveY, { { { -h, h, -h }, { h, h, -h }, { h, h, h }, { -h, h, h } } } },
            { world::Face::NegativeY, { { { -h, -h, h }, { h, -h, h }, { h, -h, -h }, { -h, -h, -h } } } },
            { world::Face::PositiveZ, { { { -h, h, h }, { h, h, h }, { h, -h, h }, { -h, -h, h } } } },
            { world::Face::NegativeZ, { { { h, h, -h }, { -h, h, -h }, { -h, -h, -h }, { h, -h, -h } } } },
            { world::Face::PositiveX, { { { h, h, h }, { h, h, -h }, { h, -h, -h }, { h, -h, h } } } },
            { world::Face::NegativeX, { { { -h, h, -h }, { -h, h, h }, { -h, -h, h }, { -h, -h, -h } } } },
        };
        const std::vector<world::Material>& materials = blockAssets->materials();
        for (const Side& side : sides) {
            uint32_t material = cube->faces[static_cast<size_t>(side.face)];
            uint32_t tint = material < materials.size() && materials[material].tintKind() != world::TintKind::None ? 0x6BBD7Cu : 0u;
            emit(side.corners, fullUv, material, (uint32_t(side.face) + 1) | (tint << 8));
        }
        return;
    }

    uint32_t layer = heldItemLayer();
    std::string key = heldName + "#" + std::to_string(held.aux) + "#" + held.icon;
    std::vector<uint8_t>& icon = heldIcon;
    if (key != heldItemKey) {
        heldItemKey = key;
        icon = blockAssets->itemIcon(held.identifier, held.aux, held.icon);
        std::vector<uint8_t> pixels(size_t(world::EntityTextureSize) * world::EntityTextureSize * 4, 0);
        if (icon.size() == size_t(ItemGrid) * ItemGrid * 4) {
            for (uint32_t y = 0; y < world::EntityTextureSize; ++y) {
                for (uint32_t x = 0; x < world::EntityTextureSize; ++x) {
                    size_t source = (size_t(y * ItemGrid / world::EntityTextureSize) * ItemGrid + x * ItemGrid / world::EntityTextureSize) * 4;
                    std::copy_n(icon.data() + source, 4, pixels.data() + (size_t(y) * world::EntityTextureSize + x) * 4);
                }
            }
        }
        renderer->updateEntityTexture(layer, pixels.data());
    }
    if (icon.size() != size_t(ItemGrid) * ItemGrid * 4) {
        return;
    }
    float h = HeldItemSize * 0.5f;
    float pixel = HeldItemSize / static_cast<float>(ItemGrid);
    float depth = HeldItemSize / 32.0f;
    uint32_t shade = EntityQuadFlag;
    emit({ { { -h, h, depth }, { h, h, depth }, { h, -h, depth }, { -h, -h, depth } } }, fullUv, layer, shade | 6);
    emit({ { { -h, h, -depth }, { h, h, -depth }, { h, -h, -depth }, { -h, -h, -depth } } }, fullUv, layer, shade | 5);
    auto opaque = [&](int32_t x, int32_t y) {
        if (x < 0 || y < 0 || x >= int32_t(ItemGrid) || y >= int32_t(ItemGrid)) {
            return false;
        }
        return icon[(size_t(y) * ItemGrid + size_t(x)) * 4 + 3] >= 26;
    };
    for (int32_t y = 0; y < int32_t(ItemGrid); ++y) {
        for (int32_t x = 0; x < int32_t(ItemGrid); ++x) {
            if (!opaque(x, y)) {
                continue;
            }
            float left = -h + x * pixel;
            float right = left + pixel;
            float top = h - y * pixel;
            float bottom = top - pixel;
            uint16_t u0 = static_cast<uint16_t>(x * 4096 / ItemGrid);
            uint16_t u1 = static_cast<uint16_t>(u0 + 4096 / ItemGrid / 2);
            uint16_t v0 = static_cast<uint16_t>(y * 4096 / ItemGrid);
            uint16_t v1 = static_cast<uint16_t>(v0 + 4096 / ItemGrid / 2);
            std::array<std::array<uint16_t, 2>, 4> texel { { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } } };
            if (!opaque(x - 1, y)) {
                emit({ { { left, top, -depth }, { left, top, depth }, { left, bottom, depth }, { left, bottom, -depth } } }, texel, layer, shade | 3);
            }
            if (!opaque(x + 1, y)) {
                emit({ { { right, top, depth }, { right, top, -depth }, { right, bottom, -depth }, { right, bottom, depth } } }, texel, layer, shade | 4);
            }
            if (!opaque(x, y - 1)) {
                emit({ { { left, top, -depth }, { right, top, -depth }, { right, top, depth }, { left, top, depth } } }, texel, layer, shade | 2);
            }
            if (!opaque(x, y + 1)) {
                emit({ { { left, bottom, depth }, { right, bottom, depth }, { right, bottom, -depth }, { left, bottom, -depth } } }, texel, layer, shade | 1);
            }
        }
    }
}

}
