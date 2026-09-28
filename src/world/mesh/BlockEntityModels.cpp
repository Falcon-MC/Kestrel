#include "world/BlockEntityModels.h"

#include <algorithm>
#include <cmath>

namespace kestrel::world {

namespace {

enum Side {
    West,
    East,
    Down,
    Up,
    North,
    South,
};

/**
 * The standard box unwrap for a box w wide, h tall and d deep whose texture
 * starts at (u, v): the front is the south face of a model facing south.
 */
std::array<EntityFace, 6> boxUv(float u, float v, float w, float h, float d)
{
    std::array<EntityFace, 6> faces {};
    faces[West] = { u, v + d, d, h };
    faces[South] = { u + d, v + d, w, h };
    faces[East] = { u + d + w, v + d, d, h };
    faces[North] = { u + d + w + d, v + d, w, h };
    faces[Up] = { u + d, v, w, d };
    faces[Down] = { u + d + w, v, w, d };
    return faces;
}

EntityBox box(std::array<float, 3> min, std::array<float, 3> max, std::array<EntityFace, 6> faces)
{
    return { min, max, faces };
}

void setSides(std::array<EntityFace, 6>& faces, bool flipV)
{
    for (int side : { West, East, North, South }) {
        faces[side].flipV = flipV;
    }
}

void setTinted(std::array<EntityFace, 6>& faces)
{
    for (EntityFace& face : faces) {
        face.tinted = true;
    }
}

}

std::vector<EntityBox> chestBodyBoxes(bool twoBlocks, float offsetX)
{
    float width = twoBlocks ? 30.0f : 14.0f;
    std::array<EntityFace, 6> body = boxUv(0.0f, 19.0f, width, 10.0f, 14.0f);
    return { box({ offsetX + 1.0f, 0.0f, 1.0f }, { offsetX + 1.0f + width, 10.0f, 15.0f }, body) };
}

std::vector<EntityBox> chestLidBoxes(bool twoBlocks, float offsetX)
{
    float width = twoBlocks ? 30.0f : 14.0f;
    std::array<EntityFace, 6> lid = boxUv(0.0f, 0.0f, width, 5.0f, 14.0f);
    setSides(lid, true);
    float latchX = offsetX + 1.0f + width * 0.5f - 1.0f;
    return {
        box({ offsetX + 1.0f, 9.0f, 1.0f }, { offsetX + 1.0f + width, 14.0f, 15.0f }, lid),
        box({ latchX, 7.0f, 15.0f }, { latchX + 2.0f, 11.0f, 16.0f }, boxUv(0.0f, 0.0f, 2.0f, 4.0f, 1.0f)),
    };
}

std::vector<EntityBox> chestBoxes(bool twoBlocks, float offsetX)
{
    std::vector<EntityBox> boxes = chestBodyBoxes(twoBlocks, offsetX);
    std::vector<EntityBox> lid = chestLidBoxes(twoBlocks, offsetX);
    boxes.insert(boxes.end(), lid.begin(), lid.end());
    return boxes;
}

std::vector<EntityBox> bedBoxes(bool head)
{
    float row = head ? 6.0f : 22.0f;
    std::array<EntityFace, 6> frame {};
    frame[Up] = { 6.0f, row, 16.0f, 16.0f, 2 };
    frame[Down] = { 28.0f, row, 16.0f, 16.0f, 2 };
    frame[East] = { 0.0f, row, 6.0f, 16.0f, 3 };
    frame[West] = { 22.0f, row, 6.0f, 16.0f, 1 };
    frame[South] = { 6.0f, 0.0f, 16.0f, 6.0f };
    frame[North] = { 22.0f, 0.0f, 16.0f, 6.0f };
    frame[head ? North : South].present = false;

    std::vector<EntityBox> boxes { box({ 0.0f, 3.0f, 0.0f }, { 16.0f, 9.0f, 16.0f }, frame) };
    float legZ = head ? 13.0f : 0.0f;
    float legV = head ? 38.0f : 44.0f;
    boxes.push_back(box({ 0.0f, 0.0f, legZ }, { 3.0f, 3.0f, legZ + 3.0f }, boxUv(0.0f, legV, 3.0f, 3.0f, 3.0f)));
    boxes.push_back(box({ 13.0f, 0.0f, legZ }, { 16.0f, 3.0f, legZ + 3.0f }, boxUv(12.0f, legV, 3.0f, 3.0f, 3.0f)));
    return boxes;
}

std::vector<EntityBox> skullBoxes(bool wall)
{
    std::array<EntityFace, 6> head = boxUv(0.0f, 0.0f, 8.0f, 8.0f, 8.0f);
    if (wall) {
        return { box({ 4.0f, 4.0f, 0.0f }, { 12.0f, 12.0f, 8.0f }, head) };
    }
    return { box({ 4.0f, 0.0f, 4.0f }, { 12.0f, 8.0f, 12.0f }, head) };
}

std::vector<EntityBox> piglinHeadBoxes(bool wall)
{
    float lift = wall ? 4.0f : 0.0f;
    float back = wall ? 0.0f : 4.0f;
    return {
        box({ 3.0f, lift, back }, { 13.0f, lift + 8.0f, back + 8.0f }, boxUv(0.0f, 0.0f, 10.0f, 8.0f, 8.0f)),
        box({ 6.0f, lift, back + 8.0f }, { 10.0f, lift + 4.0f, back + 9.0f }, boxUv(31.0f, 1.0f, 4.0f, 4.0f, 1.0f)),
    };
}

/**
 * The dragon head is the dragon's own head at three quarters scale, so its
 * snout reaches past the block the way it does in game.
 */
std::vector<EntityBox> dragonHeadBoxes(bool wall)
{
    float lift = wall ? 4.0f : 0.0f;
    float back = wall ? 0.0f : 2.0f;
    return {
        box({ 2.0f, lift, back }, { 14.0f, lift + 12.0f, back + 12.0f }, boxUv(112.0f, 30.0f, 16.0f, 16.0f, 16.0f)),
        box({ 3.5f, lift + 3.0f, back + 10.5f }, { 12.5f, lift + 6.75f, back + 22.5f }, boxUv(176.0f, 44.0f, 12.0f, 5.0f, 16.0f)),
        box({ 3.5f, lift, back + 10.5f }, { 12.5f, lift + 3.0f, back + 22.5f }, boxUv(176.0f, 65.0f, 12.0f, 4.0f, 16.0f)),
    };
}

/**
 * A closed shulker box: the base sits a hair inside the lid so the lid wins
 * where they overlap.
 */
std::vector<EntityBox> shulkerBoxBoxes()
{
    return {
        box({ 0.0f, 4.0f, 0.0f }, { 16.0f, 16.0f, 16.0f }, boxUv(0.0f, 0.0f, 16.0f, 12.0f, 16.0f)),
        box({ 0.1f, 0.0f, 0.1f }, { 15.9f, 8.0f, 15.9f }, boxUv(0.0f, 28.0f, 16.0f, 8.0f, 16.0f)),
    };
}

std::vector<EntityBox> bannerBoxes(bool wall)
{
    std::array<EntityFace, 6> flag = boxUv(0.0f, 0.0f, 20.0f, 40.0f, 1.0f);
    setTinted(flag);
    std::array<EntityFace, 6> bar = boxUv(0.0f, 42.0f, 20.0f, 2.0f, 2.0f);
    if (wall) {
        return {
            box({ -2.0f, 14.0f, 0.0f }, { 18.0f, 16.0f, 2.0f }, bar),
            box({ -2.0f, -26.0f, 2.0f }, { 18.0f, 14.0f, 3.0f }, flag),
        };
    }
    return {
        box({ 7.0f, 0.0f, 7.0f }, { 9.0f, 42.0f, 9.0f }, boxUv(44.0f, 0.0f, 2.0f, 42.0f, 2.0f)),
        box({ -2.0f, 42.0f, 8.0f }, { 18.0f, 44.0f, 10.0f }, bar),
        box({ -2.0f, 2.0f, 10.0f }, { 18.0f, 42.0f, 11.0f }, flag),
    };
}

std::vector<uint8_t> sliceEntityFace(const EntityImage& image, const EntityFace& face, uint32_t tint)
{
    constexpr uint32_t Size = TextureSize;
    std::vector<uint8_t> sampled(size_t(Size) * Size * 4, 0);
    for (uint32_t j = 0; j < Size; ++j) {
        for (uint32_t i = 0; i < Size; ++i) {
            float sx = face.u + (static_cast<float>(i) + 0.5f) * face.w / Size;
            float sy = face.v + (static_cast<float>(j) + 0.5f) * face.h / Size;
            int32_t px = std::clamp(static_cast<int32_t>(std::floor(sx)), 0, int32_t(image.width) - 1);
            int32_t py = std::clamp(static_cast<int32_t>(std::floor(sy)), 0, int32_t(image.height) - 1);
            const uint8_t* source = image.rgba.data() + (size_t(py) * image.width + size_t(px)) * 4;
            uint32_t ti = face.flipU ? Size - 1 - i : i;
            uint32_t tj = face.flipV ? Size - 1 - j : j;
            std::copy(source, source + 4, sampled.data() + (size_t(tj) * Size + ti) * 4);
        }
    }
    for (uint8_t turn = 0; turn < (face.turns & 3); ++turn) {
        std::vector<uint8_t> rotated(sampled.size());
        for (uint32_t j = 0; j < Size; ++j) {
            for (uint32_t i = 0; i < Size; ++i) {
                const uint8_t* source = sampled.data() + (size_t(Size - 1 - i) * Size + j) * 4;
                std::copy(source, source + 4, rotated.data() + (size_t(j) * Size + i) * 4);
            }
        }
        sampled = std::move(rotated);
    }
    if (face.tinted && tint) {
        for (size_t pixel = 0; pixel < sampled.size(); pixel += 4) {
            sampled[pixel] = static_cast<uint8_t>(sampled[pixel] * ((tint >> 16) & 0xFF) / 255);
            sampled[pixel + 1] = static_cast<uint8_t>(sampled[pixel + 1] * ((tint >> 8) & 0xFF) / 255);
            sampled[pixel + 2] = static_cast<uint8_t>(sampled[pixel + 2] * (tint & 0xFF) / 255);
        }
    }
    return sampled;
}

std::vector<ModelQuad> buildEntityQuads(const std::vector<EntityBox>& boxes, float yawDegrees, const std::function<uint32_t(const EntityFace&)>& material)
{
    static constexpr float Normals[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    static constexpr float Rights[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { 1, 0, 0 }, { -1, 0, 0 }, { 1, 0, 0 } };
    static constexpr float Ups[6][3] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, 1, 0 }, { 0, 1, 0 } };
    static constexpr uint32_t FaceIds[6] = { 3, 4, 1, 2, 5, 6 };
    float radians = yawDegrees * 3.14159265f / 180.0f;
    float cosine = std::cos(radians);
    float sine = std::sin(radians);
    auto turn = [&](float x, float z) {
        float dx = x - 8.0f;
        float dz = z - 8.0f;
        return std::array<float, 2> { 8.0f + dx * cosine - dz * sine, 8.0f + dx * sine + dz * cosine };
    };

    std::vector<ModelQuad> quads;
    for (const EntityBox& entityBox : boxes) {
        std::array<float, 3> center {};
        std::array<float, 3> half {};
        for (int axis = 0; axis < 3; ++axis) {
            center[axis] = (entityBox.min[axis] + entityBox.max[axis]) * 0.5f;
            half[axis] = (entityBox.max[axis] - entityBox.min[axis]) * 0.5f;
        }
        for (int side = 0; side < 6; ++side) {
            const EntityFace& face = entityBox.faces[side];
            if (!face.present) {
                continue;
            }
            float halfNormal = 0.0f;
            float halfRight = 0.0f;
            float halfUp = 0.0f;
            for (int axis = 0; axis < 3; ++axis) {
                halfNormal += std::abs(Normals[side][axis]) * half[axis];
                halfRight += std::abs(Rights[side][axis]) * half[axis];
                halfUp += std::abs(Ups[side][axis]) * half[axis];
            }
            static constexpr float CornerSigns[4][2] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } };
            static constexpr uint16_t CornerUvs[4][2] = { { 0, 0 }, { 4096, 0 }, { 4096, 4096 }, { 0, 4096 } };
            ModelQuad quad;
            for (size_t corner = 0; corner < 4; ++corner) {
                std::array<float, 3> point {};
                for (int axis = 0; axis < 3; ++axis) {
                    point[axis] = center[axis] + Normals[side][axis] * halfNormal + Rights[side][axis] * halfRight * CornerSigns[corner][0] + Ups[side][axis] * halfUp * CornerSigns[corner][1];
                }
                std::array<float, 2> turned = turn(point[0], point[2]);
                quad.positions[corner] = {
                    static_cast<int16_t>(std::lround(turned[0] * 16.0f)),
                    static_cast<int16_t>(std::lround(point[1] * 16.0f)),
                    static_cast<int16_t>(std::lround(turned[1] * 16.0f)),
                };
                quad.uvs[corner] = { CornerUvs[corner][0], CornerUvs[corner][1] };
            }
            float nx = Normals[side][0] * cosine - Normals[side][2] * sine;
            float nz = Normals[side][0] * sine + Normals[side][2] * cosine;
            uint32_t faceId = FaceIds[side];
            if (side != Up && side != Down) {
                faceId = std::abs(nx) > std::abs(nz) ? (nx < 0.0f ? 3u : 4u) : (nz < 0.0f ? 5u : 6u);
            }
            quad.material = material(face);
            quad.flags = faceId | QuadTwoSided;
            quads.push_back(quad);
        }
    }
    return quads;
}

}
