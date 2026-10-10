#include "client/Client.h"


#include "render/Renderer.h"
#include "world/BlockAssets.h"
#include "world/ItemInfo.h"
#include "world/ItemGlint.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>

namespace kestrel {

namespace {

constexpr float Pi = 3.14159265f;
constexpr double SwingSeconds = 0.3;
constexpr uint32_t EntityQuadFlag = 1u << 5;
constexpr uint64_t InvisibleFlag = 1ull << 5;
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

using Mat4 = std::array<float, 16>;

Mat4 identity()
{
    return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
}

/**
 * Row-major product a * b, so b applies to a point first.
 */
Mat4 operator*(const Mat4& a, const Mat4& b)
{
    Mat4 out {};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            for (size_t k = 0; k < 4; ++k) {
                out[row * 4 + column] += a[row * 4 + k] * b[k * 4 + column];
            }
        }
    }
    return out;
}

Mat4 translation(float x, float y, float z)
{
    Mat4 m = identity();
    m[3] = x;
    m[7] = y;
    m[11] = z;
    return m;
}

Mat4 uniformScale(float s)
{
    Mat4 m = identity();
    m[0] = s;
    m[5] = s;
    m[10] = s;
    return m;
}

Mat4 rotationX(float degrees)
{
    float c = std::cos(degrees * Pi / 180.0f);
    float s = std::sin(degrees * Pi / 180.0f);
    return { 1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0, 0, 0, 0, 1 };
}

Mat4 rotationY(float degrees)
{
    float c = std::cos(degrees * Pi / 180.0f);
    float s = std::sin(degrees * Pi / 180.0f);
    return { c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, 0, 0, 0, 1 };
}

Mat4 rotationZ(float degrees)
{
    float c = std::cos(degrees * Pi / 180.0f);
    float s = std::sin(degrees * Pi / 180.0f);
    return { c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
}

Vec3 transformed(const Mat4& m, const Vec3& p)
{
    return { m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3],
        m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
        m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11] };
}

/**
 * The default transforms the game applies to a flat sprite in hand: the 1.5
 * scale and tilt that seat the held sprite mesh in the grip.
 */
Mat4 itemDefault()
{
    return uniformScale(1.5f) * rotationY(50.0f) * rotationZ(335.0f) * translation(0.075f, -0.245f, -0.1f);
}

/**
 * Items whose icon the game turns half a revolution in first person.
 */
bool mirroredArt(const std::string& identifier)
{
    return identifier == "minecraft:fishing_rod" || identifier == "minecraft:carrot_on_a_stick" || identifier == "minecraft:warped_fungus_on_a_stick";
}

/**
 * Whether using an item over time means eating or drinking it, as opposed to
 * drawing, charging, aiming or thrusting it.
 */
bool consumed(const std::string& identifier)
{
    if (identifier.rfind("minecraft:", 0) != 0) {
        return false;
    }
    std::string name = identifier.substr(10);
    return name != "bow" && name != "trident" && name != "spyglass" && name != "crossbow" && name != "camera"
        && !name.ends_with("_spear");
}

/**
 * Camera-space placement of the first person held item, in blocks with x to
 * the right, y up and z backwards: the swing offset or the eat and drink
 * raise, the anchor, the equip dip, the swing turns and the 0.4 hand scale,
 * then the default item transforms for a sprite. consumeTicks and
 * consumeDuration describe an eat or drink under way, duration 0 for none.
 */
Mat4 firstPersonItem(bool block, bool mirrored, float swing, float equip, float consumeTicks, float consumeDuration, float idleBob)
{
    float sine = std::sin(swing * Pi);
    float rootSine = std::sin(std::sqrt(swing) * Pi);
    Mat4 lead;
    if (consumeDuration > 0.0f) {
        float remaining = consumeDuration - consumeTicks + 1.0f;
        float progress = 1.0f - remaining / consumeDuration;
        float bob = progress > 0.2f ? std::abs(std::cos(remaining * 0.25f * Pi)) * 0.1f : 0.0f;
        float raise = 1.0f - std::pow(std::clamp(1.0f - progress, 0.0f, 1.0f), 27.0f);
        lead = translation(0.0f, bob, 0.0f) * translation(raise * 0.55f, raise * -0.5f, 0.0f) * rotationY(raise * 90.0f)
            * rotationX(raise * 10.0f) * rotationZ(raise * 30.0f);
    } else {
        lead = translation(rootSine * -0.4f, std::sin(std::sqrt(swing) * Pi * 2.0f) * 0.2f, sine * -0.2f);
    }
    Mat4 held = lead * translation(0.56f, -0.52f, -0.72f) * translation(0.0f, (1.0f - equip) * -0.6f, 0.0f) * rotationY(45.0f)
        * rotationY(std::sin(swing * swing * Pi) * -20.0f) * rotationZ(rootSine * -20.0f) * rotationX(rootSine * -80.0f)
        * uniformScale(0.4f);
    held = held * translation(0.0f, idleBob, 0.0f) * rotationX(idleBob * 27.000002f);
    if (block) {
        return held;
    }
    return held * (mirrored ? rotationY(180.0f) : identity()) * itemDefault();
}

/**
 * A model quad's corner UVs over the whole texture.
 */
std::array<std::array<float, 2>, 4> quadUvs(const world::ModelQuad& quad)
{
    std::array<std::array<float, 2>, 4> uvs {};
    for (size_t corner = 0; corner < 4; ++corner) {
        uvs[corner] = { quad.uvs[corner][0] / 4096.0f, quad.uvs[corner][1] / 4096.0f };
    }
    return uvs;
}

/**
 * Packs a quad whose corners are in 1/256 block around the draw origin.
 */
world::ModelQuadGpu packQuad(const std::array<Vec3, 4>& corners, const std::array<std::array<uint16_t, 2>, 4>& uvs, uint32_t material, uint32_t shadeWord)
{
    world::ModelQuadGpu gpu;
    packEntityPositions(corners, gpu.words);
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
    return blockAssets ? blockAssets->skinLayerBase() + world::SkinPoolLayers : 0;
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
        if (const SkinView* view = skinViewOf(localSkinSlot); view && view->base.present) {
            skinLayer = blockAssets->skinLayerBase() + view->base.layer;
        }
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
    if (!blockAssets || !playerView.active || !worldShown || perspective != PerspectiveFirst || cameraDetached) {
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
    bool bobbing = menu.option("view_bobbing", 1) != 0;
    double movementTickTime = seenSessionSnapshot ? seenSessionSnapshot->player.tickTime : playerView.tickTime;
    float partialTick = static_cast<float>(std::clamp((now - movementTickTime) / 0.05, 0.0, 1.0));
    Mat4 viewMotion = identity();
    if (bobbing) {
        firstPersonMotion.look(now, camera.minecraftPitch(), camera.minecraftYaw());
        float amount = firstPersonMotion.oldBob + (firstPersonMotion.bob - firstPersonMotion.oldBob) * partialTick;
        float phase = -(firstPersonMotion.distance + (firstPersonMotion.distance - firstPersonMotion.oldDistance) * partialTick) * Pi;
        float sine = std::sin(phase);
        float tilt = firstPersonMotion.oldTilt + (firstPersonMotion.tilt - firstPersonMotion.oldTilt) * partialTick;
        viewMotion = translation(sine * amount * 0.65f, -std::abs(std::cos(phase) * amount), 0.0f)
            * rotationZ(sine * amount * 3.0f) * rotationX(std::abs(std::cos(phase - 0.2f) * amount) * 5.0f)
            * rotationX(tilt) * rotationX(firstPersonMotion.rotation[0]) * rotationY(firstPersonMotion.rotation[1]);
    }
    float idleBob = bobbing ? static_cast<float>(std::sin(now * 2.0)) * 0.011f : 0.0f;
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
    input.walkDistance = firstPersonMotion.distance + (firstPersonMotion.distance - firstPersonMotion.oldDistance) * partialTick;
    if (seenSessionSnapshot && seenSessionSnapshot->playerTicks && !seenSessionSnapshot->playerTicks->empty()) {
        const auto& movement = seenSessionSnapshot->playerTicks->back().velocity;
        input.tickPositionDelta = std::array<double, 3> { movement[0], movement[1], movement[2] };
    }
    input.worldTime = currentWorldTime(timeState);
    input.identifier = "minecraft:player";
    if (seenSessionSnapshot) input.flags = seenSessionSnapshot->localActorFlags;
    input.onGround = playerView.onGround;
    input.health = hudState.health;
    input.maxHealth = hudState.maxHealth;
    input.hurtTime = hudState.lastHurt > 0.0 ? static_cast<float>(std::clamp(10.0 - (now - hudState.lastHurt) * 20.0, 0.0, 10.0)) : 0.0f;
    input.mainHandItem = heldName;
    input.offHandItem = hudState.offhand.empty() ? std::string() : hudState.offhand.identifier;
    input.itemUseTicks = localItemUseTicks();
    input.engineVariables = {
        { "is_first_person", 1.0 },
        { "swim_amount", localSwimAmount },
        { "left_arm_swim_amount", localSwimAmount },
        { "right_arm_swim_amount", localSwimAmount },
        { "attack_time", attackTime },
        { "player_arm_height", handEquip },
        { "is_holding_right", heldName.empty() ? 0.0 : 1.0 },
        { "is_holding_left", 0.0 },
        { "bob_animation", bobbing ? 1.0 : 0.0 },
        { "is_using_vr", 0.0 },
        { "is_paperdoll", 0.0 },
        { "map_face_icon", 0.0 },
        { "short_arm_offset_right", 0.0 },
        { "short_arm_offset_left", 0.0 },
        { "player_x_rotation", pitch },
        { "is_horizontal_splitscreen", 0.0 },
        { "is_vertical_splitscreen", 0.0 },
    };
    input.swimAmount = localSwimAmount;
    if (seenSessionSnapshot) input.inWater = session.cameraEnvironment(*seenSessionSnapshot,
        { eyePosition[0], eyePosition[1] - playerView.eyeHeight() + 0.1, eyePosition[2] }, false).first == 1;
    input.flags[0] |= playerView.sprinting && input.inWater.value_or(false) ? uint64_t(1) << 57 : 0;
    handAnimator.update(model->scripts.get(), &blockAssets->animationLibrary(), rig.bones, input);
    const std::vector<world::BoneMatrix>& matrices = handAnimator.matrices();
    if (matrices.size() != rig.bones.size()) {
        return;
    }

    int32_t itemBone = -1;
    int32_t bodyBone = -1;
    std::vector<uint8_t> shown(rig.bones.size(), 0);
    HeldItemMesh* mapMesh = held.identifier == "minecraft:filled_map" ? heldMesh(held) : nullptr;
    bool holdingMap = mapMesh && mapMesh->map;
    for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
        std::string name = lowercase(rig.bones[bone].name);
        if (name == "rightitem") {
            itemBone = static_cast<int32_t>(bone);
        }
        if (name == "body") {
            bodyBone = static_cast<int32_t>(bone);
        }
        if ((heldName.empty() || holdingMap) && (name == "rightarm" || name == "rightsleeve")) {
            shown[bone] = 1;
        }
        if (holdingMap && hudState.offhand.empty() && (name == "leftarm" || name == "leftsleeve")) {
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
        Vec3 view = transformed(viewMotion, { -local[0] * unit / 256.0f, local[1] * unit / 256.0f, -local[2] * unit / 256.0f });
        Vec3 offset = add(add(scaled(axes[0], view[0] * handZoom), scaled(axes[1], view[1] * handZoom)), scaled(axes[2], view[2]));
        return add(eyePoint, scaled(offset, 256.0f));
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
        if (bone >= rig.bones.size() || !shown[bone] || (input.flags[0] & InvisibleFlag) != 0) {
            continue;
        }
        const world::ModelQuad& quad = rig.quads[index];
        std::array<Vec3, 4> corners;
        Vec3 center {};
        for (size_t corner = 0; corner < 4; ++corner) {
            Vec3 pixels { float(quad.positions[corner][0]), float(quad.positions[corner][1]), float(quad.positions[corner][2]) };
            center = add(center, scaled(pixels, 0.25f));
            corners[corner] = modelToWorld(matrices[bone], pixels);
        }
        uint32_t posedFace = world::posedShadeFace(quad.flags & world::QuadFaceMask, center, 16.0f, [&](const Vec3& pixels) {
            return modelToWorld(matrices[bone], pixels);
        });
        appendEntityQuad(corners, quadUvs(quad), skinLayer, posedFace | EntityQuadFlag | (input.hurtTime > 0.0f ? 1u << 7 : 0u), out);
    }
    std::array<std::string, 4> armor;
    for (size_t piece = 0; piece < armor.size(); ++piece) {
        armor[piece] = hudState.armor[piece].empty() ? std::string() : hudState.armor[piece].identifier;
    }
    appendArmor(armor, rig, matrices, posedToWorld, input.hurtTime > 0.0f ? 1u << 7 : 0u, out, &shown, &hudState.armor);
    if (holdingMap && appendFirstPersonMap(held, attackTime, axes, eyePoint, handZoom, viewMotion, out)) {
        return;
    }
    if (heldName.empty() || itemBone < 0) {
        return;
    }
    if (appendAttachable(held, input.itemUseTicks, rig, matrices, true, handAttachable, posedToWorld, out)) {
        return;
    }

    double ticksUsed = 0.0;
    int32_t consumeDuration = 0;
    if (consumed(held.identifier)) {
        consumeDuration = held.useTicks > 0 ? held.useTicks : blockAssets->itemUseTicks(held.identifier);
        if (consumeDuration <= 0) {
            consumeDuration = world::itemDrinkTicks(held.identifier);
        }
    }
    if (consumeDuration > 0 && session.useIsHeld() && menu.capturesMouse()) {
        if (consumeIdentity != heldIdentity || consumeStarted <= 0.0) {
            consumeIdentity = heldIdentity;
            consumeStarted = now;
        }
        ticksUsed = (now - consumeStarted) * 20.0;
        if (ticksUsed >= consumeDuration) {
            consumeStarted = now;
            ticksUsed = 0.0;
        }
    } else {
        consumeStarted = 0.0;
        consumeIdentity.clear();
    }
    bool consuming = consumeStarted > 0.0;
    float consumeTicks = static_cast<float>(ticksUsed);
    float consumeLength = consuming ? static_cast<float>(consumeDuration) : 0.0f;
    // a mod can shift the item in view space and grow it around its own middle
    mod::Vec3 heldOffset = visuals.heldOffset.value_or(mod::Vec3 {});
    Mat4 moved = translation(static_cast<float>(heldOffset.x), static_cast<float>(heldOffset.y), static_cast<float>(heldOffset.z));
    Mat4 grown = uniformScale(visuals.heldScale.value_or(1.0f));
    Mat4 blockPlacement = viewMotion * moved * firstPersonItem(true, false, attackTime, handEquip, consumeTicks, consumeLength, idleBob) * grown;
    Mat4 spritePlacement = viewMotion * moved * firstPersonItem(false, mirroredArt(held.identifier), attackTime, handEquip, consumeTicks, consumeLength, idleBob) * grown;
    auto place = [&](const Vec3& local, bool cube) {
        Vec3 shaped = cube
            ? scaled(local, 1.0f / HeldCubeSize)
            : Vec3 { -(local[0] / HeldItemSize + 0.5f), local[1] / HeldItemSize + 0.5f, local[2] / HeldItemSize - 1.0f / 32.0f };
        Vec3 view = transformed(cube ? blockPlacement : spritePlacement, shaped);
        Vec3 world = add(add(scaled(axes[0], view[0] * handZoom), scaled(axes[1], view[1] * handZoom)), scaled(axes[2], view[2]));
        return add(eyePoint, scaled(world, 256.0f));
    };
    appendHeldItem(held, place, out, true);
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
    input.itemUseTicks = localItemUseTicks();
    input.engineVariables = {
        { "attack_time", swingProgress() },
        { "is_holding_right", held.empty() ? 0.0 : 1.0 },
        { "is_first_person", 0.0 },
        { "swim_amount", localSwimAmount },
        { "left_arm_swim_amount", localSwimAmount },
        { "right_arm_swim_amount", localSwimAmount },
    };
    input.swimAmount = localSwimAmount;
    if (seenSessionSnapshot) input.inWater = session.cameraEnvironment(*seenSessionSnapshot, { self.x, self.y + 0.1, self.z }, false).first == 1;
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
    bool invisible = (self.flags[0] & InvisibleFlag) != 0;
    for (size_t index = 0; !invisible && index < rig.quads.size(); ++index) {
        size_t bone = index < rig.quadBones.size() ? rig.quadBones[index] : rig.bones.size();
        const world::ModelQuad& quad = rig.quads[index];
        auto place = [&](const Vec3& point) {
            Vec3 p = point;
            if (bone < matrices.size()) {
                const world::BoneMatrix& m = matrices[bone];
                p = {
                    m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3],
                    m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
                    m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11],
                };
            }
            return toWorld(p);
        };
        std::array<Vec3, 4> corners;
        Vec3 center {};
        for (size_t corner = 0; corner < 4; ++corner) {
            Vec3 p { quad.positions[corner][0] / 16.0f, quad.positions[corner][1] / 16.0f, quad.positions[corner][2] / 16.0f };
            center = add(center, scaled(p, 0.25f));
            corners[corner] = place(p);
        }
        uint32_t posedFace = world::posedShadeFace(quad.flags & world::QuadFaceMask, center, 1.0f / 16.0f, place);
        appendEntityQuad(corners, quadUvs(quad), skinLayer, posedFace | EntityQuadFlag | hurt, out);
    }
    appendArmor(self.armor, rig, matrices, toWorld, hurt, out, nullptr, &hudState.armor);
    appendThirdPersonItem(hudState.inventory[static_cast<size_t>(std::clamp(hudState.selectedSlot, 0, 8))], input.itemUseTicks, bodyAttachable, rig, matrices, toWorld, out);
    appendThirdPersonItem(hudState.offhand, input.itemUseTicks, bodyOffhandAttachable, rig, matrices, toWorld, out, true);
}

/**
 * The selected item in the right hand of the local player's model in the
 * third person views. The game places it the same way the Java renderer does:
 * turned into the hand frame of the right arm, nudged to the fist, then run
 * through the item's thirdperson_righthand display transform (block, flat item
 * or tool). Those run in the flipped Java model space, which is the rig space
 * turned half a circle around z.
 */
void Client::appendThirdPersonItem(const HudItem& held, double itemUseTicks, HeldAttachable& attachable, const world::EntityRig& rig, const std::vector<world::BoneMatrix>& matrices, const std::function<Vec3(const Vec3&)>& toWorld, std::vector<world::ModelQuadGpu>& out, bool leftHand)
{
    int32_t armBone = -1;
    for (size_t bone = 0; bone < rig.bones.size() && bone < matrices.size(); ++bone) {
        if (lowercase(rig.bones[bone].name) == (leftHand ? "leftarm" : "rightarm")) {
            armBone = static_cast<int32_t>(bone);
        }
    }
    if (armBone < 0) {
        return;
    }
    const world::BoneMatrix& m = matrices[static_cast<size_t>(armBone)];
    const Vec3& shoulder = rig.bones[static_cast<size_t>(armBone)].pivot;
    if (appendAttachable(held, itemUseTicks, rig, matrices, false, attachable, toWorld, out, leftHand)) {
        return;
    }
    auto place = [&](const Vec3& local, bool cube) {
        const ItemDisplay& display = cube ? BlockThirdPerson : held.handEquipped ? HandheldThirdPerson : GeneratedThirdPerson;
        Vec3 p = scaled(local, display.scale / (cube ? HeldCubeSize : HeldItemSize));
        p = rotate(rotate(rotate(p, { 0.0f, 0.0f, display.rotation[2] }), { 0.0f, display.rotation[1], 0.0f }), { display.rotation[0], 0.0f, 0.0f });
        p = add(p, scaled(add(display.translation, HandOffset), 1.0f / 16.0f));
        p = rotate(rotate(p, { 0.0f, 180.0f, 0.0f }), { -90.0f, 0.0f, 0.0f });
        if (leftHand) p[0] = -p[0];
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
    if (swingStart < 0.0 || now - swingStart >= SwingSeconds * visuals.swingDuration.value_or(1.0f) * 0.5) {
        swingStart = now;
    }
}

/**
 * How far a swing that started at start has got by now, zero once it is over.
 */
float Client::swingProgressSince(double start, double now) const
{
    double swing = (now - start) / (SwingSeconds * visuals.swingDuration.value_or(1.0f));
    return swing >= 0.0 && swing < 1.0 ? static_cast<float>(swing) : 0.0f;
}

float Client::swingProgress()
{
    double now = secondsNow();
    if (menu.capturesMouse() && !mods->wantsCursor() && window->input().mousePressed) {
        startSwing(now);
    }
    return swingStart >= 0.0 ? swingProgressSince(swingStart, now) : 0.0f;
}

/**
 * The mesh an item is held with, built once per look and kept in one of the
 * held texture layers; when they run out the least recently drawn look gives
 * its layer up. A filled map whose content arrived is a flat map showing it,
 * rebuilt whenever the map changes.
 */
Client::HeldItemMesh* Client::heldMesh(const HudItem& held)
{
    if (held.empty() || !blockAssets) {
        return nullptr;
    }
    std::string mapKey = mapMeshKey(held);
    std::string meshKey = held.identifier + "#" + std::to_string(held.aux) + "#" + held.icon + mapKey;
    if (held.customColor) meshKey += "#color" + std::to_string(*held.customColor);
    auto found = heldMeshes.find(meshKey);
    if (found == heldMeshes.end()) {
        uint32_t slot = static_cast<uint32_t>(heldMeshes.size());
        if (slot >= HeldItemTextureSlots) {
            auto oldest = heldMeshes.end();
            for (auto it = heldMeshes.begin(); it != heldMeshes.end(); ++it) {
                if (it->second.used == heldItemFrame) continue;
                if (oldest == heldMeshes.end() || it->second.used < oldest->second.used) oldest = it;
            }
            if (oldest == heldMeshes.end()) return nullptr;
            slot = oldest->second.slot;
            heldMeshes.erase(oldest);
        }
        HeldItemMesh cached;
        cached.slot = slot;
        if (!mapKey.empty() && buildMapMesh(held, heldItemLayer() + slot, cached.faces)) {
            cached.map = true;
        } else {
            cached.faces = buildItemMesh(held, heldItemLayer() + slot, cached.block);
        }
        found = heldMeshes.emplace(std::move(meshKey), std::move(cached)).first;
    }
    found->second.used = heldItemFrame;
    return &found->second;
}

/**
 * The selected hotbar item as the hand holds it: a block as a cube and any
 * other item as its texture extruded one pixel deep. place maps a point of
 * the item, in blocks around its center, to 1/256 block around the draw
 * origin; it is told whether the item is a cube so it can pose it.
 */
void Client::appendHeldItem(const HudItem& held, const std::function<std::array<float, 3>(const std::array<float, 3>&, bool)>& place, std::vector<world::ModelQuadGpu>& out, bool mirroredSprite)
{
    HeldItemMesh* cached = heldMesh(held);
    if (!cached) {
        return;
    }
    HeldItemMesh& mesh = *cached;
    for (const HeldItemFace& face : mesh.faces) {
        std::array<Vec3, 4> corners;
        for (size_t i = 0; i < 4; ++i) {
            size_t corner = mirroredSprite && !mesh.block ? 3 - i : i;
            corners[i] = place(face.corners[corner], mesh.block);
        }
        std::array<std::array<uint16_t, 2>, 4> uvs = face.uvs;
        if (mirroredSprite && !mesh.block) {
            std::reverse(uvs.begin(), uvs.end());
        }
        Vec3 center {};
        for (const Vec3& corner : face.corners) {
            center = add(center, scaled(corner, 0.25f));
        }
        uint32_t posedFace = world::posedShadeFace(face.shade & world::QuadFaceMask, center, 1.0f / 16.0f, [&](const Vec3& point) {
            return place(point, mesh.block);
        });
        out.push_back(packQuad(corners, uvs, face.material, (face.shade & ~uint32_t(world::QuadFaceMask)) | posedFace));
        if (held.enchanted && !mesh.block) {
            world::applyItemGlint(out.back(), blockAssets->armorGlintLayer(), secondsNow(), visuals.glintStrength.value_or(menu.glintStrength()), visuals.glintSpeed.value_or(menu.glintSpeed()));
        }
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
            static constexpr uint32_t SideFaceIds[6] = { 3, 4, 1, 2, 5, 6 };
            for (const Side& side : sides) {
                uint32_t material = cube->faces[static_cast<size_t>(side.face)];
                if (material >= materials.size()) {
                    continue;
                }
                uint32_t tint = materials[material].tintKind() != world::TintKind::None ? world::ItemTint : 0u;
                emit(side.corners, fullUv, materials[material].gpuWord(), SideFaceIds[size_t(side.face)] | (tint << 8));
            }
            return mesh;
        }

        std::vector<uint8_t> icon = blockAssets->itemIcon(held.identifier, held.aux, held.icon, held.customColor);
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

/**
 * A filled map as it is held or framed, in blocks around its center like a
 * flat item: the map's pixels in front over the map background, which is a
 * little larger and also covers the back. False until the server has sent
 * the map, so the item shows its icon meanwhile.
 */
bool Client::buildMapMesh(const HudItem& item, uint32_t layer, std::vector<HeldItemFace>& faces)
{
    MapView map;
    if (!renderer || !session.copyMap(item.mapId, map)) {
        return false;
    }
    std::vector<uint8_t> pixels = composeMap(map);
    renderer->updateEntityTexture(layer, pixels.data());
    float h = HeldItemSize * 0.5f;
    float edge = h * 142.0f / 128.0f;
    float depth = HeldItemSize / 64.0f;
    uint32_t shade = EntityQuadFlag | (1u << 8);
    const std::array<std::array<uint16_t, 2>, 4> fullUv { { { 0, 0 }, { 4096, 0 }, { 4096, 4096 }, { 0, 4096 } } };
    faces.clear();
    faces.push_back({ { { { -h, h, depth }, { h, h, depth }, { h, -h, depth }, { -h, -h, depth } } }, fullUv, layer, shade | 6 });
    faces.push_back({ { { { -edge, edge, 0.0f }, { edge, edge, 0.0f }, { edge, -edge, 0.0f }, { -edge, -edge, 0.0f } } }, fullUv, mapBackgroundLayer(), shade | 6 });
    faces.push_back({ { { { -edge, edge, -depth }, { edge, edge, -depth }, { edge, -edge, -depth }, { -edge, -edge, -depth } } }, fullUv, mapBackgroundLayer(), shade | 5 });
    return true;
}

/**
 * The items hanging in item frames: laid flat against the frame's plate,
 * turned by the frame's rotation, half a block wide, a block as a small cube
 * standing out of it, and a map filling the whole frame.
 */
void Client::appendFrameItems(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out)
{
    static constexpr std::array<Vec3, 6> Normals { {
        { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f },
        { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
    } };
    constexpr float PlateDepth = 1.0f / 16.0f;
    constexpr float FrameItemSize = 0.5f;
    for (const FrameItemView& frame : frameItemViews) {
        HeldItemMesh* mesh = heldMesh(frame.item);
        if (!mesh || mesh->faces.empty()) {
            continue;
        }
        Vec3 normal = Normals[size_t(std::clamp(frame.facing, 0, 5))];
        Vec3 up = frame.facing == 1 ? Vec3 { 0.0f, 0.0f, -1.0f } : frame.facing == 0 ? Vec3 { 0.0f, 0.0f, 1.0f } : Vec3 { 0.0f, 1.0f, 0.0f };
        Vec3 back = scaled(normal, -1.0f);
        Vec3 right {
            back[1] * up[2] - back[2] * up[1],
            back[2] * up[0] - back[0] * up[2],
            back[0] * up[1] - back[1] * up[0],
        };
        float turn = -frame.rotation * Pi / 180.0f;
        Vec3 turnedRight = add(scaled(right, std::cos(turn)), scaled(up, std::sin(turn)));
        Vec3 turnedUp = add(scaled(up, std::cos(turn)), scaled(right, -std::sin(turn)));
        float scale = mesh->map ? 1.0f / HeldItemSize * 128.0f / 142.0f : mesh->block ? 1.0f : FrameItemSize / HeldItemSize;
        float lift = mesh->block ? HeldCubeSize * 0.5f : 0.01f;
        Vec3 center {
            float(frame.cell[0] - origin[0]) + 0.5f,
            float(frame.cell[1] - origin[1]) + 0.5f,
            float(frame.cell[2] - origin[2]) + 0.5f,
        };
        center = add(center, scaled(normal, -0.5f + PlateDepth + lift));
        auto place = [&](const Vec3& local, bool) {
            Vec3 point = add(add(add(center, scaled(turnedRight, local[0] * scale)), scaled(turnedUp, local[1] * scale)), scaled(normal, local[2] * scale));
            return scaled(point, 256.0f);
        };
        size_t first = out.size();
        appendHeldItem(frame.item, place, out);
        lightQuads(out, first, lightCorners(frame.cell[0] + 0.5 + normal[0] * 0.5, frame.cell[1] + 0.5 + normal[1] * 0.5, frame.cell[2] + 0.5 + normal[2] * 0.5));
    }
}

void Client::appendShelfItems(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out)
{
    for (const ShelfItemView& shelf : shelfItemViews) {
        const float yaw = float(shelf.rotation) * Pi * 0.5f;
        const float cosine = std::cos(yaw);
        const float sine = std::sin(yaw);
        for (size_t slot = 0; slot < shelf.items.size(); ++slot) {
            const HudItem& item = shelf.items[slot];
            if (item.empty()) {
                continue;
            }
            HeldItemMesh* mesh = heldMesh(item);
            if (!mesh || mesh->faces.empty()) {
                continue;
            }
            const float scale = 0.25f / (mesh->block ? HeldCubeSize : HeldItemSize);
            const float offset = (float(slot) - 1.0f) * 0.3125f;
            auto place = [&](const Vec3& point, bool) {
                const float x = offset + point[0] * scale;
                const float z = -0.25f + point[2] * scale;
                return Vec3 {
                    (float(shelf.cell[0] - origin[0]) + 0.5f + x * cosine - z * sine) * 256.0f,
                    (float(shelf.cell[1] - origin[1]) + 0.5f + point[1] * scale) * 256.0f,
                    (float(shelf.cell[2] - origin[2]) + 0.5f + x * sine + z * cosine) * 256.0f,
                };
            };
            const size_t first = out.size();
            appendHeldItem(item, place, out);
            lightQuads(out, first, lightCorners(shelf.cell[0] + 0.5 - sine * 0.5, shelf.cell[1] + 0.5, shelf.cell[2] + 0.5 + cosine * 0.5));
        }
    }
}

void Client::appendVaultItems(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out)
{
    const float yaw = float(std::fmod((secondsNow() - startSeconds) * 3.4906585, 2.0 * Pi));
    const float cosine = std::cos(yaw);
    const float sine = std::sin(yaw);
    for (const VaultItemView& vault : vaultItemViews) {
        HeldItemMesh* mesh = heldMesh(vault.item);
        if (!mesh || mesh->faces.empty()) continue;
        const float scale = mesh->block ? 0.5f : 0.5f / HeldItemSize;
        auto place = [&](const Vec3& point, bool) {
            return Vec3 {
                (float(vault.cell[0] - origin[0]) + 0.5f + (point[0] * cosine - point[2] * sine) * scale) * 256,
                (float(vault.cell[1] - origin[1]) + 0.4f + point[1] * scale) * 256,
                (float(vault.cell[2] - origin[2]) + 0.5f + (point[0] * sine + point[2] * cosine) * scale) * 256,
            };
        };
        const size_t first = out.size();
        appendHeldItem(vault.item, place, out);
        lightQuads(out, first, lightCorners(vault.cell[0] + 0.5, vault.cell[1] + 0.4, vault.cell[2] + 0.5));
    }
}

/**
 * A filled map held in first person the way the game shows it: in front of
 * the view, raised toward the eye as the player looks down and lowered while
 * the hand comes up, following the swing. The arms are posed by the pack's
 * map animations. False while the map content has not arrived.
 */
bool Client::appendFirstPersonMap(const HudItem& held, float attackTime, const std::array<std::array<float, 3>, 3>& axes, const std::array<float, 3>& eyePoint, float handZoom, const std::array<float, 16>& viewMotion, std::vector<world::ModelQuadGpu>& out)
{
    HeldItemMesh* mesh = heldMesh(held);
    if (!mesh || !mesh->map) {
        return false;
    }
    float pitch = camera.minecraftPitch();
    float root = std::sqrt(std::max(attackTime, 0.0f));
    float lift = -0.2f * std::sin(attackTime * Pi);
    float push = -0.4f * std::sin(root * Pi);
    float tilt = std::clamp(1.0f - pitch / 45.0f + 0.1f, 0.0f, 1.0f);
    tilt = -std::cos(tilt * Pi) * 0.5f + 0.5f;
    Mat4 placement = viewMotion * translation(0.0f, -lift / 2.0f, push) * translation(0.0f, 0.04f + (1.0f - handEquip) * -1.2f + tilt * -0.5f, -0.72f)
        * rotationX(tilt * -85.0f) * rotationX(std::sin(root * Pi) * 20.0f) * uniformScale(2.0f * 0.38f);
    uint32_t contentLayer = heldItemLayer() + mesh->slot;
    uint32_t shade = EntityQuadFlag | (1u << 8) | 6;
    auto corner = [&](float u, float v, float z) {
        Vec3 view = transformed(placement, { u / 128.0f - 0.5f, -(v / 128.0f - 0.5f), z });
        Vec3 world = add(add(scaled(axes[0], view[0] * handZoom), scaled(axes[1], view[1] * handZoom)), scaled(axes[2], view[2]));
        return add(eyePoint, scaled(world, 256.0f));
    };
    auto quad = [&](float low, float high, float z, uint32_t layer) {
        std::array<Vec3, 4> corners { corner(low, low, z), corner(high, low, z), corner(high, high, z), corner(low, high, z) };
        std::array<std::array<uint16_t, 2>, 4> uvs { { { 0, 0 }, { 4096, 0 }, { 4096, 4096 }, { 0, 4096 } } };
        out.push_back(packQuad(corners, uvs, layer, shade));
    };
    quad(-7.0f, 135.0f, 0.0f, mapBackgroundLayer());
    quad(0.0f, 128.0f, 0.004f, contentLayer);
    return true;
}

}
