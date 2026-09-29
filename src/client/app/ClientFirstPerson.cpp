#include "client/Client.h"


#include "render/Renderer.h"
#include "world/BlockAssets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>

namespace kestrel {

namespace {

constexpr float Pi = 3.14159265f;
constexpr double SwingSeconds = 0.3;
constexpr uint32_t EntityQuadFlag = 1u << 5;
constexpr uint32_t FullSkyLight = 0xF0F0F0F0u;
constexpr float HandFovDegrees = 70.0f;
constexpr float HeldCubeSize = 0.5f;
constexpr float HeldItemSize = 0.65f;
constexpr uint32_t ItemGrid = world::ItemIconSize;

using Vec3 = std::array<float, 3>;

struct ItemDisplay {
    Vec3 rotation;
    Vec3 translation;
    float scale;
};

// thirdperson_righthand of block/block, item/generated and item/handheld,
// translations in pixels. Bedrock documents the same block values.
constexpr ItemDisplay BlockThirdPerson { { 75.0f, 45.0f, 0.0f }, { 0.0f, 2.5f, 0.0f }, 0.375f };
constexpr ItemDisplay GeneratedThirdPerson { { 0.0f, 0.0f, 0.0f }, { 0.0f, 3.0f, 1.0f }, 0.55f };
constexpr ItemDisplay HandheldThirdPerson { { 0.0f, -90.0f, 55.0f }, { 0.0f, 4.0f, 0.5f }, 0.85f };
constexpr Vec3 HandOffset { 1.0f, 2.0f, -10.0f };

// Where the paper doll sits in HUD units: hud_player_renderer is a 15 unit
// panel 15 units in from the top left, and the HUD leaves the top 50 units
// clear for it.
constexpr std::array<float, 2> PaperDollCenter { 22.5f, 24.0f };
constexpr float PaperDollHeight = 30.0f;
constexpr float PaperDollTurnDegrees = 30.0f;
// Far enough that a model pixel is several 1/256 block steps wide.
constexpr float PaperDollDistance = 8.0f;
constexpr float PlayerHeight = 1.8f;
// The doll stays up this long after the player stops moving that way.
constexpr double PaperDollLingerSeconds = 3.0;

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

float wrapDegrees(float degrees)
{
    float wrapped = std::fmod(degrees + 180.0f, 360.0f);
    return (wrapped < 0.0f ? wrapped + 360.0f : wrapped) - 180.0f;
}

std::array<float, 9> inverseBasis(const world::BoneMatrix& m)
{
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(det) < 1.0e-8f) return { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    float s = 1.0f / det;
    return { (e*i-f*h)*s, (c*h-b*i)*s, (b*f-c*e)*s,
        (f*g-d*i)*s, (a*i-c*g)*s, (c*d-a*f)*s,
        (d*h-e*g)*s, (b*g-a*h)*s, (a*e-b*d)*s };
}

world::BoneMatrix relativeFrame(const world::BoneMatrix& body, const world::BoneMatrix& item)
{
    auto inverse = inverseBasis(body);
    world::BoneMatrix frame {};
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            for (size_t k = 0; k < 3; ++k) {
                frame[row * 4 + column] += inverse[row * 3 + k] * (item[k * 4 + column] - (column == 3 ? body[k * 4 + 3] : 0.0f));
            }
        }
    }
    return frame;
}

Vec3 transformPoint(const world::BoneMatrix& m, const Vec3& p)
{
    return { m[0]*p[0]+m[1]*p[1]+m[2]*p[2]+m[3],
        m[4]*p[0]+m[5]*p[1]+m[6]*p[2]+m[7], m[8]*p[0]+m[9]*p[1]+m[10]*p[2]+m[11] };
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
 * The player model the local player wears, with the rig and texture layer of
 * their own skin when it has arrived, the default ones until then.
 */
const world::EntityModel* Client::localPlayerModel(const world::EntityRig*& rig, uint32_t& skinLayer) const
{
    const world::EntityModel* model = localSlim ? blockAssets->entityModel("minecraft:player#slim") : nullptr;
    if (!model) {
        model = blockAssets->entityModel("minecraft:player");
    }
    if (!model || model->rigs.empty()) {
        return nullptr;
    }
    rig = &model->rigs.front();
    skinLayer = model->layer;
    if (localSkinSlot != NoSkin) {
        if (auto skinRig = skinRigs.find(localSkinSlot); skinRig != skinRigs.end() && skinRig->second) {
            rig = skinRig->second.get();
        }
        skinLayer = blockAssets->skinLayerBase() + localSkinSlot;
    }
    return model;
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
    const world::EntityRig* chosenRig = nullptr;
    uint32_t skinLayer = 0;
    const world::EntityModel* model = localPlayerModel(chosenRig, skinLayer);
    if (!model) {
        return;
    }
    const world::EntityRig& rig = *chosenRig;
    double now = secondsNow();
    float attackTime = swingProgress();

    int32_t slot = std::clamp(hudState.selectedSlot, 0, 8);
    const HudItem& selected = hudState.inventory[static_cast<size_t>(slot)];
    std::string heldIdentity = selected.identifier + "#" + std::to_string(selected.aux) + "#" + selected.icon;
    float elapsed = handUpdatedAt > 0.0 ? static_cast<float>(std::clamp(now - handUpdatedAt, 0.0, 0.1)) : 0.0f;
    handUpdatedAt = now;
    bool changing = heldIdentity != lastHeldIdentity;
    handEquip = std::clamp(handEquip + (changing ? -8.0f : 8.0f) * elapsed, 0.0f, 1.0f);
    if (!changing || handEquip <= 0.1f) {
        handItem = selected;
        lastHeldIdentity = heldIdentity;
    }
    const HudItem& held = handItem;
    std::string heldName = held.empty() ? std::string() : held.identifier;

    float yaw = camera.minecraftYaw();
    float pitch = camera.minecraftPitch();
    double eye = playerView.eyeHeight();
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
    input.onGround = playerView.onGround;
    input.health = hudState.health;
    input.maxHealth = hudState.maxHealth;
    input.hurtTime = hudState.lastHurt > 0.0 ? static_cast<float>(std::clamp(10.0 - (now - hudState.lastHurt) * 20.0, 0.0, 10.0)) : 0.0f;
    input.mainHandItem = heldName;
    input.offHandItem = hudState.offhand.empty() ? std::string() : hudState.offhand.identifier;
    input.engineVariables = {
        { "is_first_person", 1.0 },
        { "attack_time", attackTime },
        { "player_arm_height", handEquip },
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
    auto posedToWorld = [&](const Vec3& model) {
        Vec3 posed { model[0] - body[3], model[1] - body[7], model[2] - body[11] };
        Vec3 local {
            inverse[0] * posed[0] + inverse[1] * posed[1] + inverse[2] * posed[2] - neck[0],
            inverse[3] * posed[0] + inverse[4] * posed[1] + inverse[5] * posed[2] - neck[1],
            inverse[6] * posed[0] + inverse[7] * posed[1] + inverse[8] * posed[2] - neck[2],
        };
        Vec3 offset = add(add(scaled(axes[0], -local[0] * handZoom), scaled(axes[1], local[1] * handZoom)), scaled(axes[2], -local[2]));
        return add(eyePoint, scaled(offset, unit));
    };
    auto modelToWorld = [&](const world::BoneMatrix& m, const Vec3& pixels) {
        float x = pixels[0] / 16.0f;
        float y = pixels[1] / 16.0f;
        float z = pixels[2] / 16.0f;
        return posedToWorld({
            m[0] * x + m[1] * y + m[2] * z + m[3],
            m[4] * x + m[5] * y + m[6] * z + m[7],
            m[8] * x + m[9] * y + m[10] * z + m[11],
        });
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
        out.push_back(packQuad(corners, quad.uvs, skinLayer,(quad.flags & world::QuadFaceMask) | EntityQuadFlag | (input.hurtTime > 0.0f ? 1u << 7 : 0u)));
    }
    if (heldName.empty() || itemBone < 0) {
        return;
    }
    if (appendAttachable(held, rig, matrices, true, handAttachable, posedToWorld, out)) {
        return;
    }

    const world::EntityBone& anchor = rig.bones[static_cast<size_t>(itemBone)];
    world::AnimationInput rest = input;
    rest.x = rest.y = rest.z = rest.now = rest.worldTime = 0.0;
    rest.yaw = rest.headYaw = rest.pitch = 0.0f;
    rest.onGround = true;
    rest.hurtTime = 0.0f;
    for (auto& [name, value] : rest.engineVariables) {
        if (name == "attack_time" || name == "bob_animation" || name == "player_x_rotation") value = 0.0;
        if (name == "player_arm_height") value = 1.0;
    }
    handRestAnimator.update(model->scripts.get(), &blockAssets->animationLibrary(), rig.bones, rest);
    const auto& restMatrices = handRestAnimator.matrices();
    if (restMatrices.size() != matrices.size()) return;
    auto currentFrame = relativeFrame(body, matrices[static_cast<size_t>(itemBone)]);
    auto restFrame = relativeFrame(restMatrices[static_cast<size_t>(bodyBone)], restMatrices[static_cast<size_t>(itemBone)]);
    auto restInverse = inverseBasis(restFrame);
    Vec3 displacement = add(transformPoint(currentFrame, anchor.pivot), scaled(transformPoint(restFrame, anchor.pivot), -1.0f));
    float modelScale = handAnimator.scale() / 16.0f;
    auto place = [&](const Vec3& local, bool cube) {
        Vec3 pose = cube ? Vec3 { 12.0f, -35.0f, 0.0f }
            : held.handEquipped ? Vec3 { 0.0f, -30.0f, 75.0f } : Vec3 { 0.0f, -30.0f, 0.0f };
        Vec3 p = rotate(local, pose);
        Vec3 bodyPoint { -p[0], p[1], -p[2] };
        Vec3 unposed {};
        for (size_t row = 0; row < 3; ++row) {
            for (size_t k = 0; k < 3; ++k) unposed[row] += restInverse[row * 3 + k] * bodyPoint[k];
        }
        Vec3 animated = add(transformPoint(currentFrame, unposed), { -currentFrame[3], -currentFrame[7], -currentFrame[11] });
        Vec3 turned { 0.56f - animated[0] - displacement[0] * modelScale,
            (held.handEquipped ? -0.36f : -0.48f) + animated[1] + displacement[1] * modelScale,
            -0.85f - animated[2] - displacement[2] * modelScale };
        Vec3 world = add(add(scaled(axes[0], turned[0] * handZoom), scaled(axes[1], turned[1] * handZoom)), scaled(axes[2], turned[2]));
        return add(eyePoint, scaled(world, 256.0f));
    };
    appendHeldItem(held, place, out);
}

bool Client::paperDollVisible()
{
    if (!blockAssets || !worldShown || !playerView.active || menu.paperDollHidden() || menu.inventoryPanel().active) {
        return false;
    }
    double now = secondsNow();
    if (playerView.sneaking || playerView.sprinting || playerView.swimming || playerView.flying) {
        paperDollShownAt = now;
    }
    return paperDollShownAt > 0.0 && now - paperDollShownAt < PaperDollLingerSeconds;
}

/**
 * Bedrock's paper doll: the local player model in the top left corner of the
 * HUD while they sneak, sprint, swim or fly and a few seconds after, running the same third person
 * animations as F5 (swings, sneaking, swimming) with its armor and held item,
 * turned a little toward the middle of the screen. The game's own paperdoll
 * state is the dressing room pose, not this one. The head
 * keeps that turn and the body swings under it, so strafing shows up on the
 * doll. It is laid out in HUD units and projected back into the world a few
 * blocks ahead of the eye, so sprinting's wider view doesn't move, shrink or
 * skew it, and goes out with the hand so terrain never covers it.
 */
void Client::appendPaperDoll(const ActorView& self, const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out)
{
    if (!paperDollVisible()) {
        return;
    }
    const world::EntityRig* chosenRig = nullptr;
    uint32_t skinLayer = 0;
    const world::EntityModel* model = localPlayerModel(chosenRig, skinLayer);
    if (!model) {
        return;
    }
    const world::EntityRig& rig = *chosenRig;
    double now = secondsNow();
    const HudItem& held = hudState.inventory[static_cast<size_t>(std::clamp(hudState.selectedSlot, 0, 8))];

    world::AnimationInput input;
    input.x = self.x;
    input.y = self.y;
    input.z = self.z;
    input.yaw = self.yaw;
    input.headYaw = self.headYaw;
    input.pitch = self.pitch;
    input.now = now;
    input.worldTime = currentWorldTime(timeState);
    input.flags = self.flags;
    input.identifier = self.identifier;
    input.onGround = playerView.onGround;
    input.health = hudState.health;
    input.maxHealth = hudState.maxHealth;
    input.hurtTime = self.lastHurt > 0.0 ? static_cast<float>(std::clamp(10.0 - (now - self.lastHurt) * 20.0, 0.0, 10.0)) : 0.0f;
    input.mainHandItem = held.empty() ? std::string() : held.identifier;
    input.offHandItem = hudState.offhand.empty() ? std::string() : hudState.offhand.identifier;
    input.engineVariables = {
        { "attack_time", swingProgress() },
        { "is_holding_right", held.empty() ? 0.0 : 1.0 },
        { "is_first_person", 0.0 },
        { "swim_amount", playerView.swimming ? 1.0 : 0.0 },
    };
    paperDollAnimator.update(model->scripts.get(), &blockAssets->animationLibrary(), rig.bones, input);
    const std::vector<world::BoneMatrix>& matrices = paperDollAnimator.matrices();
    if (matrices.size() != rig.bones.size()) {
        return;
    }

    float width = static_cast<float>(window->width());
    float height = static_cast<float>(std::max<uint32_t>(window->height(), 1));
    float aspect = width / height;
    float gui = guiScale();
    ui::Rect safe = menu.safeRect(width / gui, height / gui);
    float tangent = camera.halfVerticalTangent(aspect);
    std::array<Vec3, 3> axes = cameraAxes(camera.minecraftYaw(), camera.minecraftPitch());
    Vec3 eyePoint {
        static_cast<float>((camera.x() - origin[0]) * 256.0),
        static_cast<float>((camera.y() - origin[1]) * 256.0),
        static_cast<float>((camera.z() - origin[2]) * 256.0),
    };

    // A doll simply placed in the corner of a wide view gets stretched toward
    // the corner and seen from the side. Instead every point is laid out on
    // the HUD as a straight on view would draw it, then pushed out along the
    // ray through that HUD spot, nearer points a little closer to keep depth.
    float hudPerBlock = PaperDollHeight / PlayerHeight;
    float unit = paperDollAnimator.scale() / 16.0f;
    float turn = (PaperDollTurnDegrees - wrapDegrees(self.yaw - self.headYaw)) * Pi / 180.0f;
    float cosine = std::cos(turn);
    float sine = std::sin(turn);
    auto toWorld = [&](const Vec3& posed) {
        float x = -posed[0] * unit;
        float y = posed[1] * unit - PlayerHeight * 0.5f;
        float z = -posed[2] * unit;
        float across = x * cosine + z * sine;
        float toward = -x * sine + z * cosine;
        float hudX = safe.x + PaperDollCenter[0] + across * hudPerBlock;
        float hudY = safe.y + PaperDollCenter[1] - y * hudPerBlock;
        float ndcX = hudX * gui / width * 2.0f - 1.0f;
        float ndcY = 1.0f - hudY * gui / height * 2.0f;
        Vec3 ray = add(add(scaled(axes[2], -1.0f), scaled(axes[0], ndcX * tangent * aspect)), scaled(axes[1], ndcY * tangent));
        return add(eyePoint, scaled(ray, std::max(PaperDollDistance - toward, 1.0f) * 256.0f));
    };

    uint32_t hurt = self.lastHurt > 0.0 && now - self.lastHurt < 0.5 ? 1u << 7 : 0u;
    for (size_t index = 0; index < rig.quads.size(); ++index) {
        size_t bone = index < rig.quadBones.size() ? rig.quadBones[index] : rig.bones.size();
        const world::ModelQuad& quad = rig.quads[index];
        std::array<Vec3, 4> corners;
        for (size_t corner = 0; corner < 4; ++corner) {
            Vec3 p { quad.positions[corner][0] / 16.0f, quad.positions[corner][1] / 16.0f, quad.positions[corner][2] / 16.0f };
            if (bone < matrices.size()) {
                const world::BoneMatrix& m = matrices[bone];
                p = {
                    m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3],
                    m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
                    m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11],
                };
            }
            corners[corner] = toWorld(p);
        }
        out.push_back(packQuad(corners, quad.uvs, skinLayer, (quad.flags & world::QuadFaceMask) | EntityQuadFlag | hurt));
    }
    appendArmor(self.armor, rig, matrices, toWorld, hurt, out);
    appendThirdPersonItem(rig, matrices, toWorld, out);
}

/**
 * The selected item in the right hand of the local player's model in the
 * third person views. The game places it the same way the Java renderer does:
 * turned into the hand frame of the right arm, nudged to the fist, then run
 * through the item's thirdperson_righthand display transform (block, flat item
 * or tool). Those run in the flipped Java model space, which is the rig space
 * turned half a circle around z.
 */
void Client::appendThirdPersonItem(const world::EntityRig& rig, const std::vector<world::BoneMatrix>& matrices, const std::function<Vec3(const Vec3&)>& toWorld, std::vector<world::ModelQuadGpu>& out)
{
    int32_t armBone = -1;
    for (size_t bone = 0; bone < rig.bones.size() && bone < matrices.size(); ++bone) {
        if (lowercase(rig.bones[bone].name) == "rightarm") {
            armBone = static_cast<int32_t>(bone);
        }
    }
    if (armBone < 0) {
        return;
    }
    const world::BoneMatrix& m = matrices[static_cast<size_t>(armBone)];
    const Vec3& shoulder = rig.bones[static_cast<size_t>(armBone)].pivot;
    const HudItem& held = hudState.inventory[static_cast<size_t>(std::clamp(hudState.selectedSlot, 0, 8))];
    if (appendAttachable(held, rig, matrices, false, bodyAttachable, toWorld, out)) {
        return;
    }
    auto place = [&](const Vec3& local, bool cube) {
        const ItemDisplay& display = cube ? BlockThirdPerson : held.handEquipped ? HandheldThirdPerson : GeneratedThirdPerson;
        Vec3 p = scaled(local, display.scale / (cube ? HeldCubeSize : HeldItemSize));
        p = rotate(rotate(rotate(p, { 0.0f, 0.0f, display.rotation[2] }), { 0.0f, display.rotation[1], 0.0f }), { display.rotation[0], 0.0f, 0.0f });
        p = add(p, scaled(add(display.translation, HandOffset), 1.0f / 16.0f));
        p = rotate(rotate(p, { 0.0f, 180.0f, 0.0f }), { -90.0f, 0.0f, 0.0f });
        Vec3 pixels { shoulder[0] - p[0] * 16.0f, shoulder[1] - p[1] * 16.0f, shoulder[2] + p[2] * 16.0f };
        return toWorld({
            m[0] * pixels[0] + m[1] * pixels[1] + m[2] * pixels[2] + m[3],
            m[4] * pixels[0] + m[5] * pixels[1] + m[6] * pixels[2] + m[7],
            m[8] * pixels[0] + m[9] * pixels[1] + m[10] * pixels[2] + m[11],
        });
    };
    appendHeldItem(held, place, out);
}

/**
 * Starts a new swing unless the current one is less than halfway through,
 * which is why spam clicking never spins the arm faster than the game does.
 */
void Client::startSwing(double now)
{
    if (swingStart < 0.0 || now - swingStart >= SwingSeconds * 0.5) {
        swingStart = now;
    }
}

float Client::swingProgress()
{
    double now = secondsNow();
    if (menu.capturesMouse() && window->input().mousePressed) {
        startSwing(now);
    }
    double swing = swingStart >= 0.0 ? (now - swingStart) / SwingSeconds : 1.0;
    return swing < 1.0 ? static_cast<float>(swing) : 0.0f;
}

/**
 * The selected hotbar item as the hand holds it: a block as a cube and any
 * other item as its texture extruded one pixel deep. place maps a point of
 * the item, in blocks around its center, to 1/256 block around the draw
 * origin; it is told whether the item is a cube so it can pose it.
 */
void Client::appendHeldItem(const HudItem& held, const std::function<std::array<float, 3>(const std::array<float, 3>&, bool)>& place, std::vector<world::ModelQuadGpu>& out)
{
    if (held.empty() || !blockAssets) {
        return;
    }
    std::string meshKey = held.identifier + "#" + std::to_string(held.aux) + "#" + held.icon;
    if (meshKey != heldItemKey) {
        heldItemMesh = buildItemMesh(held, heldItemLayer(), heldItemBlock);
        heldItemKey = meshKey;
    }
    for (const HeldItemFace& face : heldItemMesh) {
        std::array<Vec3, 4> corners;
        for (size_t i = 0; i < 4; ++i) corners[i] = place(face.corners[i], heldItemBlock);
        out.push_back(packQuad(corners, face.uvs, face.material, face.shade));
    }
}

/**
 * The shape of an item as it is drawn in the world, in blocks around its
 * center: a block as its model or a cube half a block wide, any other item as
 * its icon extruded one pixel deep, the icon uploaded to the given entity
 * texture layer. block tells which of the two it is.
 */
std::vector<Client::HeldItemFace> Client::buildItemMesh(const HudItem& held, uint32_t layer, bool& block)
{
    std::vector<HeldItemFace> mesh;
    const std::string& heldName = held.identifier;
    {
        const world::BlockVisual* cube = blockAssets->itemCube(heldName);
        std::vector<world::ModelQuad> shape = blockAssets->itemGeometry(heldName);
        block = cube != nullptr || !shape.empty();
        auto emit = [&](const std::array<Vec3, 4>& local, const std::array<std::array<uint16_t, 2>, 4>& uvs, uint32_t material, uint32_t shadeWord) {
            mesh.push_back({ local, uvs, material, shadeWord });
        };
        const std::array<std::array<uint16_t, 2>, 4> fullUv { { { 0, 0 }, { 4096, 0 }, { 4096, 4096 }, { 0, 4096 } } };

        if (!shape.empty()) {
            const auto& materials = blockAssets->materials();
            for (const world::ModelQuad& quad : shape) {
                if (quad.material >= materials.size()) continue;
                std::array<Vec3, 4> corners;
                for (size_t i = 0; i < 4; ++i) {
                    for (size_t axis = 0; axis < 3; ++axis) corners[i][axis] = (float(quad.positions[i][axis]) / 256.0f - 0.5f) * HeldCubeSize;
                }
                const auto& material = materials[quad.material];
                uint32_t tint = material.tintKind() != world::TintKind::None ? world::ItemTint : 0u;
                emit(corners, quad.uvs, material.gpuWord(), (quad.flags & world::QuadFaceMask) | (tint << 8));
            }
            return mesh;
        }

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
                if (material >= materials.size()) {
                    continue;
                }
                uint32_t tint = materials[material].tintKind() != world::TintKind::None ? world::ItemTint : 0u;
                emit(side.corners, fullUv, materials[material].gpuWord(), (uint32_t(side.face) + 1) | (tint << 8));
            }
            return mesh;
        }

        std::vector<uint8_t> icon = blockAssets->itemIcon(held.identifier, held.aux, held.icon);
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
        if (icon.size() != size_t(ItemGrid) * ItemGrid * 4) {
            return mesh;
        }
        float h = HeldItemSize * 0.5f;
        float pixel = HeldItemSize / static_cast<float>(ItemGrid);
        float depth = HeldItemSize / 32.0f;
        uint32_t shade = EntityQuadFlag | (1u << 8);
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
                uint16_t u0 = static_cast<uint16_t>((x + 0.25f) * 4096 / ItemGrid);
                uint16_t u1 = static_cast<uint16_t>((x + 0.75f) * 4096 / ItemGrid);
                uint16_t v0 = static_cast<uint16_t>((y + 0.25f) * 4096 / ItemGrid);
                uint16_t v1 = static_cast<uint16_t>((y + 0.75f) * 4096 / ItemGrid);
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
    return mesh;
}

}
