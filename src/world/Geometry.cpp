#include "world/Geometry.h"

#include "Core/Json/Json.h"

#include <algorithm>
#include <cmath>

namespace kestrel::world {

namespace {

constexpr float DegreesToRadians = 3.14159265f / 180.0f;

enum Side {
    West = 0,
    East = 1,
    Down = 2,
    Up = 3,
    North = 4,
    South = 5,
};

constexpr const char* SideNames[6] = { "west", "east", "down", "up", "north", "south" };

Vec3f readVec3(const json::Value* value, Vec3f fallback = {})
{
    if (!value || !value->isArray() || value->mArray.size() < 3) {
        return fallback;
    }
    return { static_cast<float>(value->mArray[0]->number()), static_cast<float>(value->mArray[1]->number()), static_cast<float>(value->mArray[2]->number()) };
}

std::array<float, 2> readVec2(const json::Value* value)
{
    if (!value || !value->isArray() || value->mArray.size() < 2) {
        return {};
    }
    return { static_cast<float>(value->mArray[0]->number()), static_cast<float>(value->mArray[1]->number()) };
}

Vec3f add(const Vec3f& a, const Vec3f& b)
{
    return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
}

Vec3f subtract(const Vec3f& a, const Vec3f& b)
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

Vec3f scaleVec(const Vec3f& a, float s)
{
    return { a[0] * s, a[1] * s, a[2] * s };
}

Vec3f rotate(const Vec3f& point, const Vec3f& degrees)
{
    Vec3f p = point;
    float rx = degrees[0] * DegreesToRadians;
    float ry = degrees[1] * DegreesToRadians;
    float rz = degrees[2] * DegreesToRadians;
    if (rx != 0.0f) {
        float c = std::cos(rx);
        float s = std::sin(rx);
        p = { p[0], p[1] * c - p[2] * s, p[1] * s + p[2] * c };
    }
    if (ry != 0.0f) {
        float c = std::cos(ry);
        float s = std::sin(ry);
        p = { p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c };
    }
    if (rz != 0.0f) {
        float c = std::cos(rz);
        float s = std::sin(rz);
        p = { p[0] * c - p[1] * s, p[0] * s + p[1] * c, p[2] };
    }
    return p;
}

Vec3f rotateAround(const Vec3f& point, const Vec3f& pivot, const Vec3f& degrees)
{
    if (degrees[0] == 0.0f && degrees[1] == 0.0f && degrees[2] == 0.0f) {
        return point;
    }
    return add(rotate(subtract(point, pivot), degrees), pivot);
}

GeometryFace parseFace(const json::Value* face)
{
    GeometryFace result;
    if (!face || !face->isObject()) {
        return result;
    }
    result.present = true;
    result.uv = readVec2(face->get("uv"));
    result.size = readVec2(face->get("uv_size"));
    if (const json::Value* instance = face->get("material_instance"); instance && instance->isString()) {
        result.materialInstance = instance->string();
    }
    return result;
}

GeometryCube parseCube(const json::Value& value)
{
    GeometryCube cube;
    cube.origin = readVec3(value.get("origin"));
    cube.size = readVec3(value.get("size"));
    cube.rotation = readVec3(value.get("rotation"));
    cube.pivot = readVec3(value.get("pivot"), add(cube.origin, scaleVec(cube.size, 0.5f)));
    if (const json::Value* inflate = value.get("inflate")) {
        cube.inflate = static_cast<float>(inflate->number());
    }
    if (const json::Value* mirror = value.get("mirror")) {
        cube.mirror = mirror->boolean(false);
    }
    const json::Value* uv = value.get("uv");
    if (uv && uv->isObject()) {
        cube.boxUv = false;
        for (int side = 0; side < 6; ++side) {
            cube.faces[side] = parseFace(uv->get(SideNames[side]));
        }
    } else {
        cube.uv = readVec2(uv);
    }
    return cube;
}

void parseBones(const json::Value* bones, Geometry& geometry)
{
    if (!bones || !bones->isArray()) {
        return;
    }
    for (const auto& bone : bones->mArray) {
        GeometryBone parsed;
        if (const json::Value* name = bone->get("name"); name && name->isString()) {
            parsed.name = name->string();
        }
        if (const json::Value* parent = bone->get("parent"); parent && parent->isString()) {
            parsed.parent = parent->string();
        }
        parsed.pivot = readVec3(bone->get("pivot"));
        parsed.rotation = readVec3(bone->get("rotation"));
        if (const json::Value* mirror = bone->get("mirror"); mirror && mirror->mType == json::Value::Type::Boolean) {
            parsed.mirror = mirror->mBoolean;
        }
        if (const json::Value* inflate = bone->get("inflate"); inflate && inflate->isNumber()) {
            parsed.inflate = static_cast<float>(inflate->mNumber);
        }
        if (const json::Value* hidden = bone->get("neverRender"); hidden && hidden->mType == json::Value::Type::Boolean) {
            parsed.neverRender = hidden->mBoolean;
        }
        if (const json::Value* cubes = bone->get("cubes"); cubes && cubes->isArray()) {
            for (const auto& cube : cubes->mArray) {
                parsed.cubes.push_back(parseCube(*cube));
            }
        }
        geometry.bones.push_back(std::move(parsed));
    }
}

std::array<float, 4> boxRegion(const GeometryCube& cube, int side)
{
    float w = cube.size[0];
    float h = cube.size[1];
    float d = cube.size[2];
    float u = cube.uv[0];
    float v = cube.uv[1];
    switch (side) {
    case North:
        return { u + d, v + d, w, h };
    case South:
        return { u + d + w + d, v + d, w, h };
    case East:
        return { u, v + d, d, h };
    case West:
        return { u + d + w, v + d, d, h };
    case Up:
        return { u + d, v, w, d };
    default:
        return { u + d + w, v + d, w, -d };
    }
}

uint32_t worldSide(int geometrySide)
{
    if (geometrySide == West) {
        return East;
    }
    if (geometrySide == East) {
        return West;
    }
    return static_cast<uint32_t>(geometrySide);
}

uint32_t faceIdOf(const Vec3f& normal)
{
    static constexpr uint32_t Ids[6] = { 3, 4, 1, 2, 5, 6 };
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(normal[axis]) > 0.99f) {
            int side = axis == 0 ? (normal[0] < 0 ? West : East) : axis == 1 ? (normal[1] < 0 ? Down : Up) : (normal[2] < 0 ? North : South);
            return Ids[side];
        }
    }
    return 0;
}

}

void GeometryLibrary::load(const std::vector<std::shared_ptr<const PackFiles>>& packs)
{
    byIdentifier.clear();
    for (auto pack = packs.rbegin(); pack != packs.rend(); ++pack) {
        for (const auto& [path, content] : (*pack)->files) {
            if (path.rfind("models/", 0) == 0 && path.size() > 5 && path.compare(path.size() - 5, 5, ".json") == 0) {
                parse(content);
            }
        }
    }
}

void GeometryLibrary::parse(const std::string& text)
{
    std::unique_ptr<json::Value> document = json::parse(text);
    if (!document || !document->isObject()) {
        return;
    }
    if (const json::Value* list = document->get("minecraft:geometry"); list && list->isArray()) {
        for (const auto& entry : list->mArray) {
            const json::Value* description = entry->get("description");
            const json::Value* identifier = description ? description->get("identifier") : nullptr;
            if (!identifier || !identifier->isString()) {
                continue;
            }
            Geometry geometry;
            if (const json::Value* width = description->get("texture_width")) {
                geometry.textureWidth = static_cast<float>(width->number(16.0));
            }
            if (const json::Value* height = description->get("texture_height")) {
                geometry.textureHeight = static_cast<float>(height->number(16.0));
            }
            parseBones(entry->get("bones"), geometry);
            byIdentifier[identifier->string()] = std::move(geometry);
        }
        return;
    }
    for (const std::string& key : document->mKeys) {
        if (key.rfind("geometry.", 0) != 0) {
            continue;
        }
        const json::Value* entry = document->get(key);
        if (!entry || !entry->isObject()) {
            continue;
        }
        Geometry geometry;
        if (const json::Value* width = entry->get("texturewidth")) {
            geometry.textureWidth = static_cast<float>(width->number(16.0));
        }
        if (const json::Value* height = entry->get("textureheight")) {
            geometry.textureHeight = static_cast<float>(height->number(16.0));
        }
        parseBones(entry->get("bones"), geometry);
        size_t separator = key.find(':');
        std::string name = key.substr(0, separator);
        if (separator != std::string::npos) {
            parents[name] = key.substr(separator + 1);
        }
        byIdentifier[name] = std::move(geometry);
    }
}

/**
 * Applies legacy geometry inheritance (geometry.child:geometry.parent): the
 * child keeps its own bones and takes every parent bone it does not redefine.
 */
void GeometryLibrary::resolveInheritance()
{
    std::function<void(const std::string&, int)> resolve = [&](const std::string& name, int depth) {
        auto parent = parents.find(name);
        if (parent == parents.end() || depth > 16) {
            return;
        }
        std::string parentName = parent->second;
        parents.erase(parent);
        resolve(parentName, depth + 1);
        auto base = byIdentifier.find(parentName);
        auto child = byIdentifier.find(name);
        if (base == byIdentifier.end() || child == byIdentifier.end()) {
            return;
        }
        std::vector<GeometryBone> merged = base->second.bones;
        for (const GeometryBone& bone : child->second.bones) {
            auto same = std::find_if(merged.begin(), merged.end(), [&](const GeometryBone& existing) {
                return existing.name == bone.name;
            });
            if (same != merged.end()) {
                *same = bone;
            } else {
                merged.push_back(bone);
            }
        }
        child->second.bones = std::move(merged);
    };
    while (!parents.empty()) {
        resolve(parents.begin()->first, 0);
    }
}

const Geometry* GeometryLibrary::find(const std::string& identifier) const
{
    auto found = byIdentifier.find(identifier);
    return found == byIdentifier.end() ? nullptr : &found->second;
}

const Geometry* GeometryLibrary::first() const
{
    return byIdentifier.empty() ? nullptr : &byIdentifier.begin()->second;
}

std::vector<ModelQuad> buildGeometryQuads(const Geometry& geometry, const BlockTransform& transform, const std::function<uint32_t(const std::string&, int)>& material)
{
    std::map<std::string, const GeometryBone*> bones;
    for (const GeometryBone& bone : geometry.bones) {
        bones[bone.name] = &bone;
    }
    auto toBlock = [&](const GeometryBone& bone, const GeometryCube& cube, Vec3f point) {
        point = rotateAround(point, cube.pivot, cube.rotation);
        for (const GeometryBone* current = &bone; current;) {
            point = rotateAround(point, current->pivot, current->rotation);
            auto parent = bones.find(current->parent);
            current = parent == bones.end() || parent->second == current ? nullptr : parent->second;
        }
        Vec3f world { 8.0f - point[0], point[1], 8.0f + point[2] };
        Vec3f centered = subtract(world, { 8.0f, 8.0f, 8.0f });
        centered = { centered[0] * transform.scale[0], centered[1] * transform.scale[1], centered[2] * transform.scale[2] };
        centered = rotate(centered, transform.rotation);
        return add(add(centered, { 8.0f, 8.0f, 8.0f }), scaleVec(transform.translation, 16.0f));
    };

    static const Vec3f Normals[6] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    static const Vec3f Ups[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, -1 }, { 0, 1, 0 }, { 0, 1, 0 } };

    std::vector<ModelQuad> quads;
    for (const GeometryBone& bone : geometry.bones) {
        for (const GeometryCube& cube : bone.cubes) {
            Vec3f min = subtract(cube.origin, { cube.inflate, cube.inflate, cube.inflate });
            Vec3f max = add(add(cube.origin, cube.size), { cube.inflate, cube.inflate, cube.inflate });
            Vec3f center = scaleVec(add(min, max), 0.5f);
            Vec3f half = scaleVec(subtract(max, min), 0.5f);
            for (int side = 0; side < 6; ++side) {
                std::array<float, 4> region {};
                std::string instance;
                if (cube.boxUv) {
                    region = boxRegion(cube, side);
                } else {
                    const GeometryFace& face = cube.faces[side];
                    if (!face.present) {
                        continue;
                    }
                    region = { face.uv[0], face.uv[1], face.size[0], face.size[1] };
                    instance = face.materialInstance;
                }
                const Vec3f& normal = Normals[side];
                const Vec3f& up = Ups[side];
                Vec3f forward = scaleVec(normal, -1.0f);
                Vec3f right { forward[1] * up[2] - forward[2] * up[1], forward[2] * up[0] - forward[0] * up[2], forward[0] * up[1] - forward[1] * up[0] };
                float halfRight = std::abs(right[0]) * half[0] + std::abs(right[1]) * half[1] + std::abs(right[2]) * half[2];
                float halfUp = std::abs(up[0]) * half[0] + std::abs(up[1]) * half[1] + std::abs(up[2]) * half[2];
                float halfNormal = std::abs(normal[0]) * half[0] + std::abs(normal[1]) * half[1] + std::abs(normal[2]) * half[2];
                Vec3f faceCenter = add(center, scaleVec(normal, halfNormal));
                std::array<Vec3f, 4> corners {
                    add(add(faceCenter, scaleVec(right, -halfRight)), scaleVec(up, halfUp)),
                    add(add(faceCenter, scaleVec(right, halfRight)), scaleVec(up, halfUp)),
                    add(add(faceCenter, scaleVec(right, halfRight)), scaleVec(up, -halfUp)),
                    add(add(faceCenter, scaleVec(right, -halfRight)), scaleVec(up, -halfUp)),
                };
                float u0 = region[0];
                float u1 = region[0] + region[2];
                if (cube.mirror) {
                    std::swap(u0, u1);
                }
                std::array<std::array<float, 2>, 4> uvs { { { u0, region[1] }, { u1, region[1] }, { u1, region[1] + region[3] }, { u0, region[1] + region[3] } } };

                uint32_t targetSide = worldSide(side);
                ModelQuad quad;
                for (size_t corner = 0; corner < 4; ++corner) {
                    Vec3f position = toBlock(bone, cube, corners[corner]);
                    for (int axis = 0; axis < 3; ++axis) {
                        quad.positions[corner][axis] = static_cast<int16_t>(std::clamp(std::lround(position[axis] * 16.0f), -32768L, 32767L));
                    }
                    quad.uvs[corner][0] = static_cast<uint16_t>(std::clamp(uvs[corner][0] / geometry.textureWidth * 4096.0f, 0.0f, 65535.0f));
                    quad.uvs[corner][1] = static_cast<uint16_t>(std::clamp(uvs[corner][1] / geometry.textureHeight * 4096.0f, 0.0f, 65535.0f));
                }
                Vec3f a = toBlock(bone, cube, faceCenter);
                Vec3f b = toBlock(bone, cube, add(faceCenter, normal));
                Vec3f worldNormal = subtract(b, a);
                float length = std::sqrt(worldNormal[0] * worldNormal[0] + worldNormal[1] * worldNormal[1] + worldNormal[2] * worldNormal[2]);
                if (length > 0.0f) {
                    worldNormal = scaleVec(worldNormal, 1.0f / length);
                }
                quad.material = material(instance, static_cast<int>(targetSide));
                quad.flags = faceIdOf(worldNormal) | QuadTwoSided;
                quads.push_back(quad);
            }
        }
    }
    return quads;
}

}
