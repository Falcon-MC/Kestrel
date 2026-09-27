#include "world/BlockModels.h"

#include <algorithm>

namespace kestrel::world::models {

namespace {

constexpr int16_t Full = 256;

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
    static constexpr uint32_t Ids[6] = { 3, 4, 1, 2, 5, 6 };
    return Ids[side];
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

    std::vector<ModelQuad> result;
    for (size_t x = 0; x < 2; ++x) {
        for (size_t y = 0; y < 2; ++y) {
            for (size_t z = 0; z < 2; ++z) {
                if (!occupied[cellIndex(x, y, z)]) {
                    continue;
                }
                Point min { static_cast<int16_t>(x * 128), static_cast<int16_t>(y * 128), static_cast<int16_t>(z * 128) };
                Point max { static_cast<int16_t>(min[0] + 128), static_cast<int16_t>(min[1] + 128), static_cast<int16_t>(min[2] + 128) };
                for (uint32_t side = 0; side < 6; ++side) {
                    bool neighbourOccupied = false;
                    switch (side) {
                    case West:
                        neighbourOccupied = x > 0 && occupied[cellIndex(x - 1, y, z)];
                        break;
                    case East:
                        neighbourOccupied = x < 1 && occupied[cellIndex(x + 1, y, z)];
                        break;
                    case Down:
                        neighbourOccupied = y > 0 && occupied[cellIndex(x, y - 1, z)];
                        break;
                    case Up:
                        neighbourOccupied = y < 1 && occupied[cellIndex(x, y + 1, z)];
                        break;
                    case North:
                        neighbourOccupied = z > 0 && occupied[cellIndex(x, y, z - 1)];
                        break;
                    default:
                        neighbourOccupied = z < 1 && occupied[cellIndex(x, y, z + 1)];
                        break;
                    }
                    if (!neighbourOccupied) {
                        result.push_back(makeQuad(side, min, max, materials[side], true));
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
        quad.flags &= ~QuadFaceMask;
        if (face == 1 || face == 2) {
            quad.flags |= face;
        } else if ((rotation & 15) % 4 == 0 && face != 0) {
            quad.flags |= faceId(rotateGateFace(sideFromFaceId(face), (rotation & 15) / 4));
        }
    }
    return quads;
}

}

std::vector<ModelQuad> orientedCross(uint32_t material, uint32_t facing)
{
    std::vector<ModelQuad> quads = cross(material, material);
    for (ModelQuad& quad : quads) {
        for (Point& position : quad.positions) {
            int32_t cx = int32_t(position[0]) - 128;
            int32_t cy = int32_t(position[1]) - 128;
            int32_t cz = int32_t(position[2]) - 128;
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
            position = { static_cast<int16_t>(x + 128), static_cast<int16_t>(y + 128), static_cast<int16_t>(z + 128) };
        }
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
    constexpr int16_t Inset = 13;
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
