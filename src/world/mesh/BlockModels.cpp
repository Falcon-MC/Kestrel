#include "world/BlockModels.h"

#include <algorithm>

namespace kestrel::world::models {

namespace {

constexpr int16_t Full = 256;
constexpr uint32_t FaceIds[6] = { 3, 4, 1, 2, 5, 6 };

uint32_t boundary(uint32_t id, bool touches)
{
    return id | (touches ? id << 4 : 0u);
}

std::array<std::array<uint16_t, 2>, 4> projectedUvs(uint32_t side, const std::array<Point, 4>& positions)
{
    std::array<std::array<uint16_t, 2>, 4> uvs {};
    for (size_t i = 0; i < 4; ++i) {
        const Point& p = positions[i];
        switch (side) {
        case West:
        case East:
            uvs[i] = { static_cast<uint16_t>(p[2] * 16), static_cast<uint16_t>(4096 - p[1] * 16) };
            break;
        case North:
        case South:
            uvs[i] = { static_cast<uint16_t>(p[0] * 16), static_cast<uint16_t>(4096 - p[1] * 16) };
            break;
        default:
            uvs[i] = { static_cast<uint16_t>(p[0] * 16), static_cast<uint16_t>(p[2] * 16) };
            break;
        }
    }
    return uvs;
}

std::array<Point, 4> cuboidFace(uint32_t side, Point min, Point max)
{
    auto [x0, y0, z0] = min;
    auto [x1, y1, z1] = max;
    switch (side) {
    case West:
        return { { { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 } } };
    case East:
        return { { { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x1, y0, z1 } } };
    case Down:
        return { { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 } } };
    case Up:
        return { { { x0, y1, z0 }, { x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 } } };
    case North:
        return { { { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 }, { x1, y0, z0 } } };
    default:
        return { { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } } };
    }
}

bool touchesBoundary(uint32_t side, Point min, Point max)
{
    switch (side) {
    case West:
        return min[0] == 0;
    case East:
        return max[0] == Full;
    case Down:
        return min[1] == 0;
    case Up:
        return max[1] == Full;
    case North:
        return min[2] == 0;
    default:
        return max[2] == Full;
    }
}

ModelQuad makeQuad(uint32_t side, Point min, Point max, uint32_t material, bool cull)
{
    ModelQuad quad;
    quad.positions = cuboidFace(side, min, max);
    quad.uvs = projectedUvs(side, quad.positions);
    quad.material = material;
    quad.flags = cull ? boundary(faceId(side), touchesBoundary(side, min, max)) : faceId(side);
    return quad;
}

void rectUvs(ModelQuad& quad, uint32_t side, std::array<uint16_t, 4> rect)
{
    uint16_t u1 = static_cast<uint16_t>(rect[0] * 256);
    uint16_t v1 = static_cast<uint16_t>(rect[1] * 256);
    uint16_t u2 = static_cast<uint16_t>(rect[2] * 256);
    uint16_t v2 = static_cast<uint16_t>(rect[3] * 256);
    switch (side) {
    case West:
    case South:
        quad.uvs = { { { u1, v2 }, { u2, v2 }, { u2, v1 }, { u1, v1 } } };
        break;
    case East:
    case North:
        quad.uvs = { { { u1, v2 }, { u1, v1 }, { u2, v1 }, { u2, v2 } } };
        break;
    case Down:
        quad.uvs = { { { u1, v1 }, { u2, v1 }, { u2, v2 }, { u1, v2 } } };
        break;
    default:
        quad.uvs = { { { u1, v1 }, { u1, v2 }, { u2, v2 }, { u2, v1 } } };
        break;
    }
}

/**
 * A two sided upright plane from (x0, z0) to (x1, z1), like a lantern handle
 * or a candle wick, with the texture rectangle given in pixels.
 */
ModelQuad uprightPlane(uint32_t material, int16_t x0, int16_t z0, int16_t x1, int16_t z1, int16_t bottom, int16_t top, std::array<uint16_t, 4> rect)
{
    ModelQuad quad;
    quad.positions = { { { x0, bottom, z0 }, { x1, bottom, z1 }, { x1, top, z1 }, { x0, top, z0 } } };
    auto u1 = static_cast<uint16_t>(rect[0] * 256);
    auto v1 = static_cast<uint16_t>(rect[1] * 256);
    auto u2 = static_cast<uint16_t>(rect[2] * 256);
    auto v2 = static_cast<uint16_t>(rect[3] * 256);
    quad.uvs = { { { u1, v2 }, { u2, v2 }, { u2, v1 }, { u1, v1 } } };
    quad.material = material;
    quad.flags = QuadTwoSided;
    return quad;
}

/**
 * A box in block pixels whose top and bottom show the ends rectangle and whose
 * sides show the sides rectangle of one texture.
 */
void pushTexturedBox(std::vector<ModelQuad>& out, uint32_t material, std::array<int16_t, 3> min, std::array<int16_t, 3> max, std::array<uint16_t, 4> ends, std::array<uint16_t, 4> sides)
{
    Materials materials;
    materials.fill(material);
    auto faces = cuboid(materials, { int16_t(min[0] * 16), int16_t(min[1] * 16), int16_t(min[2] * 16) }, { int16_t(max[0] * 16), int16_t(max[1] * 16), int16_t(max[2] * 16) });
    for (uint32_t side = 0; side < 6; ++side) {
        rectUvs(faces[side], side, side == Up || side == Down ? ends : sides);
        out.push_back(faces[side]);
    }
}

size_t cellIndex(size_t x, size_t y, size_t z)
{
    return x | (y << 1) | (z << 2);
}

bool toward(uint32_t orientation, size_t x, size_t z)
{
    switch (orientation) {
    case 0:
        return z == 1;
    case 1:
        return x == 0;
    case 2:
        return z == 0;
    case 3:
        return x == 1;
    default:
        return false;
    }
}

struct GateElement {
    Point min;
    Point max;
    std::array<std::array<uint16_t, 5>, 6> faces;
};

constexpr std::array<uint16_t, 5> Uv(uint16_t u1, uint16_t v1, uint16_t u2, uint16_t v2, uint16_t rotation = 0)
{
    return { u1, v1, u2, v2, static_cast<uint16_t>(rotation + 1) };
}

constexpr std::array<uint16_t, 5> NoUv {};

constexpr GateElement GateClosed[8] = {
    { { 0, 80, 112 }, { 32, 256, 144 }, { Uv(7, 0, 9, 11), Uv(7, 0, 9, 11), Uv(0, 7, 2, 9), Uv(0, 7, 2, 9), Uv(0, 0, 2, 11), Uv(0, 0, 2, 11) } },
    { { 224, 80, 112 }, { 256, 256, 144 }, { Uv(7, 0, 9, 11), Uv(7, 0, 9, 11), Uv(14, 7, 16, 9), Uv(14, 7, 16, 9), Uv(14, 0, 16, 11), Uv(14, 0, 16, 11) } },
    { { 96, 96, 112 }, { 128, 240, 144 }, { Uv(7, 1, 9, 10), Uv(7, 1, 9, 10), Uv(6, 7, 8, 9), Uv(6, 7, 8, 9), Uv(6, 1, 8, 10), Uv(6, 1, 8, 10) } },
    { { 128, 96, 112 }, { 160, 240, 144 }, { Uv(7, 1, 9, 10), Uv(7, 1, 9, 10), Uv(8, 7, 10, 9), Uv(8, 7, 10, 9), Uv(8, 1, 10, 10), Uv(8, 1, 10, 10) } },
    { { 32, 96, 112 }, { 96, 144, 144 }, { NoUv, NoUv, Uv(2, 7, 6, 9), Uv(2, 7, 6, 9), Uv(2, 7, 6, 10), Uv(2, 7, 6, 10) } },
    { { 32, 192, 112 }, { 96, 240, 144 }, { NoUv, NoUv, Uv(2, 7, 6, 9), Uv(2, 7, 6, 9), Uv(2, 1, 6, 4), Uv(2, 1, 6, 4) } },
    { { 160, 96, 112 }, { 224, 144, 144 }, { NoUv, NoUv, Uv(10, 7, 14, 9), Uv(10, 7, 14, 9), Uv(10, 7, 14, 10), Uv(10, 7, 14, 10) } },
    { { 160, 192, 112 }, { 224, 240, 144 }, { NoUv, NoUv, Uv(10, 7, 14, 9), Uv(10, 7, 14, 9), Uv(10, 1, 14, 4), Uv(10, 1, 14, 4) } },
};

constexpr GateElement GateOpen[8] = {
    GateClosed[0],
    GateClosed[1],
    { { 0, 96, 208 }, { 32, 240, 240 }, { Uv(13, 1, 15, 10), Uv(13, 1, 15, 10), Uv(0, 13, 2, 15), Uv(0, 13, 2, 15), Uv(0, 1, 2, 10), Uv(0, 1, 2, 10) } },
    { { 224, 96, 208 }, { 256, 240, 240 }, { Uv(13, 1, 15, 10), Uv(13, 1, 15, 10), Uv(14, 13, 16, 15), Uv(14, 13, 16, 15), Uv(14, 1, 16, 10), Uv(14, 1, 16, 10) } },
    { { 0, 96, 144 }, { 32, 144, 208 }, { Uv(13, 7, 15, 10), Uv(13, 7, 15, 10), Uv(0, 9, 2, 13), Uv(0, 9, 2, 13), NoUv, NoUv } },
    { { 0, 192, 144 }, { 32, 240, 208 }, { Uv(13, 1, 15, 4), Uv(13, 1, 15, 4), Uv(0, 9, 2, 13), Uv(0, 9, 2, 13), NoUv, NoUv } },
    { { 224, 96, 144 }, { 256, 144, 208 }, { Uv(13, 7, 15, 10), Uv(13, 7, 15, 10), Uv(14, 9, 16, 13), Uv(14, 9, 16, 13), NoUv, NoUv } },
    { { 224, 192, 144 }, { 256, 240, 208 }, { Uv(13, 1, 15, 4), Uv(13, 1, 15, 4), Uv(14, 9, 16, 13), Uv(14, 9, 16, 13), NoUv, NoUv } },
};

constexpr GateElement BambooGateClosed[8] = {
    { { 0, 80, 112 }, { 32, 256, 144 }, { Uv(14, 2, 16, 13), Uv(14, 2, 16, 13), Uv(16, 13, 14, 15), Uv(14, 0, 16, 2), Uv(14, 2, 16, 13), Uv(14, 2, 16, 13) } },
    { { 224, 80, 112 }, { 256, 256, 144 }, { Uv(0, 2, 2, 13), Uv(0, 2, 2, 13), Uv(2, 13, 0, 15), Uv(0, 0, 2, 2), Uv(0, 2, 2, 13), Uv(0, 2, 2, 13) } },
    { { 96, 96, 112 }, { 128, 240, 144 }, { Uv(8, 3, 10, 12), NoUv, Uv(8, 14, 10, 12), Uv(8, 1, 10, 3), Uv(8, 3, 10, 12), Uv(6, 3, 8, 12) } },
    { { 128, 96, 112 }, { 160, 240, 144 }, { NoUv, Uv(6, 3, 8, 12), Uv(6, 14, 8, 12), Uv(6, 1, 8, 3), Uv(6, 3, 8, 12), Uv(8, 3, 10, 12) } },
    { { 32, 96, 112 }, { 96, 144, 144 }, { NoUv, NoUv, Uv(10, 14, 14, 12), Uv(10, 1, 14, 3), Uv(10, 3, 14, 6), Uv(10, 9, 14, 12) } },
    { { 32, 192, 112 }, { 96, 240, 144 }, { NoUv, NoUv, Uv(10, 14, 14, 12), Uv(10, 1, 14, 3), Uv(10, 3, 14, 6), Uv(10, 9, 14, 12) } },
    { { 160, 96, 112 }, { 224, 144, 144 }, { NoUv, NoUv, Uv(2, 14, 6, 12), Uv(2, 1, 6, 3), Uv(2, 3, 6, 6), Uv(2, 9, 6, 12) } },
    { { 160, 192, 112 }, { 224, 240, 144 }, { NoUv, NoUv, Uv(2, 14, 6, 12), Uv(2, 1, 6, 3), Uv(2, 3, 6, 6), Uv(2, 9, 6, 12) } },
};

constexpr GateElement BambooGateOpen[8] = {
    BambooGateClosed[0],
    BambooGateClosed[1],
    { { 0, 96, 208 }, { 32, 240, 240 }, { Uv(8, 3, 10, 12), Uv(8, 3, 10, 12), Uv(8, 14, 10, 12), Uv(8, 1, 10, 3), Uv(8, 3, 10, 12), Uv(8, 3, 10, 12) } },
    { { 224, 96, 208 }, { 256, 240, 240 }, { Uv(6, 3, 8, 12), Uv(6, 3, 8, 12), Uv(6, 14, 8, 12), Uv(6, 1, 8, 3), Uv(6, 3, 8, 12), Uv(6, 3, 8, 12) } },
    { { 0, 96, 144 }, { 32, 144, 208 }, { Uv(2, 3, 6, 6), Uv(2, 9, 6, 12), Uv(2, 12, 6, 14, 270), Uv(2, 1, 6, 3, 270), NoUv, NoUv } },
    { { 0, 192, 144 }, { 32, 240, 208 }, { Uv(2, 3, 6, 6), Uv(2, 9, 6, 12), Uv(2, 12, 6, 14, 270), Uv(2, 1, 6, 3, 270), NoUv, NoUv } },
    { { 224, 96, 144 }, { 256, 144, 208 }, { Uv(10, 3, 14, 6), Uv(10, 9, 14, 12), Uv(10, 12, 14, 14, 270), Uv(10, 1, 14, 3, 270), NoUv, NoUv } },
    { { 224, 192, 144 }, { 256, 240, 208 }, { Uv(14, 3, 10, 6), Uv(10, 9, 14, 12), Uv(10, 12, 14, 14, 270), Uv(10, 1, 14, 3, 270), NoUv, NoUv } },
};

uint32_t rotateGateFace(uint32_t side, uint32_t orientation)
{
    static constexpr uint32_t Rotated[4][6] = {
        { West, East, Down, Up, North, South },
        { North, South, Down, Up, East, West },
        { East, West, Down, Up, South, North },
        { South, North, Down, Up, West, East },
    };
    return Rotated[orientation & 3][side];
}

std::pair<Point, Point> rotateGateBounds(Point min, Point max, uint32_t orientation)
{
    auto [x0, y0, z0] = min;
    auto [x1, y1, z1] = max;
    switch (orientation & 3) {
    case 1:
        return { { static_cast<int16_t>(Full - z1), y0, x0 }, { static_cast<int16_t>(Full - z0), y1, x1 } };
    case 2:
        return { { static_cast<int16_t>(Full - x1), y0, static_cast<int16_t>(Full - z1) }, { static_cast<int16_t>(Full - x0), y1, static_cast<int16_t>(Full - z0) } };
    case 3:
        return { { z0, y0, static_cast<int16_t>(Full - x1) }, { z1, y1, static_cast<int16_t>(Full - x0) } };
    default:
        return { min, max };
    }
}

uint32_t buttonRotatedFace(uint32_t side, uint32_t orientation)
{
    static constexpr uint32_t X90[6] = { West, East, South, North, Down, Up };
    static constexpr uint32_t Y90[6] = { North, South, Down, Up, East, West };
    auto yaw = [](uint32_t face, uint32_t turns) {
        for (uint32_t i = 0; i < turns; ++i) {
            face = Y90[face];
        }
        return face;
    };
    switch (orientation) {
    case 0:
        switch (side) {
        case Down:
            return Up;
        case Up:
            return Down;
        case North:
            return South;
        case South:
            return North;
        default:
            return side;
        }
    case 1:
        return side;
    case 2:
        return X90[side];
    case 3:
        return yaw(X90[side], 2);
    case 4:
        return yaw(X90[side], 3);
    default:
        return yaw(X90[side], 1);
    }
}

Point buttonRotatePosition(Point p, uint32_t orientation)
{
    auto [x, y, z] = p;
    switch (orientation) {
    case 0:
        return { x, static_cast<int16_t>(Full - y), static_cast<int16_t>(Full - z) };
    case 2:
        return { x, z, static_cast<int16_t>(Full - y) };
    case 3:
        return { static_cast<int16_t>(Full - x), z, y };
    case 4:
        return { static_cast<int16_t>(Full - y), z, static_cast<int16_t>(Full - x) };
    case 5:
        return { y, z, x };
    default:
        return p;
    }
}

std::pair<Point, Point> buttonBounds(uint32_t orientation, bool pressed)
{
    int16_t height = pressed ? 16 : 32;
    int16_t high = static_cast<int16_t>(Full - height);
    switch (orientation) {
    case 0:
        return { { 80, high, 96 }, { 176, Full, 160 } };
    case 1:
        return { { 80, 0, 96 }, { 176, height, 160 } };
    case 2:
        return { { 80, 96, high }, { 176, 160, Full } };
    case 3:
        return { { 80, 96, 0 }, { 176, 160, height } };
    case 4:
        return { { high, 96, 80 }, { Full, 160, 176 } };
    default:
        return { { 0, 96, 80 }, { height, 160, 176 } };
    }
}

std::array<uint16_t, 4> buttonFaceUv(uint32_t side, bool pressed)
{
    switch (side) {
    case Down:
    case Up:
        return { 5, 6, 11, 10 };
    case North:
    case South:
        return { 5, 14, 11, static_cast<uint16_t>(pressed ? 15 : 16) };
    default:
        return { 6, 14, 10, static_cast<uint16_t>(pressed ? 15 : 16) };
    }
}

std::array<uint16_t, 4> uvLockRect(uint32_t side, Point min, Point max)
{
    auto px = [](int16_t value) {
        return static_cast<uint16_t>(value / 16);
    };
    switch (side) {
    case West:
    case East:
        return { px(min[2]), static_cast<uint16_t>(16 - px(max[1])), px(max[2]), static_cast<uint16_t>(16 - px(min[1])) };
    case North:
    case South:
        return { px(min[0]), static_cast<uint16_t>(16 - px(max[1])), px(max[0]), static_cast<uint16_t>(16 - px(min[1])) };
    default:
        return { px(min[0]), px(min[2]), px(max[0]), px(max[2]) };
    }
}

}

uint32_t faceId(uint32_t side)
{
    return FaceIds[side];
}

std::array<ModelQuad, 6> cuboid(const Materials& materials, Point min, Point max)
{
    std::array<ModelQuad, 6> result {};
    for (uint32_t side = 0; side < 6; ++side) {
        result[side] = makeQuad(side, min, max, materials[side], false);
    }
    return result;
}

std::vector<ModelQuad> slab(const Materials& materials, uint32_t half)
{
    int16_t minY = half == 1 ? 128 : 0;
    int16_t maxY = half == 0 ? 128 : Full;
    std::vector<ModelQuad> result;
    for (uint32_t side = 0; side < 6; ++side) {
        result.push_back(makeQuad(side, { 0, minY, 0 }, { Full, maxY, Full }, materials[side], true));
    }
    return result;
}

std::vector<ModelQuad> stair(const Materials& materials, bool upsideDown, uint32_t shape)
{
    constexpr uint32_t Orientation = 2;
    std::array<bool, 8> occupied {};
    size_t baseY = upsideDown ? 1 : 0;
    size_t stepY = 1 - baseY;
    for (size_t x = 0; x < 2; ++x) {
        for (size_t z = 0; z < 2; ++z) {
            occupied[cellIndex(x, baseY, z)] = true;
            bool facing = toward(Orientation, x, z);
            bool right = toward((Orientation + 1) & 3, x, z);
            bool left = toward((Orientation + 3) & 3, x, z);
            bool opposite = toward((Orientation + 2) & 3, x, z);
            bool step = false;
            switch (shape) {
            case 0:
                step = facing;
                break;
            case 1:
                step = facing || (opposite && right);
                break;
            case 2:
                step = facing || (opposite && left);
                break;
            case 3:
                step = facing && left;
                break;
            case 4:
                step = facing && right;
                break;
            default:
                break;
            }
            if (step) {
                occupied[cellIndex(x, stepY, z)] = true;
            }
        }
    }

    // Each face goes out as few rectangles as its half cells allow. Half-cell quads leave vertices
    // midway along the edges of the faces around them, and those T-junctions show up as specks.
    std::vector<ModelQuad> result;
    for (uint32_t side = 0; side < 6; ++side) {
        size_t normal = side == West || side == East ? 0 : side == Down || side == Up ? 1 : 2;
        size_t first = normal == 0 ? 1 : 0;
        size_t second = normal == 2 ? 1 : 2;
        bool positive = side == East || side == Up || side == South;
        for (size_t layer = 0; layer < 2; ++layer) {
            std::array<std::array<bool, 2>, 2> open {};
            for (size_t a = 0; a < 2; ++a) {
                for (size_t b = 0; b < 2; ++b) {
                    std::array<size_t, 3> cell {};
                    cell[normal] = layer;
                    cell[first] = a;
                    cell[second] = b;
                    if (!occupied[cellIndex(cell[0], cell[1], cell[2])]) {
                        continue;
                    }
                    bool inside = positive ? layer == 0 : layer == 1;
                    std::array<size_t, 3> next = cell;
                    next[normal] = 1 - layer;
                    open[a][b] = !(inside && occupied[cellIndex(next[0], next[1], next[2])]);
                }
            }
            auto emit = [&](size_t a0, size_t a1, size_t b0, size_t b1) {
                Point min {};
                Point max {};
                min[normal] = static_cast<int16_t>(layer * 128);
                max[normal] = static_cast<int16_t>(min[normal] + 128);
                min[first] = static_cast<int16_t>(a0 * 128);
                max[first] = static_cast<int16_t>((a1 + 1) * 128);
                min[second] = static_cast<int16_t>(b0 * 128);
                max[second] = static_cast<int16_t>((b1 + 1) * 128);
                result.push_back(makeQuad(side, min, max, materials[side], true));
            };
            if (open[0][0] && open[0][1] && open[1][0] && open[1][1]) {
                emit(0, 1, 0, 1);
                continue;
            }
            std::array<std::array<bool, 2>, 2> done {};
            for (size_t a = 0; a < 2; ++a) {
                if (open[a][0] && open[a][1]) {
                    emit(a, a, 0, 1);
                    done[a] = { true, true };
                }
            }
            for (size_t b = 0; b < 2; ++b) {
                if (open[0][b] && open[1][b] && !done[0][b] && !done[1][b]) {
                    emit(0, 1, b, b);
                    done[0][b] = done[1][b] = true;
                }
            }
            for (size_t a = 0; a < 2; ++a) {
                for (size_t b = 0; b < 2; ++b) {
                    if (open[a][b] && !done[a][b]) {
                        emit(a, a, b, b);
                    }
                }
            }
        }
    }
    return result;
}

std::vector<ModelQuad> cross(uint32_t first, uint32_t second)
{
    std::array<std::array<uint16_t, 2>, 4> uvs { { { 0, 4096 }, { 4096, 4096 }, { 4096, 0 }, { 0, 0 } } };
    ModelQuad a;
    a.positions = { { { 0, 0, 0 }, { Full, 0, Full }, { Full, Full, Full }, { 0, Full, 0 } } };
    a.uvs = uvs;
    a.material = first;
    a.flags = QuadTwoSided;
    ModelQuad b;
    b.positions = { { { Full, 0, 0 }, { 0, 0, Full }, { 0, Full, Full }, { Full, Full, 0 } } };
    b.uvs = uvs;
    b.material = second;
    b.flags = QuadTwoSided;
    return { a, b };
}

std::vector<ModelQuad> fencePost(uint32_t material)
{
    Materials materials;
    materials.fill(material);
    auto faces = cuboid(materials, { 96, 0, 96 }, { 160, Full, 160 });
    return { faces.begin(), faces.end() };
}

std::vector<ModelQuad> fenceArms(uint32_t material, uint32_t mask)
{
    struct Arm {
        uint32_t bit;
        Point min;
        Point max;
    };
    static constexpr Arm Arms[] = {
        { 1, { 112, 0, 0 }, { 144, 0, 128 } },
        { 2, { 128, 0, 112 }, { Full, 0, 144 } },
        { 4, { 112, 0, 128 }, { 144, 0, Full } },
        { 8, { 0, 0, 112 }, { 128, 0, 144 } },
    };
    Materials materials;
    materials.fill(material);
    std::vector<ModelQuad> result;
    for (const Arm& arm : Arms) {
        if (!(mask & arm.bit)) {
            continue;
        }
        bool alongZ = arm.bit == 1 || arm.bit == 4;
        for (auto [minY, maxY] : { std::pair<int16_t, int16_t> { 96, 144 }, std::pair<int16_t, int16_t> { 192, 240 } }) {
            Point min = arm.min;
            Point max = arm.max;
            min[1] = minY;
            max[1] = maxY;
            auto faces = cuboid(materials, min, max);
            for (uint32_t side = 0; side < 6; ++side) {
                bool cap = alongZ ? (side == North || side == South) : (side == West || side == East);
                if (!cap) {
                    result.push_back(faces[side]);
                }
            }
        }
    }
    return result;
}

std::vector<ModelQuad> pane(uint32_t body, uint32_t edge, uint32_t mask)
{
    Materials center { body, body, edge, edge, body, body };
    std::vector<ModelQuad> result;
    auto centerFaces = cuboid(center, { 112, 0, 112 }, { 144, Full, 144 });
    for (uint32_t side = 0; side < 6; ++side) {
        uint32_t touchingArm = 0;
        switch (side) {
        case West:
            touchingArm = 8;
            break;
        case East:
            touchingArm = 2;
            break;
        case North:
            touchingArm = 1;
            break;
        case South:
            touchingArm = 4;
            break;
        default:
            break;
        }
        if (!(mask & touchingArm)) {
            result.push_back(centerFaces[side]);
        }
    }

    struct Arm {
        uint32_t bit;
        Point min;
        Point max;
        Materials materials;
        uint32_t hidden;
        uint32_t outward;
    };
    const Arm arms[] = {
        { 1, { 112, 0, 0 }, { 144, Full, 112 }, { body, body, edge, edge, edge, edge }, South, North },
        { 2, { 144, 0, 112 }, { Full, Full, 144 }, { edge, edge, edge, edge, body, body }, West, East },
        { 4, { 112, 0, 144 }, { 144, Full, Full }, { body, body, edge, edge, edge, edge }, North, South },
        { 8, { 0, 0, 112 }, { 112, Full, 144 }, { edge, edge, edge, edge, body, body }, East, West },
    };
    for (const Arm& arm : arms) {
        if (!(mask & arm.bit)) {
            continue;
        }
        auto faces = cuboid(arm.materials, arm.min, arm.max);
        for (uint32_t side = 0; side < 6; ++side) {
            if (side == arm.hidden) {
                continue;
            }
            ModelQuad quad = faces[side];
            if (side == arm.outward) {
                quad.flags |= (quad.flags & QuadFaceMask) << 4;
            }
            result.push_back(quad);
        }
    }
    return result;
}

std::vector<ModelQuad> wall(const Materials& materials, uint32_t connections)
{
    uint32_t north = connections & 3;
    uint32_t east = (connections >> 2) & 3;
    uint32_t south = (connections >> 4) & 3;
    uint32_t west = (connections >> 6) & 3;
    bool post = (connections >> 8) & 1;
    auto height = [](uint32_t connection) -> int16_t {
        return connection == 1 ? 224 : Full;
    };
    std::vector<ModelQuad> result;
    auto append = [&](Point min, Point max) {
        auto faces = cuboid(materials, min, max);
        result.insert(result.end(), faces.begin(), faces.end());
    };
    if (post) {
        append({ 64, 0, 64 }, { 192, Full, 192 });
    }
    if (north) {
        append({ 80, 0, 0 }, { 176, height(north), 128 });
    }
    if (east) {
        append({ 128, 0, 80 }, { Full, height(east), 176 });
    }
    if (south) {
        append({ 80, 0, 128 }, { 176, height(south), Full });
    }
    if (west) {
        append({ 0, 0, 80 }, { 128, height(west), 176 });
    }
    return result;
}

std::pair<Point, Point> doorBounds(uint32_t orientation, uint32_t open, uint32_t hinge)
{
    constexpr int16_t Thickness = 3 * 16;
    constexpr int16_t High = Full - Thickness;
    enum { North, South, West, East };
    static constexpr int Decoded[4] = { East, South, West, North };
    int facing = Decoded[orientation & 3];
    auto rotateRight = [](int f) {
        switch (f) {
        case North:
            return East;
        case East:
            return South;
        case South:
            return West;
        default:
            return North;
        }
    };
    auto rotateLeft = [](int f) {
        switch (f) {
        case North:
            return West;
        case West:
            return South;
        case South:
            return East;
        default:
            return North;
        }
    };
    int effective = !open ? facing : (hinge ? rotateLeft(facing) : rotateRight(facing));
    switch (effective) {
    case North:
        return { { 0, 0, High }, { Full, Full, Full } };
    case South:
        return { { 0, 0, 0 }, { Full, Full, Thickness } };
    case West:
        return { { High, 0, 0 }, { Full, Full, Full } };
    default:
        return { { 0, 0, 0 }, { Thickness, Full, Full } };
    }
}

std::pair<Point, Point> trapdoorBounds(uint32_t orientation, uint32_t open, uint32_t half)
{
    constexpr int16_t Thickness = 3 * 16;
    constexpr int16_t High = Full - Thickness;
    if (!open) {
        return half ? std::pair<Point, Point> { { 0, High, 0 }, { Full, Full, Full } } : std::pair<Point, Point> { { 0, 0, 0 }, { Full, Thickness, Full } };
    }
    switch (orientation & 3) {
    case 0:
        return { { 0, 0, 0 }, { Thickness, Full, Full } };
    case 1:
        return { { High, 0, 0 }, { Full, Full, Full } };
    case 2:
        return { { 0, 0, 0 }, { Full, Full, Thickness } };
    default:
        return { { 0, 0, High }, { Full, Full, Full } };
    }
}

std::vector<ModelQuad> gate(const Materials& materials, uint32_t orientation, bool open, bool inWall, bool bamboo)
{
    const GateElement* elements = bamboo ? (open ? BambooGateOpen : BambooGateClosed) : (open ? GateOpen : GateClosed);
    std::vector<ModelQuad> result;
    for (size_t e = 0; e < 8; ++e) {
        Point min = elements[e].min;
        Point max = elements[e].max;
        if (inWall) {
            min[1] = static_cast<int16_t>(min[1] - 48);
            max[1] = static_cast<int16_t>(max[1] - 48);
        }
        auto [rotatedMin, rotatedMax] = rotateGateBounds(min, max, orientation);
        for (uint32_t source = 0; source < 6; ++source) {
            const auto& uv = elements[e].faces[source];
            if (uv[4] == 0) {
                continue;
            }
            uint32_t target = rotateGateFace(source, orientation);
            ModelQuad quad = makeQuad(target, rotatedMin, rotatedMax, materials[target], false);
            rectUvs(quad, target, { uv[0], uv[1], uv[2], uv[3] });
            uint16_t rotation = static_cast<uint16_t>(uv[4] - 1);
            std::rotate(quad.uvs.begin(), quad.uvs.begin() + ((360 - rotation) / 90 % 4), quad.uvs.end());
            result.push_back(quad);
        }
    }
    return result;
}

std::vector<ModelQuad> button(const Materials& materials, uint32_t orientation, bool pressed)
{
    int16_t height = pressed ? 16 : 32;
    Point sourceMin { 80, 0, 96 };
    Point sourceMax { 176, height, 160 };
    auto [targetMin, targetMax] = buttonBounds(orientation, pressed);
    std::vector<ModelQuad> result;
    for (uint32_t source = 0; source < 6; ++source) {
        uint32_t target = buttonRotatedFace(source, orientation);
        if (orientation <= 1) {
            ModelQuad quad = makeQuad(source, sourceMin, sourceMax, materials[target], false);
            rectUvs(quad, source, buttonFaceUv(source, pressed));
            for (Point& position : quad.positions) {
                position = buttonRotatePosition(position, orientation);
            }
            quad.flags = faceId(target);
            result.push_back(quad);
        } else {
            ModelQuad quad = makeQuad(target, targetMin, targetMax, materials[target], false);
            rectUvs(quad, target, uvLockRect(target, targetMin, targetMax));
            result.push_back(quad);
        }
    }
    return result;
}

std::vector<ModelQuad> pressurePlate(const Materials& materials, bool pressed)
{
    int16_t maxY = pressed ? 8 : 16;
    auto faces = cuboid(materials, { 16, 0, 16 }, { 240, maxY, 240 });
    if (pressed) {
        for (uint32_t side : { West, East, North, South }) {
            for (auto& uv : faces[side].uvs) {
                uv[1] = static_cast<uint16_t>(uv[1] - 128);
            }
        }
    }
    return { faces.begin(), faces.end() };
}

std::vector<ModelQuad> torch(uint32_t material, uint32_t facing)
{
    Materials materials;
    materials.fill(material);
    Point min { 112, 0, 112 };
    Point max { 144, 160, 144 };
    if (facing != 0) {
        int16_t shift = 5 * 16;
        min[1] = 48;
        max[1] = 208;
        switch (facing) {
        case 1:
            min[0] = static_cast<int16_t>(min[0] + shift);
            max[0] = static_cast<int16_t>(max[0] + shift);
            break;
        case 2:
            min[0] = static_cast<int16_t>(min[0] - shift);
            max[0] = static_cast<int16_t>(max[0] - shift);
            break;
        case 3:
            min[2] = static_cast<int16_t>(min[2] + shift);
            max[2] = static_cast<int16_t>(max[2] + shift);
            break;
        default:
            min[2] = static_cast<int16_t>(min[2] - shift);
            max[2] = static_cast<int16_t>(max[2] - shift);
            break;
        }
    }
    auto faces = cuboid(materials, min, max);
    std::vector<ModelQuad> result;
    for (uint32_t side = 0; side < 6; ++side) {
        ModelQuad quad = faces[side];
        if (side == Up || side == Down) {
            rectUvs(quad, side, { 7, 6, 9, 8 });
        } else {
            rectUvs(quad, side, { 7, 6, 9, 16 });
        }
        result.push_back(quad);
    }
    return result;
}

std::vector<ModelQuad> lantern(uint32_t material, bool hanging)
{
    Materials materials;
    materials.fill(material);
    int16_t lift = hanging ? 16 : 0;
    std::vector<ModelQuad> result;
    auto body = cuboid(materials, { 80, lift, 80 }, { 176, static_cast<int16_t>(112 + lift), 176 });
    for (uint32_t side = 0; side < 6; ++side) {
        rectUvs(body[side], side, side == Up || side == Down ? std::array<uint16_t, 4> { 0, 9, 6, 15 } : std::array<uint16_t, 4> { 0, 2, 6, 9 });
        result.push_back(body[side]);
    }
    auto cap = cuboid(materials, { 96, static_cast<int16_t>(112 + lift), 96 }, { 160, static_cast<int16_t>(144 + lift), 160 });
    for (uint32_t side = 0; side < 6; ++side) {
        if (side == Down && !hanging) {
            continue;
        }
        rectUvs(cap[side], side, side == Up || side == Down ? std::array<uint16_t, 4> { 1, 10, 5, 14 } : std::array<uint16_t, 4> { 1, 0, 5, 2 });
        result.push_back(cap[side]);
    }

    // The handle is two 3px wide planes crossed at 45 degrees, so each end sits about 17 units off center on both axes.
    constexpr int16_t Near = 128 - 17;
    constexpr int16_t Far = 128 + 17;
    auto handle = [&](int16_t x0, int16_t z0, int16_t x1, int16_t z1, int16_t bottom, int16_t top, std::array<uint16_t, 4> rect) {
        result.push_back(uprightPlane(material, x0, z0, x1, z1, bottom, top, rect));
    };
    if (hanging) {
        handle(Near, Near, Far, Far, 176, 240, { 11, 1, 14, 5 });
        handle(Near, Far, Far, Near, 160, Full, { 11, 6, 14, 12 });
    } else {
        handle(Near, Near, Far, Far, 144, 176, { 11, 1, 14, 3 });
        handle(Near, Far, Far, Near, 144, 176, { 11, 10, 14, 12 });
    }
    return result;
}

std::vector<ModelQuad> candles(uint32_t material, uint32_t count)
{
    struct Candle {
        int16_t x;
        int16_t z;
        int16_t height;
    };
    static constexpr Candle Layouts[4][4] = {
        { { 7, 7, 6 } },
        { { 5, 7, 6 }, { 9, 6, 5 } },
        { { 7, 9, 6 }, { 5, 7, 5 }, { 8, 6, 4 } },
        { { 5, 8, 6 }, { 8, 8, 5 }, { 5, 5, 4 }, { 8, 5, 3 } },
    };
    // Each wick is two 1px planes crossed at 45 degrees, so its ends sit about 6 units off center.
    constexpr int16_t Reach = 6;
    uint32_t candleCount = std::clamp<uint32_t>(count, 1, 4);
    std::vector<ModelQuad> result;
    for (uint32_t index = 0; index < candleCount; ++index) {
        const Candle& candle = Layouts[candleCount - 1][index];
        int16_t top = static_cast<int16_t>(candle.height);
        pushTexturedBox(result, material, { candle.x, 0, candle.z }, { int16_t(candle.x + 2), top, int16_t(candle.z + 2) }, { 0, 6, 2, 8 }, { 0, 8, 2, static_cast<uint16_t>(8 + candle.height) });
        int16_t centerX = static_cast<int16_t>((candle.x + 1) * 16);
        int16_t centerZ = static_cast<int16_t>((candle.z + 1) * 16);
        int16_t bottom = static_cast<int16_t>(top * 16);
        int16_t tip = static_cast<int16_t>(bottom + 16);
        result.push_back(uprightPlane(material, centerX - Reach, centerZ - Reach, centerX + Reach, centerZ + Reach, bottom, tip, { 0, 5, 1, 6 }));
        result.push_back(uprightPlane(material, centerX - Reach, centerZ + Reach, centerX + Reach, centerZ - Reach, bottom, tip, { 0, 5, 1, 6 }));
    }
    return result;
}

std::vector<ModelQuad> turtleEggs(uint32_t material, uint32_t count)
{
    struct Egg {
        std::array<int16_t, 3> min;
        std::array<int16_t, 3> max;
        std::array<uint16_t, 4> ends;
        std::array<uint16_t, 4> sides;
    };
    static constexpr Egg Eggs[4] = {
        { { 5, 0, 4 }, { 9, 7, 8 }, { 0, 0, 4, 4 }, { 1, 4, 5, 11 } },
        { { 1, 0, 7 }, { 5, 5, 11 }, { 6, 7, 10, 11 }, { 10, 10, 14, 15 } },
        { { 5, 0, 11 }, { 8, 4, 14 }, { 5, 0, 8, 3 }, { 8, 3, 11, 7 } },
        { { 10, 0, 10 }, { 14, 4, 14 }, { 0, 11, 4, 15 }, { 4, 11, 8, 15 } },
    };
    std::vector<ModelQuad> result;
    for (uint32_t index = 0; index < std::clamp<uint32_t>(count, 1, 4); ++index) {
        pushTexturedBox(result, material, Eggs[index].min, Eggs[index].max, Eggs[index].ends, Eggs[index].sides);
    }
    return result;
}

std::vector<ModelQuad> cauldron(const Materials& materials, uint32_t liquid, uint32_t level)
{
    uint32_t side = materials[North];
    uint32_t inner = materials[South];
    uint32_t top = materials[Up];
    uint32_t bottom = materials[Down];
    std::vector<ModelQuad> result;
    auto box = [&](Point min, Point max, uint32_t up, uint32_t facingIn) {
        Materials faces;
        for (uint32_t face = 0; face < 6; ++face) {
            faces[face] = face == Up ? up : face == Down ? bottom : touchesBoundary(face, min, max) ? side : facingIn;
        }
        auto quads = cuboid(faces, min, max);
        result.insert(result.end(), quads.begin(), quads.end());
    };
    static constexpr std::array<int16_t, 4> Legs[8] = {
        { 0, 0, 64, 32 }, { 0, 32, 32, 64 }, { 192, 0, 256, 32 }, { 224, 32, 256, 64 },
        { 0, 224, 64, 256 }, { 0, 192, 32, 224 }, { 192, 224, 256, 256 }, { 224, 192, 256, 224 },
    };
    for (const auto& [x0, z0, x1, z1] : Legs) {
        box({ x0, 0, z0 }, { x1, 48, z1 }, side, side);
    }
    box({ 32, 48, 32 }, { 224, 64, 224 }, inner, inner);
    box({ 0, 48, 0 }, { 32, Full, Full }, top, inner);
    box({ 224, 48, 0 }, { Full, Full, Full }, top, inner);
    box({ 32, 48, 0 }, { 224, Full, 32 }, top, inner);
    box({ 32, 48, 224 }, { 224, Full, Full }, top, inner);
    if (level > 0) {
        // Six fill levels climb from 6px to 15px, a bottle being two of them.
        auto height = static_cast<int16_t>(96 + std::min<uint32_t>(level, 6) * 24);
        result.push_back(makeQuad(Up, { 32, height, 32 }, { 224, height, 224 }, liquid, false));
    }
    return result;
}

namespace {

uint32_t sideFromFaceId(uint32_t id)
{
    static constexpr uint32_t Sides[7] = { 0, Down, Up, West, East, North, South };
    return Sides[std::min<uint32_t>(id, 6)];
}

void appendCuboid(std::vector<ModelQuad>& out, uint32_t material, Point min, Point max)
{
    Materials materials;
    materials.fill(material);
    auto faces = cuboid(materials, min, max);
    out.insert(out.end(), faces.begin(), faces.end());
}

std::vector<ModelQuad> rotateSign(std::vector<ModelQuad> quads, uint32_t rotation)
{
    static constexpr std::pair<int32_t, int32_t> Trig[16] = {
        { 1024, 0 }, { 946, 392 }, { 724, 724 }, { 392, 946 }, { 0, 1024 }, { -392, 946 }, { -724, 724 }, { -946, 392 },
        { -1024, 0 }, { -946, -392 }, { -724, -724 }, { -392, -946 }, { 0, -1024 }, { 392, -946 }, { 724, -724 }, { 946, -392 },
    };
    auto [cosine, sine] = Trig[rotation & 15];
    auto rounded = [](int32_t value) {
        return value < 0 ? (value - 512) / 1024 : (value + 512) / 1024;
    };
    for (ModelQuad& quad : quads) {
        for (Point& position : quad.positions) {
            int32_t dx = int32_t(position[0]) - 128;
            int32_t dz = int32_t(position[2]) - 128;
            position[0] = static_cast<int16_t>(128 + rounded(dx * cosine - dz * sine));
            position[2] = static_cast<int16_t>(128 + rounded(dx * sine + dz * cosine));
        }
        uint32_t face = quad.flags & QuadFaceMask;
        uint32_t cull = (quad.flags & QuadCullFaceMask) >> 4;
        quad.flags &= ~uint32_t(QuadFaceMask | QuadCullFaceMask);
        auto turned = [&](uint32_t id) -> uint32_t {
            if (id == 1 || id == 2) {
                return id;
            }
            return (rotation & 15) % 4 == 0 && id != 0 ? faceId(rotateGateFace(sideFromFaceId(id), (rotation & 15) / 4)) : 0;
        };
        // The side a face is culled against turns with it, or it would hide behind the wrong neighbour.
        quad.flags |= turned(face) | (turned(cull) << 4);
    }
    return quads;
}

}

std::vector<ModelQuad> shape(const std::vector<ShapePart>& parts, std::vector<ModelQuad> extra, uint32_t turns)
{
    std::vector<ModelQuad> quads;
    for (const ShapePart& part : parts) {
        auto faces = cuboid(part.materials, part.min, part.max);
        for (uint32_t side = 0; side < 6; ++side) {
            if (part.hidden & (1u << side)) {
                continue;
            }
            if (part.uvs) {
                rectUvs(faces[side], side, (*part.uvs)[side]);
            }
            quads.push_back(faces[side]);
        }
    }
    quads.insert(quads.end(), extra.begin(), extra.end());
    return rotateSign(std::move(quads), (turns & 3) * 4);
}

std::vector<ModelQuad> orientedCross(uint32_t material, uint32_t facing)
{
    return orientedCross(material, material, facing);
}

std::vector<ModelQuad> orientedCross(uint32_t first, uint32_t second, uint32_t facing)
{
    return orient(cross(first, second), facing);
}

std::vector<ModelQuad> orient(std::vector<ModelQuad> quads, uint32_t facing)
{
    // Where a point centered on the block goes once up points toward facing.
    auto turn = [facing](int32_t cx, int32_t cy, int32_t cz) {
        int32_t x = cx;
        int32_t y = cy;
        int32_t z = cz;
        switch (facing) {
        case Down:
            y = -cy;
            z = -cz;
            break;
        case North:
            y = cz;
            z = -cy;
            break;
        case South:
            y = -cz;
            z = cy;
            break;
        case West:
            x = -cy;
            y = cx;
            break;
        case East:
            x = cy;
            y = -cx;
            break;
        default:
            break;
        }
        return std::array<int32_t, 3> { x, y, z };
    };
    static constexpr std::array<std::array<int32_t, 3>, 6> Normals { { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } } };
    for (ModelQuad& quad : quads) {
        for (Point& position : quad.positions) {
            auto turned = turn(int32_t(position[0]) - 128, int32_t(position[1]) - 128, int32_t(position[2]) - 128);
            position = { static_cast<int16_t>(turned[0] + 128), static_cast<int16_t>(turned[1] + 128), static_cast<int16_t>(turned[2] + 128) };
        }
        uint32_t id = quad.flags & QuadFaceMask;
        auto side = std::find(std::begin(FaceIds), std::end(FaceIds), id);
        if (side == std::end(FaceIds)) {
            continue;
        }
        const auto& normal = Normals[size_t(side - std::begin(FaceIds))];
        auto turned = turn(normal[0], normal[1], normal[2]);
        uint32_t newSide = uint32_t(std::find(Normals.begin(), Normals.end(), turned) - Normals.begin());
        size_t axis = newSide / 2;
        int16_t plane = (newSide & 1) ? Full : 0;
        bool touches = std::all_of(quad.positions.begin(), quad.positions.end(), [&](const Point& p) { return p[axis] == plane; });
        bool culled = (quad.flags & QuadCullFaceMask) != 0;
        quad.flags = (quad.flags & ~uint32_t(QuadFaceMask | QuadCullFaceMask)) | (culled ? boundary(faceId(newSide), touches) : faceId(newSide));
    }
    return quads;
}

std::vector<ModelQuad> bamboo(uint32_t stem, uint32_t leaves, bool thick)
{
    int16_t size = thick ? 48 : 32;
    int16_t low = static_cast<int16_t>((Full - size) / 2);
    int16_t high = static_cast<int16_t>(low + size);
    uint16_t width = static_cast<uint16_t>(thick ? 3 : 2);
    std::vector<ModelQuad> quads;
    Materials materials;
    materials.fill(stem);
    auto faces = cuboid(materials, { low, 0, low }, { high, Full, high });
    for (uint32_t side = 0; side < 6; ++side) {
        ModelQuad quad = faces[side];
        if (side == Up || side == Down) {
            rectUvs(quad, side, { static_cast<uint16_t>(16 - width), 0, 16, width });
        } else {
            rectUvs(quad, side, { 0, 0, width, 16 });
        }
        quads.push_back(quad);
    }
    if (leaves != DiagnosticMaterial) {
        std::vector<ModelQuad> crossed = cross(leaves, leaves);
        quads.insert(quads.end(), crossed.begin(), crossed.end());
    }
    return quads;
}

std::vector<ModelQuad> standingSign(uint32_t material, uint32_t rotation)
{
    std::vector<ModelQuad> quads;
    appendCuboid(quads, material, { 0, 112, 120 }, { Full, 240, 136 });
    appendCuboid(quads, material, { 120, 0, 120 }, { 136, 112, 136 });
    return rotateSign(std::move(quads), rotation);
}

std::vector<ModelQuad> wallSign(uint32_t material, uint32_t facing)
{
    static constexpr Point Bounds[6][2] = {
        { { 0, 240, 0 }, { Full, Full, Full } },
        { { 0, 0, 0 }, { Full, 16, Full } },
        { { 0, 72, 240 }, { Full, 200, Full } },
        { { 0, 72, 0 }, { Full, 200, 16 } },
        { { 240, 72, 0 }, { Full, 200, Full } },
        { { 0, 72, 0 }, { 16, 200, Full } },
    };
    std::vector<ModelQuad> quads;
    const Point* bounds = Bounds[std::min<uint32_t>(facing, 5)];
    appendCuboid(quads, material, bounds[0], bounds[1]);
    return quads;
}

std::vector<ModelQuad> hangingWallSign(uint32_t material, uint32_t facing)
{
    static constexpr Point Parts[6][4] = {
        { { 16, 120, 48 }, { 240, 136, 176 }, { 96, 128, 176 }, { 160, Full, Full } },
        { { 16, 120, 80 }, { 240, 136, 208 }, { 96, 0, 0 }, { 160, 128, 80 } },
        { { 16, 48, 120 }, { 240, 176, 136 }, { 96, 224, 128 }, { 160, Full, Full } },
        { { 16, 48, 120 }, { 240, 176, 136 }, { 96, 224, 0 }, { 160, Full, 128 } },
        { { 120, 48, 16 }, { 136, 176, 240 }, { 128, 224, 96 }, { Full, Full, 160 } },
        { { 120, 48, 16 }, { 136, 176, 240 }, { 0, 224, 96 }, { 128, Full, 160 } },
    };
    const Point* parts = Parts[std::min<uint32_t>(facing, 5)];
    std::vector<ModelQuad> quads;
    appendCuboid(quads, material, parts[0], parts[1]);
    appendCuboid(quads, material, parts[2], parts[3]);
    return quads;
}

std::vector<ModelQuad> hangingCeilingSign(uint32_t material, uint32_t rotation, bool attached)
{
    std::vector<ModelQuad> quads;
    appendCuboid(quads, material, { 16, 48, 120 }, { 240, 176, 136 });
    if (attached) {
        appendCuboid(quads, material, { 48, 176, 120 }, { 64, Full, 136 });
        appendCuboid(quads, material, { 192, 176, 120 }, { 208, Full, 136 });
    } else {
        appendCuboid(quads, material, { 32, 176, 120 }, { 48, Full, 136 });
        appendCuboid(quads, material, { 64, 176, 120 }, { 80, Full, 136 });
        appendCuboid(quads, material, { 176, 176, 120 }, { 192, Full, 136 });
        appendCuboid(quads, material, { 208, 176, 120 }, { 224, Full, 136 });
    }
    return rotateSign(std::move(quads), rotation);
}

std::vector<ModelQuad> flatPlane(uint32_t material, int16_t height)
{
    ModelQuad quad;
    quad.positions = { { { 0, height, 0 }, { 0, height, Full }, { Full, height, Full }, { Full, height, 0 } } };
    quad.uvs = { { { 0, 0 }, { 0, 4096 }, { 4096, 4096 }, { 4096, 0 } } };
    quad.material = material;
    quad.flags = faceId(Up) | QuadTwoSided;
    return { quad };
}

std::vector<ModelQuad> attachedPlanes(uint32_t material, uint32_t sides)
{
    constexpr int16_t Inset = 1;
    constexpr int16_t Far = Full - Inset;
    std::vector<ModelQuad> result;
    for (uint32_t side = 0; side < 6; ++side) {
        if (!(sides & (1u << side))) {
            continue;
        }
        Point min { 0, 0, 0 };
        Point max { Full, Full, Full };
        switch (side) {
        case West:
            min[0] = max[0] = Inset;
            break;
        case East:
            min[0] = max[0] = Far;
            break;
        case Down:
            min[1] = max[1] = Inset;
            break;
        case Up:
            min[1] = max[1] = Far;
            break;
        case North:
            min[2] = max[2] = Inset;
            break;
        default:
            min[2] = max[2] = Far;
            break;
        }
        ModelQuad quad = makeQuad(side, min, max, material, false);
        quad.flags |= QuadTwoSided;
        result.push_back(quad);
    }
    return result;
}

}
