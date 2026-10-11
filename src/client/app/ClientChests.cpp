#include "client/Client.h"
#include "world/ConduitState.h"
#include "world/ShulkerLid.h"

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

template <class Place>
void appendBlockModel(const world::BlockAssets& assets, uint32_t index, Place place, std::vector<world::ModelQuadGpu>& out, uint32_t tint = 0)
{
    if (index >= assets.modelTemplates().size()) return;
    const auto& model = assets.modelTemplates()[index];
    for (uint32_t q = 0; q < model.quadCount; ++q) {
        const world::ModelQuad& quad = assets.modelQuads()[model.quadStart + q];
        if (quad.material >= assets.materials().size()) continue;
        world::ModelQuadGpu gpu;
        std::array<Vec3, 4> positions;
        Vec3 center {};
        for (size_t corner = 0; corner < 4; ++corner) {
            Vec3 point { float(quad.positions[corner][0]), float(quad.positions[corner][1]), float(quad.positions[corner][2]) };
            positions[corner] = place(point);
            for (size_t axis = 0; axis < 3; ++axis) center[axis] += point[axis] * 0.25f;
            gpu.words[6 + corner] = uint32_t(quad.uvs[corner][0]) | (uint32_t(quad.uvs[corner][1]) << 16);
        }
        packEntityPositions(positions, gpu.words);
        gpu.words[10] = assets.materials()[quad.material].gpuWord();
        gpu.words[11] = world::posedShadeFace(quad.flags & world::QuadFaceMask, center, 16.0f, place) | ((tint & 0xFFFFFFu) << 8);
        gpu.words[12] = FullSkyLight;
        out.push_back(gpu);
    }
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

        if (view.lid.shulker) {
            const size_t first = out.size();
            auto place = [&](const Vec3& point) {
                Vec3 posed = world::shulkerLidPosition({ point[0] / 16, point[1] / 16, point[2] / 16 }, openness, view.lid.facing);
                for (size_t axis = 0; axis < 3; ++axis) {
                    posed[axis] = posed[axis] * 16 + float(view.cell[axis] - origin[axis]) * 256;
                }
                return posed;
            };
            appendBlockModel(*blockAssets, view.lid.modelTemplate, place, out);
            lightQuads(out, first, lightCorners(view.cell[0] + 0.5, view.cell[1] + 0.5, view.cell[2] + 0.5));
            continue;
        }

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

void Client::appendEnchantingBooks(const std::array<int32_t, 3>& origin, double deltaSeconds, std::vector<world::ModelQuadGpu>& out)
{
    if (!blockAssets) {
        enchantingBookShown.clear();
        return;
    }
    std::map<std::array<int32_t, 3>, world::BookAnimation> shown;
    for (const EnchantingBookView& view : enchantingBookViews) {
        world::BookAnimation animation;
        if (const auto found = enchantingBookShown.find(view.cell); found != enchantingBookShown.end()) {
            animation = found->second;
        } else {
            animation.rotation = animation.previousRotation = animation.targetRotation = view.rotation;
            animation.random = uint32_t(view.cell[0]) * 73856093u ^ uint32_t(view.cell[1]) * 19349663u ^ uint32_t(view.cell[2]) * 83492791u;
            if (!animation.random) animation.random = 1;
        }
        std::optional<float> facing;
        double nearest = 9.0;
        auto player = [&](double x, double y, double z) {
            const double dx = x - view.cell[0] - 0.5;
            const double dy = y - view.cell[1] - 0.5;
            const double dz = z - view.cell[2] - 0.5;
            const double distance = dx * dx + dy * dy + dz * dz;
            if (distance < nearest) {
                nearest = distance;
                facing = float(std::atan2(dz, dx));
            }
        };
        if (playerView.active) {
            player(playerView.current[0], playerView.current[1], playerView.current[2]);
        }
        for (const ActorView& actor : actorViews) {
            if (actor.identifier == "minecraft:player") player(actor.x, actor.y, actor.z);
        }
        if (!view.lectern) {
            animation.advance(deltaSeconds, facing);
            shown.emplace(view.cell, animation);
        }
        const size_t first = out.size();
        for (size_t part = 0; part < 7; ++part) {
            const uint32_t index = blockAssets->enchantingBookTemplates()[part];
            if (index >= blockAssets->modelTemplates().size()) {
                continue;
            }
            auto place = [&](const Vec3& point) {
                Vec3 pixels { point[0] / 16.0f, point[1] / 16.0f, point[2] / 16.0f };
                Vec3 posed = world::bookPartPosition(part, pixels, float(animation.ticks), animation.shownOpenness(),
                    animation.shownFlip(), view.lectern ? view.rotation : animation.shownRotation(), view.lectern);
                for (size_t axis = 0; axis < 3; ++axis) {
                    posed[axis] = posed[axis] * 16.0f + float(view.cell[axis] - origin[axis]) * 256.0f;
                }
                return posed;
            };
            appendBlockModel(*blockAssets, index, place, out);
        }
        lightQuads(out, first, lightCorners(view.cell[0] + 0.5, view.cell[1] + 1.0, view.cell[2] + 0.5));
    }
    enchantingBookShown = std::move(shown);
}

void Client::appendConduits(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out)
{
    if (!blockAssets) return;
    const float ticks = float(std::fmod((secondsNow() - startSeconds) * 20.0, 1048576.0));
    const float eyeYaw = camera.minecraftYaw() * Pi / 180.0f + Pi;
    const float eyePitch = -camera.minecraftPitch() * Pi / 180.0f;
    for (const ConduitView& view : conduitViews) {
        const size_t first = out.size();
        auto append = [&](size_t part, bool inner) {
            auto place = [&](const Vec3& point) {
                Vec3 posed = world::conduitPartPosition(part,
                    { point[0] / 16.0f, point[1] / 16.0f, point[2] / 16.0f }, ticks, eyeYaw, eyePitch, inner);
                for (size_t axis = 0; axis < 3; ++axis) {
                    posed[axis] = posed[axis] * 16.0f + float(view.cell[axis] - origin[axis]) * 256.0f;
                }
                return posed;
            };
            appendBlockModel(*blockAssets, blockAssets->activeConduitTemplates()[part], place, out);
        };
        append(0, false);
        const size_t wind = int(ticks / 66.0f) % 3 == 1 ? 2 : 1;
        append(wind, false);
        append(wind, true);
        append(view.open ? 4 : 3, false);
        lightQuads(out, first, lightCorners(view.cell[0] + 0.5, view.cell[1] + 0.5, view.cell[2] + 0.5));
    }
}

void Client::appendBanners(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out, std::vector<world::ModelQuadGpu>& blended)
{
    if (!blockAssets) return;
    const double ticks = (secondsNow() - startSeconds) * 20.0;
    for (const BannerView& view : bannerViews) {
        auto append = [&](uint32_t model, uint32_t tint, size_t layer, std::vector<world::ModelQuadGpu>& target) {
            const size_t first = target.size();
            auto place = [&](const Vec3& point) {
                Vec3 pixels { point[0] / 16, point[1] / 16, point[2] / 16 };
                if (layer) {
                    const float middle = view.wall ? 2.5f : 10.5f;
                    pixels[2] += (pixels[2] > middle ? 1 : -1) * float(layer) * 0.125f;
                }
                Vec3 posed = world::bannerClothPosition(pixels, view.wall, view.rotation, ticks, view.cell);
                for (size_t axis = 0; axis < 3; ++axis) {
                    posed[axis] = posed[axis] * 16 + float(view.cell[axis] - origin[axis]) * 256;
                }
                return posed;
            };
            appendBlockModel(*blockAssets, model, place, target, tint);
            lightQuads(target, first, lightCorners(view.cell[0] + 0.5, view.cell[1] + (view.wall ? 0.5 : 1.5), view.cell[2] + 0.5));
        };
        append(blockAssets->bannerClothTemplate(view.wall, view.display.color), 0, 0, out);
        if (view.display.illager) {
            append(blockAssets->bannerPatternTemplate(view.wall, world::BannerPatternNames.size()), 0, 1, blended);
        } else {
            size_t layer = 0;
            for (const auto& pattern : view.display.patterns) {
                append(blockAssets->bannerPatternTemplate(view.wall, pattern.pattern), world::BannerDyeColors[pattern.color], ++layer, blended);
            }
        }
    }
}

void Client::appendPots(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out)
{
    if (!blockAssets) return;
    const double now = secondsNow();
    for (const PotView& pot : potViews) {
        auto place = [&](const Vec3& point) {
            Vec3 posed = world::potPosition({ point[0] / 16, point[1] / 16, point[2] / 16 },
                pot.rotation, pot.animation, now - pot.animationStart);
            for (size_t axis = 0; axis < 3; ++axis) posed[axis] = posed[axis] * 16 + float(pot.cell[axis] - origin[axis]) * 256;
            return posed;
        };
        const size_t first = out.size();
        appendBlockModel(*blockAssets, blockAssets->potBodyTemplate(), place, out);
        for (size_t side = 0; side < pot.patterns.size(); ++side) {
            appendBlockModel(*blockAssets, blockAssets->potSideTemplate(pot.patterns[side], side), place, out);
        }
        lightQuads(out, first, lightCorners(pot.cell[0] + 0.5, pot.cell[1] + 0.5, pot.cell[2] + 0.5));
    }
}

void Client::appendPistons(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out, std::vector<world::ModelQuadGpu>& blended)
{
    if (!blockAssets) return;
    const double now = secondsNow();
    for (const PistonView& piston : pistonViews) {
        const auto direction = world::pistonDirection(piston.facing);
        const float progress = piston.animation.progress(now);
        auto place = [&](const Vec3& point) {
            Vec3 placed = point;
            float along = 0;
            for (size_t axis = 0; axis < 3; ++axis) along += (point[axis] - 128) * direction[axis];
            const float distance = std::max(along + progress * 256, -128.0f) - along;
            for (size_t axis = 0; axis < 3; ++axis) placed[axis] += distance * direction[axis] + float(piston.cell[axis] - origin[axis]) * 256;
            return placed;
        };
        const size_t first = out.size();
        appendBlockModel(*blockAssets, piston.head, place, out);
        lightQuads(out, first, lightCorners(piston.cell[0] + 0.5, piston.cell[1] + 0.5, piston.cell[2] + 0.5));
    }
    for (const MovingBlockView& block : movingBlockViews) {
        const auto found = std::find_if(pistonViews.begin(), pistonViews.end(), [&](const PistonView& piston) { return piston.cell == block.piston; });
        if (found == pistonViews.end()) continue;
        const auto direction = world::pistonDirection(found->facing);
        const float progress = found->animation.progress(now);
        const float offset = block.expanding ? progress - 1 : progress;
        ActorView actor;
        actor.variant = int32_t(block.value);
        actor.x = block.cell[0] + 0.5 + offset * direction[0];
        actor.y = block.cell[1] + offset * direction[1];
        actor.z = block.cell[2] + 0.5 + offset * direction[2];
        const size_t first = out.size(), firstBlended = blended.size();
        appendFallingBlock(actor, origin, out, blended);
        const auto light = lightCorners(actor.x, actor.y + 0.5, actor.z);
        lightQuads(out, first, light);
        lightQuads(blended, firstBlended, light);
    }
}

}
