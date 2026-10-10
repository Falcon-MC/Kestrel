#include "world/BlockAssets.h"

#include "world/assets/TextureTools.h"
#include "Core/Json/Json.h"
#include "ui/Image.h"
#include "util/JsonText.h"
#include "util/Text.h"
#include "world/EntityAnimation.h"
#include "world/EntityMaterialBlend.h"
#include "world/Geometry.h"
#include "world/ItemInfo.h"
#include "world/MolangScript.h"
#include "world/PackSource.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace kestrel::world {

namespace {

using util::endsWith;
using util::lowercase;
using util::startsWith;
using util::stripJsonComments;

/**
 * A client entity definition: its textures and geometries by lowercase short
 * name, the default of each, and its render controllers with their condition
 * source (empty when unconditional).
 */
struct ClientEntity {
    std::string texture;
    std::string geometry;
    std::map<std::string, std::string> textures;
    std::map<std::string, std::string> geometries;
    std::map<std::string, std::string> materials;
    std::vector<std::pair<std::string, std::string>> renderControllers;
    std::vector<uint64_t> version;
    std::shared_ptr<const EntityScripts> scripts;
};

std::map<std::string, std::string> readNamedStrings(const json::Value* table)
{
    std::map<std::string, std::string> out;
    if (!table) {
        return out;
    }
    for (const std::string& key : table->mKeys) {
        const json::Value& value = *table->mObject.at(key);
        if (value.isString()) {
            out[lowercase(key)] = value.mString;
        }
    }
    return out;
}

/**
 * Reads a minecraft:client_entity definition, or with root minecraft:attachable
 * an attachable, which describes itself the same way.
 */
void readClientEntity(const std::string& text, std::map<std::string, ClientEntity>& out, const char* root = "minecraft:client_entity")
{
    std::unique_ptr<json::Value> document = json::parse(stripJsonComments(text));
    const json::Value* entity = document ? document->get(root) : nullptr;
    const json::Value* description = entity ? entity->get("description") : nullptr;
    const json::Value* identifier = description ? description->get("identifier") : nullptr;
    if (!identifier || !identifier->isString()) {
        return;
    }
    ClientEntity parsed;
    parsed.textures = readNamedStrings(description->get("textures"));
    parsed.geometries = readNamedStrings(description->get("geometry"));
    parsed.materials = readNamedStrings(description->get("materials"));
    if (parsed.textures.empty() || parsed.geometries.empty()) {
        return;
    }
    auto texture = parsed.textures.find("default");
    auto model = parsed.geometries.find("default");
    parsed.texture = texture != parsed.textures.end() ? texture->second : parsed.textures.begin()->second;
    parsed.geometry = model != parsed.geometries.end() ? model->second : parsed.geometries.begin()->second;
    if (const json::Value* controllers = description->get("render_controllers"); controllers && controllers->isArray()) {
        for (const std::unique_ptr<json::Value>& entry : controllers->mArray) {
            if (entry->isString()) {
                parsed.renderControllers.emplace_back(lowercase(entry->mString), std::string {});
                continue;
            }
            for (const std::string& key : entry->mKeys) {
                const json::Value& condition = *entry->mObject.at(key);
                parsed.renderControllers.emplace_back(lowercase(key), condition.isString() ? condition.mString : std::string {});
            }
        }
    }
    parsed.scripts = readEntityScripts(*description);
    if (const json::Value* version = description->get("min_engine_version"); version && version->isString()) {
        parsed.version = util::versionNumbers(version->mString);
    }
    auto existing = out.find(identifier->mString);
    if (existing == out.end() || parsed.version >= existing->second.version) {
        out[identifier->mString] = std::move(parsed);
    }
}

std::array<float, 3> rotateEulerAround(std::array<float, 3> point, const std::array<float, 3>& pivot, const std::array<float, 3>& degrees)
{
    constexpr float Radians = 3.14159265f / 180.0f;
    float sx = std::sin(degrees[0] * Radians);
    float cx = std::cos(degrees[0] * Radians);
    float sy = std::sin(degrees[1] * Radians);
    float cy = std::cos(degrees[1] * Radians);
    float sz = std::sin(degrees[2] * Radians);
    float cz = std::cos(degrees[2] * Radians);
    std::array<float, 3> value { point[0] - pivot[0], point[1] - pivot[1], point[2] - pivot[2] };
    value = { value[0], value[1] * cx - value[2] * sx, value[1] * sx + value[2] * cx };
    value = { value[0] * cy + value[2] * sy, value[1], -value[0] * sy + value[2] * cy };
    value = { value[0] * cz - value[1] * sz, value[0] * sz + value[1] * cz, value[2] };
    return { value[0] + pivot[0], value[1] + pivot[1], value[2] + pivot[2] };
}

/**
 * The pixels behind a texture a geometry's texture meshes name, or no pixels
 * when the entity has no such texture.
 */
struct MeshTexture {
    uint32_t width = 0;
    uint32_t height = 0;
    const std::vector<uint8_t>* rgba = nullptr;
};
using MeshTextures = std::function<MeshTexture(const std::string& name)>;

/**
 * Builds a bone's texture meshes as one texel thick solids: the texture's
 * full front and back, with an edge wherever an opaque texel meets a clear
 * one, each edge showing its own texel. Points follow the same handedness
 * flip as cubes, so they turn the way the geometry file says.
 */
void appendTextureMeshes(const GeometryBone& bone, uint16_t boneIndex, const MeshTextures& textures, EntityRig& model)
{
    constexpr uint8_t Opaque = 26;
    for (const GeometryTextureMesh& mesh : bone.textureMeshes) {
        MeshTexture texture = textures ? textures(lowercase(mesh.texture)) : MeshTexture {};
        if (!texture.rgba || texture.width == 0 || texture.height == 0) {
            continue;
        }
        const uint32_t width = texture.width;
        const uint32_t height = texture.height;
        // Meshes are laid out in texels of a 16 texel texture, whatever its real size.
        const float texel = 16.0f / float(width);
        auto opaque = [&](int32_t x, int32_t y) {
            return x >= 0 && y >= 0 && x < int32_t(width) && y < int32_t(height) && (*texture.rgba)[(size_t(y) * width + size_t(x)) * 4 + 3] >= Opaque;
        };
        std::array<float, 3> rotation { -mesh.rotation[0], -mesh.rotation[1], mesh.rotation[2] };
        auto place = [&](float u, float y, float v) {
            std::array<float, 3> local {
                -(u * texel - mesh.localPivot[0]) * mesh.scale[0],
                (y - mesh.localPivot[1]) * mesh.scale[1],
                (v * texel - mesh.localPivot[2]) * mesh.scale[2],
            };
            local = rotateEulerAround(local, { 0.0f, 0.0f, 0.0f }, rotation);
            std::array<int16_t, 3> out {};
            out[0] = static_cast<int16_t>(std::lround((local[0] - mesh.position[0]) * 16.0f));
            out[1] = static_cast<int16_t>(std::lround((local[1] + mesh.position[1]) * 16.0f));
            out[2] = static_cast<int16_t>(std::lround((local[2] + mesh.position[2]) * 16.0f));
            return out;
        };
        auto uv = [](float value) {
            return static_cast<uint16_t>(std::clamp(value, 0.0f, 1.0f) * 4096.0f);
        };
        auto emit = [&](const std::array<std::array<float, 3>, 4>& corners, const std::array<std::array<float, 2>, 4>& uvs, uint32_t faceId) {
            ModelQuad quad;
            for (size_t corner = 0; corner < 4; ++corner) {
                quad.positions[corner] = place(corners[corner][0], corners[corner][1], corners[corner][2]);
                quad.uvs[corner] = { uv(uvs[corner][0]), uv(uvs[corner][1]) };
            }
            quad.flags = faceId | QuadTwoSided;
            model.quads.push_back(quad);
            model.quadBones.push_back(boneIndex);
        };
        const float w = float(width);
        const float h = float(height);
        const std::array<std::array<float, 2>, 4> whole { { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } };
        emit({ { { 0, 0.5f, 0 }, { w, 0.5f, 0 }, { w, 0.5f, h }, { 0, 0.5f, h } } }, whole, 2);
        emit({ { { 0, -0.5f, 0 }, { w, -0.5f, 0 }, { w, -0.5f, h }, { 0, -0.5f, h } } }, whole, 1);
        for (int32_t y = 0; y < int32_t(height); ++y) {
            for (int32_t x = 0; x < int32_t(width); ++x) {
                if (!opaque(x, y)) {
                    continue;
                }
                float left = float(x);
                float right = left + 1.0f;
                float top = float(y);
                float bottom = top + 1.0f;
                // Edges sample the middle of their own texel so filtering never reaches a clear neighbour.
                float u0 = (left + 0.25f) / w;
                float u1 = (left + 0.75f) / w;
                float v0 = (top + 0.25f) / h;
                float v1 = (top + 0.75f) / h;
                const std::array<std::array<float, 2>, 4> inset { { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } } };
                if (!opaque(x - 1, y)) {
                    emit({ { { left, 0.5f, top }, { left, 0.5f, bottom }, { left, -0.5f, bottom }, { left, -0.5f, top } } }, inset, 4);
                }
                if (!opaque(x + 1, y)) {
                    emit({ { { right, 0.5f, bottom }, { right, 0.5f, top }, { right, -0.5f, top }, { right, -0.5f, bottom } } }, inset, 3);
                }
                if (!opaque(x, y - 1)) {
                    emit({ { { right, 0.5f, top }, { left, 0.5f, top }, { left, -0.5f, top }, { right, -0.5f, top } } }, inset, 5);
                }
                if (!opaque(x, y + 1)) {
                    emit({ { { left, 0.5f, bottom }, { right, 0.5f, bottom }, { right, -0.5f, bottom }, { left, -0.5f, bottom } } }, inset, 6);
                }
            }
        }
    }
}

/**
 * The shading face of a polygon from its normal in model handedness: the
 * face of the axis it leans along the most, or none for a zero normal.
 */
uint32_t polyFaceId(const std::array<float, 3>& normal)
{
    int axis = 0;
    for (int candidate = 1; candidate < 3; ++candidate) {
        if (std::abs(normal[candidate]) > std::abs(normal[axis])) {
            axis = candidate;
        }
    }
    if (normal[axis] == 0.0f) {
        return 0;
    }
    if (axis == 0) {
        return normal[0] < 0.0f ? 3 : 4;
    }
    if (axis == 1) {
        return normal[1] < 0.0f ? 1 : 2;
    }
    return normal[2] < 0.0f ? 5 : 6;
}

/**
 * Adds a bone's polygon mesh, each triangle as a quad whose last corner
 * repeats its third. Positions are model pixels, flipped on x like cubes, and
 * UVs count v up from the bottom of the texture.
 */
void appendPolyMesh(const GeometryBone& bone, uint16_t boneIndex, float width, float height, EntityRig& model)
{
    if (!bone.polyMeshSet) {
        return;
    }
    const GeometryPolyMesh& mesh = bone.polyMesh;
    float scaleU = mesh.normalizedUvs ? 1.0f : 1.0f / width;
    float scaleV = mesh.normalizedUvs ? 1.0f : 1.0f / height;
    for (size_t first = 0; first + 2 < mesh.triangles.size(); first += 3) {
        ModelQuad quad;
        std::array<float, 3> normal {};
        for (size_t corner = 0; corner < 4; ++corner) {
            const GeometryPolyVertex& vertex = mesh.triangles[first + std::min<size_t>(corner, 2)];
            std::array<float, 3> point { -vertex.position[0], vertex.position[1], vertex.position[2] };
            for (int axis = 0; axis < 3; ++axis) {
                quad.positions[corner][axis] = static_cast<int16_t>(std::clamp<long>(std::lround(point[axis] * 16.0f), -32768L, 32767L));
            }
            float u = std::clamp(vertex.uv[0] * scaleU, 0.0f, 1.0f);
            float v = std::clamp(1.0f - vertex.uv[1] * scaleV, 0.0f, 1.0f);
            quad.uvs[corner] = { static_cast<uint16_t>(u * 4096.0f), static_cast<uint16_t>(v * 4096.0f) };
            if (corner < 3) {
                normal[0] -= vertex.normal[0];
                normal[1] += vertex.normal[1];
                normal[2] += vertex.normal[2];
            }
        }
        quad.flags = polyFaceId(normal) | QuadTwoSided;
        model.quads.push_back(quad);
        model.quadBones.push_back(boneIndex);
    }
}

/**
 * Turns a geometry into an animatable model. Geometry files mirror the x axis
 * and the x and y rotations, so pivots, cubes and rotations are flipped back
 * into world handedness; each cube is turned around its own pivot and its
 * quads stay unposed, tagged with their bone, with the box or per-face UV
 * unwrap over the whole texture.
 */
void buildEntityRig(const Geometry& geometry, EntityRig& model, const MeshTextures& textures = {})
{
    std::map<std::string, int32_t> indexByName;
    for (const GeometryBone& bone : geometry.bones) {
        indexByName.emplace(lowercase(bone.name), static_cast<int32_t>(model.bones.size()));
        EntityBone rigBone;
        rigBone.name = bone.name;
        rigBone.pivot = { -bone.pivot[0], bone.pivot[1], bone.pivot[2] };
        rigBone.rotation = { -bone.rotation[0], -bone.rotation[1], bone.rotation[2] };
        rigBone.bound = !bone.binding.empty();
        rigBone.anchoredToHolder = !bone.textureMeshes.empty() && !bone.pivotSet && bone.binding.empty();
        model.bones.push_back(rigBone);
    }
    for (size_t index = 0; index < geometry.bones.size(); ++index) {
        auto found = indexByName.find(lowercase(geometry.bones[index].parent));
        if (found != indexByName.end() && found->second != static_cast<int32_t>(index)) {
            model.bones[index].parent = found->second;
        }
    }
    for (size_t index = 0; index < model.bones.size(); ++index) {
        int32_t walker = model.bones[index].parent;
        for (size_t steps = 0; walker >= 0; ++steps) {
            if (walker == static_cast<int32_t>(index) || steps > model.bones.size()) {
                model.bones[index].parent = -1;
                break;
            }
            walker = model.bones[walker].parent;
        }
    }

    static constexpr std::array<float, 3> FaceNormals[6] = { { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    static constexpr std::array<float, 3> FaceRights[6] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { 1, 0, 0 } };
    static constexpr std::array<float, 3> FaceUps[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    static constexpr float CornerSigns[4][2] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } };
    static constexpr uint32_t FaceIds[6] = { 5, 6, 3, 4, 2, 1 };
    static constexpr int GeometrySides[6] = { 4, 5, 0, 1, 3, 2 };
    float width = geometry.textureWidth > 0.0f ? geometry.textureWidth : 64.0f;
    float height = geometry.textureHeight > 0.0f ? geometry.textureHeight : 64.0f;
    model.textureAspect = height / width;

    for (size_t boneIndex = 0; boneIndex < geometry.bones.size(); ++boneIndex) {
        const GeometryBone& bone = geometry.bones[boneIndex];
        if (bone.neverRender) {
            continue;
        }
        appendTextureMeshes(bone, static_cast<uint16_t>(boneIndex), textures, model);
        for (const GeometryCube& cube : bone.cubes) {
            float inflate = cube.inflate + bone.inflate;
            std::array<float, 3> min {
                -(cube.origin[0] + cube.size[0]) - inflate,
                cube.origin[1] - inflate,
                cube.origin[2] - inflate,
            };
            std::array<float, 3> max {
                -cube.origin[0] + inflate,
                cube.origin[1] + cube.size[1] + inflate,
                cube.origin[2] + cube.size[2] + inflate,
            };
            std::array<float, 3> cubePivot { -cube.pivot[0], cube.pivot[1], cube.pivot[2] };
            std::array<float, 3> cubeRotation { -cube.rotation[0], -cube.rotation[1], cube.rotation[2] };
            std::array<float, 3> bindOffset {};
            if (bone.bindRotationSet) {
                std::array<float, 3> rotation { -bone.bindRotation[0], -bone.bindRotation[1], bone.bindRotation[2] };
                auto pivot = rotateEulerAround(cubePivot, { -bone.pivot[0], bone.pivot[1], bone.pivot[2] }, rotation);
                for (size_t axis = 0; axis < 3; ++axis) {
                    cubeRotation[axis] += rotation[axis];
                    bindOffset[axis] = pivot[axis] - cubePivot[axis];
                }
            }
            bool mirror = cube.mirrorSet ? cube.mirror : bone.mirror;
            bool inverted = cube.size[0] < 0.0f || cube.size[1] < 0.0f || cube.size[2] < 0.0f;
            std::array<float, 3> cubeCenter = rotateEulerAround({ (min[0] + max[0]) * 0.5f, (min[1] + max[1]) * 0.5f, (min[2] + max[2]) * 0.5f }, cubePivot, cubeRotation);
            for (size_t axis = 0; axis < 3; ++axis) cubeCenter[axis] += bindOffset[axis];
            float x = cube.size[0];
            float y = cube.size[1];
            float z = cube.size[2];
            int flatAxis = -1;
            int flatAxes = 0;
            for (int axis = 0; axis < 3; ++axis) if (max[axis] == min[axis]) { flatAxis = axis; ++flatAxes; }
            if (flatAxes > 1) continue;
            for (int face = 0; face < 6; ++face) {
                if (flatAxis >= 0 && FaceNormals[face][flatAxis] == 0.0f) continue;
                // Box-UV planes share one rectangle on both sides; per-face planes retain both maps.
                if (flatAxis >= 0 && cube.boxUv && FaceNormals[face][flatAxis] > 0.0f) continue;
                std::array<float, 4> region {};
                if (cube.boxUv) {
                    float u = cube.uv[0];
                    float v = cube.uv[1];
                    const std::array<float, 4> boxRegions[6] = {
                        { u + z, v + z, x, y },
                        { u + z + x + z, v + z, x, y },
                        { u + z + x, v + z, z, y },
                        { u, v + z, z, y },
                        { u + z, v, x, z },
                        { u + z + x, v, x, z },
                    };
                    int source = face;
                    if (mirror && (face == 2 || face == 3)) {
                        source = face == 2 ? 3 : 2;
                    }
                    region = boxRegions[source];
                } else {
                    const GeometryFace& uv = cube.faces[GeometrySides[face]];
                    if (!uv.present) {
                        continue;
                    }
                    region = { uv.uv[0], uv.uv[1], uv.size[0], uv.size[1] };
                    // Bedrock per-face UVs turn the top and bottom by 180 degrees
                    // relative to the box unwrap. Flat effects depend on this too.
                    if (face == 4 || face == 5) {
                        region[0] += region[2];
                        region[1] += region[3];
                        region[2] = -region[2];
                        region[3] = -region[3];
                    }
                }
                float left = region[0] / width;
                float right = (region[0] + region[2]) / width;
                if (mirror && cube.boxUv) {
                    std::swap(left, right);
                }
                float top = region[1] / height;
                float bottom = (region[1] + region[3]) / height;
                const std::array<std::array<float, 2>, 4> uvs { { { left, top }, { right, top }, { right, bottom }, { left, bottom } } };
                const std::array<float, 3>& normal = FaceNormals[face];
                const std::array<float, 3>& rightAxis = FaceRights[face];
                const std::array<float, 3>& upAxis = FaceUps[face];
                ModelQuad quad;
                for (size_t corner = 0; corner < 4; ++corner) {
                    std::array<float, 3> point {};
                    for (int axis = 0; axis < 3; ++axis) {
                        float center = (min[axis] + max[axis]) * 0.5f;
                        float half = (max[axis] - min[axis]) * 0.5f;
                        float offset = normal[axis] + rightAxis[axis] * CornerSigns[corner][0] + upAxis[axis] * CornerSigns[corner][1];
                        point[axis] = center + offset * half;
                    }
                    point = rotateEulerAround(point, cubePivot, cubeRotation);
                    for (size_t axis = 0; axis < 3; ++axis) point[axis] += bindOffset[axis];
                    for (int axis = 0; axis < 3; ++axis) {
                        quad.positions[corner][axis] = static_cast<int16_t>(std::lround(point[axis] * 16.0f));
                    }
                    const std::array<float, 2>& uv = uvs[corner];
                    quad.uvs[corner] = { static_cast<uint16_t>(std::clamp(uv[0], 0.0f, 15.0f) * 4096.0f), static_cast<uint16_t>(std::clamp(uv[1], 0.0f, 15.0f) * 4096.0f) };
                }
                quad.flags = FaceIds[face] | QuadTwoSided;
                if (flatAxis >= 0 && !cube.boxUv) quad.flags = FaceIds[face];
                if (inverted) {
                    // A cube with a negative size is drawn inside out and only shows its far
                    // walls, which packs use as a backdrop; wind it to face the middle.
                    std::array<float, 3> edgeA {}, edgeB {}, toCenter {};
                    for (int axis = 0; axis < 3; ++axis) {
                        edgeA[axis] = float(quad.positions[1][axis] - quad.positions[0][axis]);
                        edgeB[axis] = float(quad.positions[3][axis] - quad.positions[0][axis]);
                        toCenter[axis] = cubeCenter[axis] * 16.0f - float(quad.positions[0][axis]);
                    }
                    std::array<float, 3> normal { edgeA[1] * edgeB[2] - edgeA[2] * edgeB[1], edgeA[2] * edgeB[0] - edgeA[0] * edgeB[2], edgeA[0] * edgeB[1] - edgeA[1] * edgeB[0] };
                    if (normal[0] * toCenter[0] + normal[1] * toCenter[1] + normal[2] * toCenter[2] < 0.0f) {
                        std::swap(quad.positions[1], quad.positions[3]);
                        std::swap(quad.uvs[1], quad.uvs[3]);
                    }
                    quad.flags = FaceIds[face] | QuadInward;
                }
                model.quads.push_back(quad);
                model.quadBones.push_back(static_cast<uint16_t>(boneIndex));
            }
        }
        appendPolyMesh(bone, static_cast<uint16_t>(boneIndex), width, height, model);
    }
}

using ControllerArrays = std::unordered_map<std::string, std::vector<std::string>>;

/**
 * A render controller as read from its file: the geometry and texture
 * selectors rewritten into Molang yielding an index into their lowercase
 * choice names (like texture.white), and the part visibility rules.
 */
struct RenderControllerSource {
    molang::Script geometry;
    std::vector<std::string> geometryChoices;
    molang::Script texture;
    std::vector<std::string> textureChoices;
    std::array<molang::Script, 2> extraTextures;
    std::array<std::vector<std::string>, 2> extraTextureChoices;
    std::array<molang::Script, 4> color;
    std::array<molang::Script, 4> overlay;
    std::array<molang::Script, 4> hurtColor;
    std::array<molang::Script, 4> fireColor;
    std::vector<EntityPartRule> parts;
    molang::Script material;
    std::vector<std::string> materialChoices;
    bool ignoreLighting = false;
    std::array<molang::Script, 4> uvAnim;
    bool uvAnimated = false;
};

/**
 * How a vanilla entity material draws: the additive beam and glow materials
 * add light, the alpha blended ones blend, everything else is cut out.
 */
EntityBlend blendOf(const std::string& material)
{
    if (material.find("additive") != std::string::npos) {
        return EntityBlend::Additive;
    }
    if (material.find("blend") != std::string::npos) {
        return EntityBlend::Blend;
    }
    return EntityBlend::Opaque;
}

std::string rewriteSelector(const std::string& expression, const ControllerArrays& arrays, const std::string& kind, std::vector<std::string>& choices)
{
    auto isWord = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
    };
    std::string out;
    size_t at = 0;
    while (at < expression.size()) {
        char c = expression[at];
        if (c == '\'') {
            size_t close = expression.find('\'', at + 1);
            size_t end = close == std::string::npos ? expression.size() : close + 1;
            out += expression.substr(at, end - at);
            at = end;
            continue;
        }
        if (!std::isalpha(static_cast<unsigned char>(c)) && c != '_') {
            out += c;
            ++at;
            continue;
        }
        size_t start = at;
        while (at < expression.size() && isWord(expression[at])) {
            ++at;
        }
        std::string word = lowercase(expression.substr(start, at - start));
        if (startsWith(word, "array.")) {
            std::string index = "0";
            size_t next = at;
            while (next < expression.size() && std::isspace(static_cast<unsigned char>(expression[next]))) {
                ++next;
            }
            if (next < expression.size() && expression[next] == '[') {
                int depth = 0;
                size_t close = next;
                for (; close < expression.size(); ++close) {
                    if (expression[close] == '[') {
                        ++depth;
                    } else if (expression[close] == ']' && --depth == 0) {
                        break;
                    }
                }
                index = rewriteSelector(expression.substr(next + 1, close - next - 1), arrays, kind, choices);
                at = std::min(close + 1, expression.size());
            }
            auto found = arrays.find(word);
            if (found == arrays.end() || found->second.empty()) {
                out += "0";
                continue;
            }
            std::string base = std::to_string(choices.size());
            std::string count = std::to_string(found->second.size());
            choices.insert(choices.end(), found->second.begin(), found->second.end());
            out += "(" + base + " + math.mod(math.mod(math.floor(" + index + "), " + count + ") + " + count + ", " + count + "))";
            continue;
        }
        if (startsWith(word, kind + ".")) {
            auto found = std::find(choices.begin(), choices.end(), word);
            size_t index = static_cast<size_t>(found - choices.begin());
            if (found == choices.end()) {
                choices.push_back(word);
            }
            out += std::to_string(index);
            continue;
        }
        out += expression.substr(start, at - start);
    }
    return out;
}

ControllerArrays readControllerArrays(const json::Value* table)
{
    ControllerArrays out;
    if (!table) {
        return out;
    }
    for (const std::string& key : table->mKeys) {
        const json::Value& list = *table->mObject.at(key);
        if (!list.isArray()) {
            continue;
        }
        std::vector<std::string>& entries = out[lowercase(key)];
        for (const std::unique_ptr<json::Value>& entry : list.mArray) {
            if (entry->isString()) {
                entries.push_back(lowercase(entry->mString));
            }
        }
    }
    return out;
}

void readRenderControllers(const std::string& text, std::unordered_map<std::string, RenderControllerSource>& out)
{
    std::unique_ptr<json::Value> document = json::parse(stripJsonComments(text));
    const json::Value* controllers = document ? document->get("render_controllers") : nullptr;
    if (!controllers) {
        return;
    }
    for (const std::string& name : controllers->mKeys) {
        const json::Value& controller = *controllers->mObject.at(name);
        const json::Value* arrays = controller.get("arrays");
        ControllerArrays textureArrays = readControllerArrays(arrays ? arrays->get("textures") : nullptr);
        ControllerArrays geometryArrays = readControllerArrays(arrays ? arrays->get("geometries") : nullptr);
        ControllerArrays materialArrays = readControllerArrays(arrays ? arrays->get("materials") : nullptr);
        RenderControllerSource parsed;
        std::string geometry = "Geometry.default";
        if (const json::Value* value = controller.get("geometry"); value && value->isString()) {
            geometry = value->mString;
        }
        parsed.geometry = molang::Script::compile(rewriteSelector(geometry, geometryArrays, "geometry", parsed.geometryChoices));
        std::string texture = "Texture.default";
        if (const json::Value* list = controller.get("textures"); list && list->isArray() && !list->mArray.empty() && list->mArray.front()->isString()) {
            texture = list->mArray.front()->mString;
        }
        parsed.texture = molang::Script::compile(rewriteSelector(texture, textureArrays, "texture", parsed.textureChoices));
        if (const json::Value* list = controller.get("textures"); list && list->isArray()) {
            for (size_t index = 1; index < std::min<size_t>(list->mArray.size(), 3); ++index) {
                if (list->mArray[index]->isString()) parsed.extraTextures[index - 1] = molang::Script::compile(
                    rewriteSelector(list->mArray[index]->mString, textureArrays, "texture", parsed.extraTextureChoices[index - 1]));
            }
        }
        auto colorOf = [&](const char* name, std::array<molang::Script, 4>& channels, const std::array<double, 4>& defaults) {
            const json::Value* value = controller.get(name);
            static constexpr const char* Keys[] = { "r", "g", "b", "a" };
            for (size_t channel = 0; channel < 4; ++channel) {
                const json::Value* entry = value && value->isObject() ? value->get(Keys[channel]) : nullptr;
                channels[channel] = entry && entry->isString() ? molang::Script::compile(entry->mString, defaults[channel])
                    : entry && entry->isNumber() ? molang::Script(entry->mNumber) : molang::Script::compile("this", defaults[channel]);
            }
        };
        colorOf("color", parsed.color, { 1, 1, 1, 1 });
        colorOf("overlay_color", parsed.overlay, { 0, 0, 0, 0 });
        colorOf("is_hurt_color", parsed.hurtColor, { 1, 0, 0, 0.5 });
        colorOf("on_fire_color", parsed.fireColor, { 1, 1, 1, 0 });
        std::string material = "Material.default";
        if (const json::Value* list = controller.get("materials"); list && list->isArray() && !list->mArray.empty()) {
            const json::Value& first = *list->mArray.front();
            if (!first.mKeys.empty()) {
                if (const json::Value* value = first.get(first.mKeys.front()); value && value->isString()) {
                    material = value->mString;
                }
            }
        }
        parsed.material = molang::Script::compile(rewriteSelector(material, materialArrays, "material", parsed.materialChoices));
        if (const json::Value* value = controller.get("ignore_lighting"); value && value->mType == json::Value::Type::Boolean) {
            parsed.ignoreLighting = value->mBoolean;
        }
        if (const json::Value* animation = controller.get("uv_anim"); animation && animation->isObject()) {
            static constexpr const char* Channels[4] = { "offset", "offset", "scale", "scale" };
            for (size_t slot = 0; slot < 4; ++slot) {
                double fallback = slot < 2 ? 0.0 : 1.0;
                const json::Value* list = animation->get(Channels[slot]);
                const json::Value* entry = list && list->isArray() && list->mArray.size() > slot % 2 ? list->mArray[slot % 2].get() : nullptr;
                if (entry && entry->isNumber()) {
                    parsed.uvAnim[slot] = molang::Script(entry->mNumber);
                } else if (entry && entry->isString()) {
                    parsed.uvAnim[slot] = molang::Script::compile(entry->mString, fallback);
                } else {
                    parsed.uvAnim[slot] = molang::Script(fallback);
                }
            }
            parsed.uvAnimated = true;
        }
        if (const json::Value* visibility = controller.get("part_visibility"); visibility && visibility->isArray()) {
            for (const std::unique_ptr<json::Value>& entry : visibility->mArray) {
                for (const std::string& pattern : entry->mKeys) {
                    const json::Value& value = *entry->mObject.at(pattern);
                    EntityPartRule rule;
                    rule.pattern = lowercase(pattern);
                    if (value.mType == json::Value::Type::Boolean) {
                        rule.visible = molang::Script(value.mBoolean ? 1.0 : 0.0);
                    } else if (value.isString()) {
                        rule.visible = molang::Script::compile(value.mString, 1.0);
                    } else {
                        rule.visible = molang::Script(1.0);
                    }
                    parsed.parts.push_back(std::move(rule));
                }
            }
        }
        out[lowercase(name)] = std::move(parsed);
    }
}

}

namespace {

/**
 * Folds every rig the render controllers can pick into the combined rig,
 * joining bones that share a name, and notes which controller and rig each
 * quad belongs to.
 */
void combineRigs(EntityModel& model)
{
    std::map<std::string, uint16_t> boneByName;
    for (size_t controller = 0; controller < model.controllers.size(); ++controller) {
        for (uint32_t choice : model.controllers[controller].geometryChoices) {
            if (choice >= model.rigs.size()) {
                continue;
            }
            const EntityRig& rig = model.rigs[choice];
            std::vector<uint16_t> remap(rig.bones.size());
            for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                auto [found, added] = boneByName.try_emplace(lowercase(rig.bones[bone].name), static_cast<uint16_t>(model.combined.bones.size()));
                if (added) {
                    model.combined.bones.push_back(rig.bones[bone]);
                    model.combined.bones.back().parent = -1;
                }
                remap[bone] = found->second;
            }
            for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                EntityBone& merged = model.combined.bones[remap[bone]];
                if (merged.parent < 0 && rig.bones[bone].parent >= 0) {
                    merged.parent = remap[size_t(rig.bones[bone].parent)];
                }
            }
            for (size_t quad = 0; quad < rig.quads.size(); ++quad) {
                model.combined.quads.push_back(rig.quads[quad]);
                model.combined.quadBones.push_back(quad < rig.quadBones.size() && rig.quadBones[quad] < remap.size() ? remap[rig.quadBones[quad]] : uint16_t(0xFFFF));
                model.combinedSources.push_back({ static_cast<uint16_t>(controller), static_cast<uint16_t>(choice) });
            }
        }
    }
}

}

std::string skinGeometryName(const std::string& resourcePatch, const std::string& key)
{
    std::unique_ptr<json::Value> patch = json::parse(stripJsonComments(resourcePatch));
    const json::Value* geometry = patch ? patch->get("geometry") : nullptr;
    const json::Value* named = geometry ? geometry->get(key) : nullptr;
    return named && named->isString() ? named->mString : std::string();
}

std::shared_ptr<const EntityRig> buildSkinRig(const std::string& geometryData, const std::string& resourcePatch, const GeometryLibrary* catalog, const std::string& key)
{
    std::string name = skinGeometryName(resourcePatch, key);
    if (name.empty()) {
        return nullptr;
    }
    GeometryLibrary library;
    std::string_view data = geometryData;
    while (!data.empty() && std::isspace(static_cast<unsigned char>(data.front()))) {
        data.remove_prefix(1);
    }
    while (!data.empty() && std::isspace(static_cast<unsigned char>(data.back()))) {
        data.remove_suffix(1);
    }
    if (!data.empty() && data != "null") {
        library.parse(stripJsonComments(std::string(data)));
        library.resolveInheritance();
    }
    const Geometry* geometry = library.findIgnoringCase(name);
    if (!geometry && catalog) {
        geometry = catalog->findIgnoringCase(name);
    }
    if (!geometry) {
        return nullptr;
    }
    auto rig = std::make_shared<EntityRig>();
    if (geometry->textureSizeSet) {
        buildEntityRig(*geometry, *rig);
    } else {
        // Skin geometry often leaves the texture size out; the game lays those
        // UVs out on a 64 pixel skin whatever the image size, not on the 16 a
        // bare geometry file would get.
        Geometry sized = *geometry;
        sized.textureWidth = 64.0f;
        sized.textureHeight = 64.0f;
        buildEntityRig(sized, *rig);
    }
    if (rig->quads.empty()) {
        return nullptr;
    }
    return rig;
}

/**
 * Entity models from the client entity definitions of the vanilla pack and
 * the server packs: each definition's default geometry as a bone rig, its
 * default texture scaled into one entity texture layer and its scripts, plus
 * one rig per geometry and one layer per texture its render controllers can
 * select, and every animation and animation controller. The player also gets
 * the slim humanoid model for skins that ask for it, and the attachables of
 * server packs are built the same way for the items they belong to.
 */
void BlockAssets::buildEntityModels(PackSource& pack, const std::vector<std::shared_ptr<const PackFiles>>& packs)
{
    GeometryLibrary library;
    std::map<std::string, ClientEntity> definitions;
    std::map<std::string, ClientEntity> attachableDefinitions;
    std::unordered_map<std::string, RenderControllerSource> controllerSources;
    auto parseAnimations = [&](const std::string& text) {
        if (std::unique_ptr<json::Value> document = json::parse(stripJsonComments(text))) {
            animations.parse(*document);
        }
    };
    // Server packs come after, file by file, so a pack's player.animation.json
    // adds to the game's animations instead of hiding the whole file.
    PackSource nativeModels(pack.root());
    EntityMaterialBlendLibrary materialBlends;
    auto materialLayers = nativeModels.readTextLayers("materials/entity.material");
    for (auto layer = materialLayers.rbegin(); layer != materialLayers.rend(); ++layer) {
        if (auto document = json::parse(stripJsonComments(*layer))) {
            materialBlends.parse(*document);
        }
    }
    for (const char* archive : { "models", "models/entity" }) {
        for (const std::string& name : nativeModels.archiveEntries(archive)) {
            auto layers = nativeModels.readArchivedLayers(archive, name);
            for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) library.parse(stripJsonComments(*layer), true);
        }
    }
    library.expandVanillaModels();
    for (const std::string& name : pack.archiveEntries("entity")) {
        std::string text;
        if (pack.readBaseArchived("entity", name, text)) {
            readClientEntity(text, definitions);
        }
    }
    for (const std::string& name : pack.archiveEntries("attachables")) {
        std::string text;
        if (pack.readBaseArchived("attachables", name, text)) {
            readClientEntity(text, attachableDefinitions, "minecraft:attachable");
        }
    }
    for (const std::string& name : pack.archiveEntries("render_controllers")) {
        auto layers = nativeModels.readArchivedLayers("render_controllers", name);
        for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) readRenderControllers(*layer, controllerSources);
    }
    for (const char* archive : { "animations", "animation_controllers" }) {
        for (const std::string& name : pack.archiveEntries(archive)) {
            auto layers = nativeModels.readArchivedLayers(archive, name);
            for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) parseAnimations(*layer);
        }
    }
    std::error_code error;
    auto directory = pack.root().parent_path().parent_path() / "definitions" / "attachables";
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream file(entry.path(), std::ios::binary);
        if (!file) continue;
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        readClientEntity(text, attachableDefinitions, "minecraft:attachable");
    }
    if (!attachableDefinitions.contains("minecraft:trident")) {
        auto projectile = definitions.find("minecraft:thrown_trident");
        if (projectile != definitions.end()) {
            ClientEntity held = projectile->second;
            auto scripts = std::make_shared<EntityScripts>();
            scripts->aliases["wield"] = "controller.animation.trident.wield";
            for (const char* name : { "wield_first_person", "wield_first_person_raise", "wield_first_person_riptide", "wield_third_person", "wield_third_person_raise" }) {
                scripts->aliases[name] = std::string("animation.trident.") + name;
            }
            scripts->animate.emplace_back("wield", molang::Script {});
            held.scripts = std::move(scripts);
            attachableDefinitions.emplace("minecraft:trident", std::move(held));
        }
    }
    // The game draws the elytra itself: the packs only carry its geometry, texture and animations.
    if (!attachableDefinitions.contains("minecraft:elytra")) {
        ClientEntity worn;
        worn.geometry = "geometry.elytra";
        worn.texture = "textures/models/armor/elytra";
        worn.materials["default"] = "elytra";
        worn.textures["default"] = worn.texture;
        auto scripts = std::make_shared<EntityScripts>();
        scripts->aliases["elytra"] = "controller.animation.elytra.default";
        for (const char* state : { "default", "gliding", "sneaking", "sleeping", "swimming" }) {
            scripts->aliases[state] = std::string("animation.elytra.") + state;
        }
        scripts->animate.emplace_back("elytra", molang::Script {});
        worn.scripts = std::move(scripts);
        attachableDefinitions.emplace("minecraft:elytra", std::move(worn));
    }
    for (auto layer = packs.rbegin(); layer != packs.rend(); ++layer) {
        for (const auto& path : (*layer)->paths()) {
            if (!endsWith(path, ".json")) {
                continue;
            }
            auto bytes = (*layer)->find(path);
            if (!bytes) continue;
            const auto& content = *bytes;
            if (startsWith(path, "models/")) {
                library.parse(stripJsonComments(content));
            } else if (startsWith(path, "entity/")) {
                readClientEntity(content, definitions);
            } else if (startsWith(path, "attachables/")) {
                readClientEntity(content, attachableDefinitions, "minecraft:attachable");
            } else if (startsWith(path, "animations/") || startsWith(path, "animation_controllers/")) {
                parseAnimations(content);
            } else if (startsWith(path, "render_controllers/")) {
                readRenderControllers(content, controllerSources);
            }
        }
    }

    library.resolveInheritance();

    std::map<std::string, uint32_t> layerByTexture;
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> sizeByLayer;
    auto textureLayer = [&](const std::string& path) -> std::optional<uint32_t> {
        auto found = layerByTexture.find(path);
        if (found != layerByTexture.end()) {
            return found->second;
        }
        std::string encoded;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;
        if (!pack.readTexture(path, encoded) || !ui::decodeImage(encoded, width, height, rgba) || width == 0 || height == 0) {
            return std::nullopt;
        }
        uint32_t layer = entityTextureLayers();
        uint32_t tilesX = std::clamp<uint32_t>((width + EntityTextureSize - 1) / EntityTextureSize, 1, MaxEntityTiles);
        uint32_t tilesY = std::clamp<uint32_t>((height + EntityTextureSize - 1) / EntityTextureSize, 1, MaxEntityTiles);
        if (tilesX * tilesY == 1 && (path == "textures/misc/enchanted_item_glint" || path == "textures/misc/enchanted_actor_glint")) {
            for (uint32_t y = 0; y < EntityTextureSize; ++y) {
                for (uint32_t x = 0; x < EntityTextureSize; ++x) {
                    const uint8_t* texel = rgba.data() + (size_t(y % height) * width + x % width) * 4;
                    entityPixels.insert(entityPixels.end(), texel, texel + 4);
                }
            }
            entityTiles.emplace(layer, EntityTileGrid { 1, 1, float(width) / EntityTextureSize, float(height) / EntityTextureSize });
        } else if (tilesX * tilesY == 1) {
            std::vector<uint8_t> resized = resizeNearest(rgba, width, height, EntityTextureSize);
            entityPixels.insert(entityPixels.end(), resized.begin(), resized.end());
        } else {
            // Too big for one layer: spread it over a grid of layers, row by row.
            // Stretching an odd size like 1000x320 onto the grid would shift texel
            // edges, and packs that paint whole faces from one texel lose them.
            uint32_t spanX = tilesX * EntityTextureSize;
            uint32_t spanY = tilesY * EntityTextureSize;
            uint32_t usedX = std::min(width, spanX);
            uint32_t usedY = std::min(height, spanY);
            for (uint32_t tileY = 0; tileY < tilesY; ++tileY) {
                for (uint32_t tileX = 0; tileX < tilesX; ++tileX) {
                    for (uint32_t y = 0; y < EntityTextureSize; ++y) {
                        uint32_t gridY = tileY * EntityTextureSize + y;
                        for (uint32_t x = 0; x < EntityTextureSize; ++x) {
                            uint32_t gridX = tileX * EntityTextureSize + x;
                            uint8_t texel[4] = { 0, 0, 0, 0 };
                            if (gridX < usedX && gridY < usedY) {
                                averageArea(rgba, width, gridX * width / usedX, std::max(gridX * width / usedX + 1, (gridX + 1) * width / usedX),
                                    gridY * height / usedY, std::max(gridY * height / usedY + 1, (gridY + 1) * height / usedY), texel);
                            }
                            entityPixels.insert(entityPixels.end(), texel, texel + 4);
                        }
                    }
                }
            }
            entityTiles.emplace(layer, EntityTileGrid { tilesX, tilesY, float(usedX) / float(spanX), float(usedY) / float(spanY) });
        }
        layerByTexture.emplace(path, layer);
        sizeByLayer.emplace(layer, std::make_pair(width, height));
        return layer;
    };
    struct DecodedTexture {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;
    };
    std::map<std::string, DecodedTexture> meshPixels;
    auto pixelsOf = [&](const std::string& path) -> MeshTexture {
        auto found = meshPixels.find(path);
        if (found == meshPixels.end()) {
            DecodedTexture decoded;
            std::string encoded;
            if (!pack.readTexture(path, encoded) || !ui::decodeImage(encoded, decoded.width, decoded.height, decoded.rgba)) {
                decoded = DecodedTexture {};
            }
            found = meshPixels.emplace(path, std::move(decoded)).first;
        }
        const DecodedTexture& texture = found->second;
        return texture.rgba.empty() ? MeshTexture {} : MeshTexture { texture.width, texture.height, &texture.rgba };
    };
    auto modelOf = [&](const std::string& geometryName, const ClientEntity& definition, uint32_t layer) -> std::optional<EntityModel> {
        const Geometry* geometry = library.find(geometryName);
        if (!geometry) {
            return std::nullopt;
        }
        // Legacy geometry like geometry.humanoid.custom never says how big its
        // texture is; the game maps it onto the texture it gets, the 64x64 Steve.
        MeshTextures textures = [&](const std::string& name) {
            auto named = definition.textures.find(name);
            return named == definition.textures.end() ? MeshTexture {} : pixelsOf(named->second);
        };
        auto buildRig = [&](const Geometry& source, EntityRig& rig) {
            auto size = sizeByLayer.find(layer);
            if (source.textureSizeSet || size == sizeByLayer.end()) {
                buildEntityRig(source, rig, textures);
                return;
            }
            Geometry sized = source;
            sized.textureWidth = static_cast<float>(size->second.first);
            sized.textureHeight = static_cast<float>(size->second.second);
            buildEntityRig(sized, rig, textures);
        };
        EntityModel model;
        model.rigs.emplace_back();
        buildRig(*geometry, model.rigs.back());
        model.scripts = definition.scripts;
        model.layer = layer;
        std::map<std::string, uint32_t> rigByGeometry { { geometryName, 0 } };
        auto rigOf = [&](const std::string& choice) -> uint32_t {
            std::string key = choice.substr(std::string("geometry.").size());
            auto named = definition.geometries.find(key);
            if (named == definition.geometries.end()) {
                return NoEntityChoice;
            }
            const std::string& id = key == "default" ? geometryName : named->second;
            if (auto known = rigByGeometry.find(id); known != rigByGeometry.end()) {
                return known->second;
            }
            const Geometry* found = library.find(id);
            if (!found) {
                return NoEntityChoice;
            }
            uint32_t index = static_cast<uint32_t>(model.rigs.size());
            model.rigs.emplace_back();
            buildRig(*found, model.rigs.back());
            rigByGeometry.emplace(id, index);
            return index;
        };
        auto layerOf = [&](const std::string& choice) -> uint32_t {
            auto named = definition.textures.find(choice.substr(std::string("texture.").size()));
            if (named == definition.textures.end()) {
                return NoEntityChoice;
            }
            std::optional<uint32_t> found = textureLayer(named->second);
            return found ? *found : NoEntityChoice;
        };
        for (const auto& [name, condition] : definition.renderControllers) {
            auto source = controllerSources.find(name);
            if (source == controllerSources.end()) {
                continue;
            }
            EntityRenderController controller;
            if (!condition.empty()) {
                controller.condition = molang::Script::compile(condition, 1.0);
            }
            controller.geometry = source->second.geometry;
            controller.texture = source->second.texture;
            controller.extraTextures = source->second.extraTextures;
            controller.color = source->second.color;
            controller.overlay = source->second.overlay;
            controller.hurtColor = source->second.hurtColor;
            controller.fireColor = source->second.fireColor;
            controller.parts = source->second.parts;
            controller.ignoreLighting = source->second.ignoreLighting;
            controller.uvAnim = source->second.uvAnim;
            controller.uvAnimated = source->second.uvAnimated;
            for (const std::string& choice : source->second.geometryChoices) {
                controller.geometryChoices.push_back(rigOf(choice));
            }
            for (const std::string& choice : source->second.textureChoices) {
                controller.textureChoices.push_back(layerOf(choice));
            }
            for (size_t index = 0; index < 2; ++index) {
                for (const std::string& choice : source->second.extraTextureChoices[index]) controller.extraTextureChoices[index].push_back(layerOf(choice));
            }
            controller.materialSelector = source->second.material;
            for (const std::string& material : source->second.materialChoices) {
                EntityMaterialChoice choice;
                auto named = startsWith(material, "material.") ? definition.materials.find(material.substr(9)) : definition.materials.end();
                if (named != definition.materials.end()) {
                    choice.blend = materialBlends.find(named->second).value_or(blendOf(named->second));
                    choice.oneSided = named->second.find("one_sided") != std::string::npos;
                    choice.glint = materialBlends.glint(named->second).value_or(named->second.find("glint") != std::string::npos);
                    if (named->second == "ender_dragon" || materialBlends.emissive(named->second).value_or(false)) choice.material = EntityMaterial::Dragon;
                    else if (named->second == "ender_crystal") choice.material = EntityMaterial::AlphaTest;
                    else if (startsWith(named->second, "entity_dissolve_layer0")) choice.material = EntityMaterial::DissolveDepth;
                    else if (startsWith(named->second, "entity_dissolve_layer1")) choice.material = EntityMaterial::DissolveColor;
                    else if (named->second.find("change_color") != std::string::npos) choice.material = EntityMaterial::ColorMask;
                    else if (named->second == "wolf_armor") choice.material = EntityMaterial::DyedArmor;
                    else if (named->second == "horse" || named->second == "horse_leather_armor") choice.material = EntityMaterial::Horse;
                    else if (materialBlends.multitexture(named->second).value_or(named->second.find("multitexture") != std::string::npos)) choice.material = EntityMaterial::Multitexture;
                }
                controller.materialChoices.push_back(choice);
            }
            if (!controller.materialChoices.empty()) {
                controller.material = controller.materialChoices.front().material;
                controller.blend = controller.materialChoices.front().blend;
                controller.oneSided = controller.materialChoices.front().oneSided;
            }
            model.controllers.push_back(std::move(controller));
        }
        if (model.controllers.size() > 1) {
            combineRigs(model);
        }
        return model;
    };

    for (const auto& [identifier, definition] : definitions) {
        std::optional<uint32_t> layer = textureLayer(definition.texture);
        if (!layer) {
            continue;
        }
        if (std::optional<EntityModel> model = modelOf(definition.geometry, definition, *layer)) {
            entityModels.emplace(identifier, std::move(*model));
        }
        if (identifier == "minecraft:player") {
            if (std::optional<EntityModel> slim = modelOf("geometry.humanoid.customSlim", definition, *layer)) {
                entityModels.emplace(identifier + "#slim", std::move(*slim));
            }
        }
    }

    for (const auto& [identifier, definition] : attachableDefinitions) {
        std::optional<uint32_t> layer = textureLayer(definition.texture);
        if (!layer) {
            continue;
        }
        if (std::optional<EntityModel> model = modelOf(definition.geometry, definition, *layer)) {
            auto material = definition.materials.find("default");
            model->wearable = material != definition.materials.end()
                && (material->second == "armor" || material->second == "armor_leather"
                    || material->second == "elytra" || material->second == "wolf_armor");
            attachableModels.emplace(identifier, std::move(*model));
        }
    }

    auto wolf = entityModels.find("minecraft:wolf");
    auto wolfArmor = attachableModels.find("minecraft:wolf_armor");
    if (wolf != entityModels.end() && wolfArmor != attachableModels.end()) {
        EntityModel& wearer = wolf->second;
        const EntityModel& armor = wolfArmor->second;
        uint32_t firstRig = static_cast<uint32_t>(wearer.rigs.size());
        wearer.rigs.insert(wearer.rigs.end(), armor.rigs.begin(), armor.rigs.end());
        for (EntityRenderController controller : armor.controllers) {
            for (uint32_t& choice : controller.geometryChoices) {
                if (choice != NoEntityChoice) choice += firstRig;
            }
            controller.condition = molang::Script::compile("query.is_item_name_any('slot.armor.body', 'minecraft:wolf_armor')");
            for (size_t channel = 0; channel < 4; ++channel) {
                controller.color[channel] = molang::Script::compile("query.armor_color_slot(4, " + std::to_string(channel) + ")");
            }
            wearer.controllers.push_back(std::move(controller));
        }
        auto scripts = wearer.scripts ? std::make_shared<EntityScripts>(*wearer.scripts) : std::make_shared<EntityScripts>();
        if (armor.scripts) scripts->preAnimation.insert(scripts->preAnimation.end(), armor.scripts->preAnimation.begin(), armor.scripts->preAnimation.end());
        wearer.scripts = std::move(scripts);
        wearer.combined = {};
        wearer.combinedSources.clear();
        combineRigs(wearer);
    }

    static constexpr const char* ArmorGeometries[] = {
        "geometry.player.armor.helmet",
        "geometry.player.armor.chestplate",
        "geometry.player.armor.leggings",
        "geometry.player.armor.boots",
    };
    static constexpr const char* LegacyArmorGeometries[] = {
        "geometry.humanoid.armor.helmet",
        "geometry.humanoid.armor.chestplate",
        "geometry.humanoid.armor.leggings",
        "geometry.humanoid.armor.boots",
    };
    for (size_t slot = 0; slot < armorRigs.size(); ++slot) {
        const Geometry* geometry = library.find(ArmorGeometries[slot]);
        if (!geometry) {
            geometry = library.find(LegacyArmorGeometries[slot]);
        }
        if (geometry) {
            buildEntityRig(*geometry, armorRigs[slot]);
        }
    }
    if (const Geometry* geometry = library.find("geometry.cape")) {
        buildEntityRig(*geometry, cape);
        for (size_t index = 0; index < cape.quads.size(); ++index) {
            size_t bone = index < cape.quadBones.size() ? cape.quadBones[index] : cape.bones.size();
            if (bone >= cape.bones.size()) {
                continue;
            }
            for (std::array<int16_t, 3>& corner : cape.quads[index].positions) {
                std::array<float, 3> point { corner[0] / 16.0f, corner[1] / 16.0f, corner[2] / 16.0f };
                point = rotateEulerAround(point, cape.bones[bone].pivot, cape.bones[bone].rotation);
                for (int axis = 0; axis < 3; ++axis) {
                    corner[axis] = static_cast<int16_t>(std::lround(point[axis] * 16.0f));
                }
            }
        }
        for (EntityBone& bone : cape.bones) {
            bone.rotation = {};
        }
    }
    for (const char* material : { "leather", "chain", "iron", "gold", "diamond", "netherite", "copper", "turtle" }) {
        for (const char* suffix : { "_1", "_2" }) {
            std::string path = "textures/models/armor/" + std::string(material) + suffix;
            std::optional<uint32_t> layer = textureLayer(path);
            if (!layer) {
                continue;
            }
            armorLayers.emplace(path, *layer);
        }
    }
    armorGlintTexture = textureLayer("textures/misc/enchanted_actor_glint").value_or(NoEntityChoice);
    itemGlintTexture = textureLayer("textures/misc/enchanted_item_glint").value_or(NoEntityChoice);
    std::string beamImage;
    uint32_t beamWidth = 0, beamHeight = 0;
    std::vector<uint8_t> beamPixels;
    if (pack.readTexture("textures/entity/endercrystal/endercrystal_beam", beamImage)
        && ui::decodeImage(beamImage, beamWidth, beamHeight, beamPixels) && beamWidth && beamHeight) {
        // A repeating beam uses one layer so UV wrapping never crosses atlas tiles.
        auto resized = resizeNearest(beamPixels, beamWidth, beamHeight, EntityTextureSize);
        crystalBeamTexture = entityTextureLayers();
        entityPixels.insert(entityPixels.end(), resized.begin(), resized.end());
    }
    if (pack.readTexture("textures/entity/beacon_beam", beamImage)
        && ui::decodeImage(beamImage, beamWidth, beamHeight, beamPixels) && beamWidth && beamHeight) {
        auto resized = resizeNearest(beamPixels, beamWidth, beamHeight, EntityTextureSize);
        beaconBeamTexture = entityTextureLayers();
        entityPixels.insert(entityPixels.end(), resized.begin(), resized.end());
        for (size_t pixel = 3; pixel < resized.size(); pixel += 4) resized[pixel] = uint8_t(resized[pixel] / 8);
        beaconBeamShellTexture = entityTextureLayers();
        entityPixels.insert(entityPixels.end(), resized.begin(), resized.end());
    }
    geometries = std::make_shared<const GeometryLibrary>(std::move(library));
}

ArmorLook BlockAssets::armorLook(size_t slot, const std::string& identifier) const
{
    if (slot >= armorRigs.size() || armorRigs[slot].quads.empty()) {
        return {};
    }
    auto found = armorLayers.find(itemArmorTexture(identifier, slot));
    if (found == armorLayers.end()) {
        return {};
    }
    return { &armorRigs[slot], found->second };
}

}
