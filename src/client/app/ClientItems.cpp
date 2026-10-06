#include "client/Client.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

using Vec3 = std::array<float, 3>;

constexpr float Pi = 3.14159265f;
constexpr uint32_t FullSkyLight = 0xF0F0F0F0u;
constexpr float BlockCubeSize = 0.5f;
constexpr float FlatItemSize = 0.65f;
constexpr float GroundBlockSize = 0.25f;
constexpr float GroundItemSize = 0.5f;
// how high a lying item floats over the ground, enough to stay out of it
constexpr float LyingLift = 0.02f;
constexpr double PickupSeconds = 0.15;

/**
 * How many copies of an item a dropped stack shows, the way the game hints
 * at its size.
 */
int copiesFor(int32_t count)
{
    return count > 48 ? 5 : count > 32 ? 4 : count > 16 ? 3 : count > 1 ? 2 : 1;
}

/**
 * A repeatable number from 0 to 1 for an entity and a stream, so each dropped
 * item keeps its own bob phase and copy offsets from frame to frame.
 */
float scatter(uint64_t seed, uint32_t stream)
{
    uint64_t value = (seed + 0x9E3779B97F4A7C15ull * (stream + 1)) * 0xBF58476D1CE4E5B9ull;
    value ^= value >> 31;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 29;
    return static_cast<float>(value & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

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

/**
 * The shape a dropped stack is drawn with, built once per item look. Flat
 * items take one of the dropped icon layers; when those run out the oldest
 * look gives its layer up and is built again next time it shows.
 */
const Client::DroppedItemMesh* Client::droppedItemMesh(const HudItem& item)
{
    if (item.empty() || !blockAssets || !renderer) {
        return nullptr;
    }
    std::string key = item.identifier + "#" + std::to_string(item.aux) + "#" + item.icon;
    if (auto found = droppedMeshes.find(key); found != droppedMeshes.end()) {
        return &found->second;
    }
    uint32_t slot = nextDroppedIcon;
    nextDroppedIcon = (nextDroppedIcon + 1) % world::DroppedIconSlots;
    if (!droppedIconKeys[slot].empty()) {
        droppedMeshes.erase(droppedIconKeys[slot]);
    }
    DroppedItemMesh mesh;
    mesh.iconSlot = slot;
    mesh.faces = buildItemMesh(item, heldItemLayer() + HeldItemTextureSlots + slot, mesh.block);
    if (mesh.block) {
        nextDroppedIcon = slot;
        droppedIconKeys[slot].clear();
    } else {
        droppedIconKeys[slot] = key;
    }
    return &droppedMeshes.emplace(key, std::move(mesh)).first->second;
}

void Client::appendFallingBlock(const ActorView& actor, const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out, std::vector<world::ModelQuadGpu>& blended)
{
    if (!seenSessionSnapshot) return;
    const auto sequential = blockAssets->sequentialMap();
    const auto& visual = blockAssets->visual(static_cast<uint32_t>(actor.variant), seenSessionSnapshot->hashedIds, sequential.get());
    const auto& materials = blockAssets->materials();
    auto& target = (visual.flags & world::FlagTranslucent) ? blended : out;
    Vec3 base {
        float((actor.x - 0.5 - origin[0]) * 256.0),
        float((actor.y - origin[1]) * 256.0),
        float((actor.z - 0.5 - origin[2]) * 256.0),
    };
    auto emit = [&](const std::array<Vec3, 4>& local, const std::array<std::array<uint16_t, 2>, 4>& uvs, uint32_t material, uint32_t shade) {
        if (material >= materials.size()) return;
        auto corners = local;
        for (auto& corner : corners) {
            for (size_t axis = 0; axis < 3; ++axis) corner[axis] = base[axis] + corner[axis] * actor.scale;
        }
        uint32_t tint = materials[material].tintKind() != world::TintKind::None ? world::ItemTint : 0u;
        target.push_back(packQuad(corners, uvs, materials[material].gpuWord(), shade | (tint << 8)));
    };
    if (visual.hasModel()) {
        const auto& templates = blockAssets->modelTemplates();
        const auto& quads = blockAssets->modelQuads();
        if (visual.modelTemplate >= templates.size()) return;
        const auto& model = templates[visual.modelTemplate];
        if (model.quadStart > quads.size() || model.quadCount > quads.size() - model.quadStart) return;
        for (size_t i = 0; i < model.quadCount; ++i) {
            const auto& quad = quads[model.quadStart + i];
            std::array<Vec3, 4> corners;
            for (size_t corner = 0; corner < 4; ++corner) {
                for (size_t axis = 0; axis < 3; ++axis) corners[corner][axis] = float(quad.positions[corner][axis]);
            }
            emit(corners, quad.uvs, quad.material, world::shadeFaceTowards(world::modelFaceNormal(quad.flags & world::QuadFaceMask)));
        }
    } else if (visual.emitsCubeGeometry()) {
        const std::array<std::array<Vec3, 4>, 6> faces { {
            { { { 256, 256, 256 }, { 256, 256, 0 }, { 256, 0, 0 }, { 256, 0, 256 } } },
            { { { 0, 256, 0 }, { 0, 256, 256 }, { 0, 0, 256 }, { 0, 0, 0 } } },
            { { { 0, 256, 0 }, { 256, 256, 0 }, { 256, 256, 256 }, { 0, 256, 256 } } },
            { { { 0, 0, 256 }, { 256, 0, 256 }, { 256, 0, 0 }, { 0, 0, 0 } } },
            { { { 0, 256, 256 }, { 256, 256, 256 }, { 256, 0, 256 }, { 0, 0, 256 } } },
            { { { 256, 256, 0 }, { 0, 256, 0 }, { 0, 0, 0 }, { 256, 0, 0 } } },
        } };
        const std::array<std::array<uint16_t, 2>, 4> uvs { { { 0, 0 }, { 4096, 0 }, { 4096, 4096 }, { 0, 4096 } } };
        static constexpr uint32_t Shades[] = { 2, 1, 4, 3, 6, 5 };
        for (size_t face = 0; face < faces.size(); ++face) emit(faces[face], uvs, visual.faces[face ^ 1], Shades[face]);
    }
}

/**
 * A dropped item the way the game draws one on the ground: a block as a
 * small cube turning slowly, any other item as its icon facing the camera,
 * both bobbing up and down, with a few copies for a bigger stack. Once picked
 * up it flies to whoever took it and is gone.
 */
void Client::appendDroppedItem(const ActorView& actor, const std::array<int32_t, 3>& origin, double now, std::vector<world::ModelQuadGpu>& out)
{
    const DroppedItemMesh* mesh = droppedItemMesh(actor.item);
    if (!mesh || mesh->faces.empty()) {
        return;
    }
    std::array<double, 3> position { actor.x, actor.y, actor.z };
    if (actor.pickedUpAt > 0.0) {
        double progress = (now - actor.pickedUpAt) / PickupSeconds;
        if (progress >= 1.0) {
            return;
        }
        std::array<double, 3> target { camera.x(), camera.y() - 1.12, camera.z() };
        if (actor.pickedUpBy != localRuntime) {
            auto collector = std::find_if(actorViews.begin(), actorViews.end(), [&](const ActorView& view) { return view.runtimeId == actor.pickedUpBy; });
            if (collector == actorViews.end()) {
                return;
            }
            target = { collector->x, collector->y + 0.5, collector->z };
        }
        double eased = std::clamp(progress, 0.0, 1.0);
        eased *= eased;
        for (int axis = 0; axis < 3; ++axis) {
            position[axis] += (target[axis] - position[axis]) * eased;
        }
    }

    float ticks = static_cast<float>(std::fmod(now * 20.0, 1.0e6));
    float phase = scatter(actor.runtimeId, 0) * Pi * 2.0f;
    // item physics: no bobbing or spinning, and flat items lie down on their back
    bool still = visuals.itemPhysics.value_or(false);
    bool lying = still && !mesh->block;
    float bob = still ? 0.0f : std::sin(ticks / 10.0f + phase) * 0.1f + 0.1f;
    float scale = mesh->block ? GroundBlockSize / BlockCubeSize : GroundItemSize / FlatItemSize;
    float center = mesh->block ? GroundBlockSize * 0.5f : lying ? LyingLift : GroundItemSize * 0.5f;
    float turn = still ? phase : mesh->block ? ticks / 20.0f + phase : std::atan2(float(camera.x() - position[0]), float(camera.z() - position[2]));
    float turnCos = std::cos(turn);
    float turnSin = std::sin(turn);
    Vec3 base {
        static_cast<float>((position[0] - origin[0]) * 256.0),
        static_cast<float>((position[1] + bob + center - origin[1]) * 256.0),
        static_cast<float>((position[2] - origin[2]) * 256.0),
    };

    int copies = copiesFor(actor.item.count);
    for (int copy = 0; copy < copies; ++copy) {
        Vec3 shift {};
        if (copy > 0) {
            float spread = mesh->block ? 0.15f : 0.075f;
            shift = {
                (scatter(actor.runtimeId, uint32_t(copy) * 3 + 1) * 2.0f - 1.0f) * spread,
                (scatter(actor.runtimeId, uint32_t(copy) * 3 + 2) * 2.0f - 1.0f) * spread,
                mesh->block ? (scatter(actor.runtimeId, uint32_t(copy) * 3 + 3) * 2.0f - 1.0f) * spread : -0.09375f * float(copy),
            };
        }
        for (const HeldItemFace& face : mesh->faces) {
            std::array<Vec3, 4> corners;
            for (size_t corner = 0; corner < 4; ++corner) {
                Vec3 local {
                    face.corners[corner][0] * scale + shift[0],
                    face.corners[corner][1] * scale + shift[1],
                    face.corners[corner][2] * scale + shift[2],
                };
                if (lying) {
                    // tip it over backwards: its height runs along the ground, the stack piles upward
                    local = { local[0], -local[2], local[1] };
                }
                corners[corner] = {
                    base[0] + (local[0] * turnCos + local[2] * turnSin) * 256.0f,
                    base[1] + local[1] * 256.0f,
                    base[2] + (-local[0] * turnSin + local[2] * turnCos) * 256.0f,
                };
            }
            out.push_back(packQuad(corners, face.uvs, face.material, face.shade));
        }
    }
}

}
