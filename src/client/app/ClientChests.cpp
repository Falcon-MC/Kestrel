#include "client/Client.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

constexpr uint32_t FullSkyLight = 0xF0F0F0F0u;
constexpr float LidSpeed = 2.0f;
constexpr float HingeY = 144.0f;
constexpr float HingeZ = 16.0f;
constexpr float BlockCenter = 128.0f;
constexpr float Pi = 3.14159265f;

using Vec3 = std::array<float, 3>;

/**
 * The face id whose outward normal lies closest to a direction.
 */
uint32_t faceIdFor(const Vec3& normal)
{
    float ax = std::abs(normal[0]);
    float ay = std::abs(normal[1]);
    float az = std::abs(normal[2]);
    if (ay >= ax && ay >= az) {
        return normal[1] < 0.0f ? 1u : 2u;
    }
    if (ax >= az) {
        return normal[0] < 0.0f ? 3u : 4u;
    }
    return normal[2] < 0.0f ? 5u : 6u;
}

}

/**
 * The lids of chests being opened or shut, turned up around their back edge
 * by the game's eased angle and faced the chest's way. Each lid glides at the
 * lid's tick pace between the ticks the session reports.
 */
void Client::appendChestLids(const std::array<int32_t, 3>& origin, double deltaSeconds, std::vector<world::ModelQuadGpu>& out)
{
    if (!blockAssets) {
        chestLidShown.clear();
        return;
    }
    std::map<std::array<int32_t, 3>, float> shown;
    const std::vector<world::ModelTemplate>& templates = blockAssets->modelTemplates();
    const std::vector<world::Material>& materials = blockAssets->materials();
    static constexpr Vec3 FaceNormals[7] = { { 0, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    for (const ChestLidView& view : chestLidViews) {
        if (view.lid.modelTemplate >= templates.size()) {
            continue;
        }
        float target = std::clamp(view.openness + (view.open ? 0.1f : -0.1f), 0.0f, 1.0f);
        float previous = view.openness;
        if (auto found = chestLidShown.find(view.cell); found != chestLidShown.end()) {
            previous = found->second;
        }
        float step = LidSpeed * static_cast<float>(deltaSeconds);
        float openness = previous < target ? std::min(previous + step, target) : std::max(previous - step, target);
        shown[view.cell] = openness;

        float eased = 1.0f - (1.0f - openness) * (1.0f - openness) * (1.0f - openness);
        float angle = eased * Pi * 0.5f;
        float lift = std::sin(angle);
        float keep = std::cos(angle);
        float yaw = static_cast<float>(view.lid.rotation) * Pi * 0.5f;
        float yawCos = std::cos(yaw);
        float yawSin = std::sin(yaw);
        std::array<float, 3> base {
            static_cast<float>(view.cell[0] - origin[0]) * 256.0f,
            static_cast<float>(view.cell[1] - origin[1]) * 256.0f,
            static_cast<float>(view.cell[2] - origin[2]) * 256.0f,
        };
        auto hinge = [&](const Vec3& point) {
            float dy = point[1] - HingeY;
            float dz = point[2] - HingeZ;
            return Vec3 { point[0], HingeY + dy * keep + dz * lift, HingeZ - dy * lift + dz * keep };
        };
        auto turn = [&](const Vec3& point) {
            float dx = point[0] - BlockCenter;
            float dz = point[2] - BlockCenter;
            return Vec3 { BlockCenter + dx * yawCos - dz * yawSin, point[1], BlockCenter + dx * yawSin + dz * yawCos };
        };
        auto direct = [&](const Vec3& normal) {
            Vec3 opened { normal[0], normal[1] * keep + normal[2] * lift, -normal[1] * lift + normal[2] * keep };
            return Vec3 { opened[0] * yawCos - opened[2] * yawSin, opened[1], opened[0] * yawSin + opened[2] * yawCos };
        };

        const world::ModelTemplate& modelTemplate = templates[view.lid.modelTemplate];
        for (uint32_t q = 0; q < modelTemplate.quadCount; ++q) {
            const world::ModelQuad& quad = blockAssets->modelQuads()[modelTemplate.quadStart + q];
            if (quad.material >= materials.size()) {
                continue;
            }
            world::ModelQuadGpu gpu;
            std::array<int16_t, 12> positions {};
            for (size_t corner = 0; corner < 4; ++corner) {
                Vec3 local { float(quad.positions[corner][0]), float(quad.positions[corner][1]), float(quad.positions[corner][2]) };
                Vec3 placed = turn(hinge(local));
                for (size_t axis = 0; axis < 3; ++axis) {
                    positions[corner * 3 + axis] = static_cast<int16_t>(std::lround(base[axis] + placed[axis]));
                }
            }
            for (size_t word = 0; word < 6; ++word) {
                gpu.words[word] = uint32_t(uint16_t(positions[word * 2])) | (uint32_t(uint16_t(positions[word * 2 + 1])) << 16);
            }
            for (size_t corner = 0; corner < 4; ++corner) {
                gpu.words[6 + corner] = uint32_t(quad.uvs[corner][0]) | (uint32_t(quad.uvs[corner][1]) << 16);
            }
            uint32_t faceId = quad.flags & world::QuadFaceMask;
            gpu.words[10] = materials[quad.material].gpuWord();
            gpu.words[11] = faceId < 7 ? faceIdFor(direct(FaceNormals[faceId])) : 0u;
            gpu.words[12] = FullSkyLight;
            out.push_back(gpu);
        }
    }
    chestLidShown = std::move(shown);
}

}
