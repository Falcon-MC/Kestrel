#include "client/Client.h"

#include "platform/Window.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

using Point = std::array<double, 3>;

constexpr double OverlayScale = 1024.0;
constexpr double OverlayReach = 30.0;
constexpr double OutlineInflate = 0.002;
constexpr double CrackLift = 1.0 / OverlayScale;
constexpr float ReferenceHeight = 540.0f;
constexpr uint32_t OutlineWord = 1u << 5;

Point subtract(const Point& a, const Point& b)
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

Point cross(const Point& a, const Point& b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

double length(const Point& a)
{
    return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

/**
 * Packs an overlay quad from corners relative to the draw origin, in blocks.
 */
world::ModelQuadGpu packOverlay(const std::array<Point, 4>& corners, const std::array<std::array<double, 2>, 4>& uvs, uint32_t material, uint32_t shadeWord)
{
    world::ModelQuadGpu gpu;
    std::array<int16_t, 12> positions {};
    for (size_t corner = 0; corner < 4; ++corner) {
        for (size_t axis = 0; axis < 3; ++axis) {
            positions[corner * 3 + axis] = static_cast<int16_t>(std::clamp(std::lround(corners[corner][axis] * OverlayScale), -32768L, 32767L));
        }
    }
    for (size_t word = 0; word < 6; ++word) {
        gpu.words[word] = uint32_t(uint16_t(positions[word * 2])) | (uint32_t(uint16_t(positions[word * 2 + 1])) << 16);
    }
    for (size_t corner = 0; corner < 4; ++corner) {
        uint32_t u = static_cast<uint32_t>(std::clamp(uvs[corner][0] * 4096.0 + 0.5, 0.0, 65535.0));
        uint32_t v = static_cast<uint32_t>(std::clamp(uvs[corner][1] * 4096.0 + 0.5, 0.0, 65535.0));
        gpu.words[6 + corner] = u | (v << 16);
    }
    gpu.words[10] = material;
    gpu.words[11] = shadeWord;
    return gpu;
}

/**
 * Lays the crack texture over a face by where its corners sit in the block,
 * seen along the face's main axis, so every face shows the whole pattern.
 */
std::array<std::array<double, 2>, 4> projectedUvs(const std::array<Point, 4>& local, const Point& normal)
{
    std::array<std::array<double, 2>, 4> uvs {};
    double ax = std::abs(normal[0]);
    double ay = std::abs(normal[1]);
    double az = std::abs(normal[2]);
    for (size_t corner = 0; corner < 4; ++corner) {
        const Point& p = local[corner];
        if (ay >= ax && ay >= az) {
            uvs[corner] = { p[0], p[2] };
        } else if (ax >= az) {
            uvs[corner] = { p[2], 1.0 - p[1] };
        } else {
            uvs[corner] = { p[0], 1.0 - p[1] };
        }
    }
    return uvs;
}

}

/**
 * The outline around the targeted block and the cracks on every block being
 * broken. The outline is the game's thin black box, its edges kept a steady
 * width on screen; the cracks cover the block's model, or the boxes of its
 * shape when it has none, one stage per tenth of the progress.
 */
void Client::appendBlockOverlays(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out)
{
    if (!blockAssets || !playerView.active) {
        return;
    }
    Point eye { camera.x(), camera.y(), camera.z() };
    Point base { double(origin[0]), double(origin[1]), double(origin[2]) };
    float aspect = static_cast<float>(window->width()) / static_cast<float>(std::max<uint32_t>(window->height(), 1));
    double worldPerPixel = 2.0 * camera.halfVerticalTangent(aspect) / std::max<double>(window->height(), 1.0);
    double lineWidth = std::max(1.0, window->height() / double(ReferenceHeight));

    for (const BlockCrack& crack : crackViews) {
        Point center { crack.cell[0] + 0.5, crack.cell[1] + 0.5, crack.cell[2] + 0.5 };
        if (length(subtract(center, eye)) > OverlayReach) {
            continue;
        }
        uint32_t stage = std::min(static_cast<uint32_t>(crack.progress * 10.0f), world::DestroyStages - 1);
        uint32_t layer = blockAssets->destroyStageLayer(stage);
        if (layer == 0) {
            continue;
        }
        Point cell { double(crack.cell[0]), double(crack.cell[1]), double(crack.cell[2]) };
        auto emitFace = [&](const std::array<Point, 4>& local, const Point& normal) {
            std::array<Point, 4> corners;
            for (size_t corner = 0; corner < 4; ++corner) {
                for (size_t axis = 0; axis < 3; ++axis) {
                    corners[corner][axis] = cell[axis] - base[axis] + local[corner][axis] + normal[axis] * CrackLift;
                }
            }
            out.push_back(packOverlay(corners, projectedUvs(local, normal), layer, 0));
        };

        const world::BlockVisual& visual = crack.visual;
        const auto& templates = blockAssets->modelTemplates();
        if (visual.hasModel() && visual.blockEntity == world::EntityNone && visual.modelTemplate < templates.size()) {
            static const Point FaceNormals[7] = { { 0, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
            const world::ModelTemplate& modelTemplate = templates[visual.modelTemplate];
            for (uint32_t q = 0; q < modelTemplate.quadCount; ++q) {
                const world::ModelQuad& quad = blockAssets->modelQuads()[modelTemplate.quadStart + q];
                std::array<Point, 4> local;
                for (size_t corner = 0; corner < 4; ++corner) {
                    double px = quad.positions[corner][0] / 256.0 - 0.5;
                    double pz = quad.positions[corner][2] / 256.0 - 0.5;
                    for (uint32_t turn = 0; turn < (visual.variant & 3); ++turn) {
                        double rotated = -pz;
                        pz = px;
                        px = rotated;
                    }
                    local[corner] = { px + 0.5, quad.positions[corner][1] / 256.0, pz + 0.5 };
                }
                Point normal = cross(subtract(local[1], local[0]), subtract(local[3], local[0]));
                double size = length(normal);
                if (size < 1.0e-9) {
                    continue;
                }
                uint32_t faceId = quad.flags & world::QuadFaceMask;
                Point expected = faceId < 7 ? FaceNormals[faceId] : Point {};
                for (uint32_t turn = 0; turn < (visual.variant & 3); ++turn) {
                    expected = { -expected[2], expected[1], expected[0] };
                }
                for (double& component : normal) {
                    component /= size;
                }
                if (normal[0] * expected[0] + normal[1] * expected[1] + normal[2] * expected[2] < 0.0) {
                    normal = { -normal[0], -normal[1], -normal[2] };
                }
                emitFace(local, normal);
                if (quad.flags & world::QuadTwoSided) {
                    emitFace(local, { -normal[0], -normal[1], -normal[2] });
                }
            }
            continue;
        }
        for (const world::CollisionBox& box : crack.boxes) {
            double x0 = box.minX, y0 = box.minY, z0 = box.minZ;
            double x1 = box.maxX, y1 = box.maxY, z1 = box.maxZ;
            emitFace({ { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y0, z0 }, { x0, y0, z0 } } }, { 0, -1, 0 });
            emitFace({ { { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 } } }, { 0, 1, 0 });
            emitFace({ { { x1, y1, z0 }, { x0, y1, z0 }, { x0, y0, z0 }, { x1, y0, z0 } } }, { 0, 0, -1 });
            emitFace({ { { x0, y1, z1 }, { x1, y1, z1 }, { x1, y0, z1 }, { x0, y0, z1 } } }, { 0, 0, 1 });
            emitFace({ { { x0, y1, z0 }, { x0, y1, z1 }, { x0, y0, z1 }, { x0, y0, z0 } } }, { -1, 0, 0 });
            emitFace({ { { x1, y1, z1 }, { x1, y1, z0 }, { x1, y0, z0 }, { x1, y0, z1 } } }, { 1, 0, 0 });
        }
    }

    if (!selectionView) {
        return;
    }
    Point low = selectionView->min;
    Point high = selectionView->max;
    for (size_t axis = 0; axis < 3; ++axis) {
        low[axis] -= OutlineInflate;
        high[axis] += OutlineInflate;
    }
    if (length(subtract(low, eye)) > OverlayReach) {
        return;
    }
    std::array<Point, 8> corners;
    for (size_t i = 0; i < 8; ++i) {
        corners[i] = { (i & 1) ? high[0] : low[0], (i & 2) ? high[1] : low[1], (i & 4) ? high[2] : low[2] };
    }
    static constexpr std::pair<size_t, size_t> Edges[12] = {
        { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
        { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    };
    const std::array<std::array<double, 2>, 4> noUv {};
    for (const auto& [from, to] : Edges) {
        const Point& a = corners[from];
        const Point& b = corners[to];
        Point along = subtract(b, a);
        double span = length(along);
        Point middle { (a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5, (a[2] + b[2]) * 0.5 };
        Point side = cross(along, subtract(eye, middle));
        double sideLength = length(side);
        if (span < 1.0e-9 || sideLength < 1.0e-9) {
            continue;
        }
        auto halfWidth = [&](const Point& at) {
            return length(subtract(at, eye)) * worldPerPixel * lineWidth * 0.5;
        };
        double startHalf = halfWidth(a);
        double endHalf = halfWidth(b);
        std::array<Point, 4> quad;
        for (size_t axis = 0; axis < 3; ++axis) {
            double direction = along[axis] / span;
            double normal = side[axis] / sideLength;
            double start = a[axis] - direction * startHalf - base[axis];
            double end = b[axis] + direction * endHalf - base[axis];
            quad[0][axis] = start - normal * startHalf;
            quad[1][axis] = start + normal * startHalf;
            quad[2][axis] = end + normal * endHalf;
            quad[3][axis] = end - normal * endHalf;
        }
        out.push_back(packOverlay(quad, noUv, 0, OutlineWord));
    }
}

}
