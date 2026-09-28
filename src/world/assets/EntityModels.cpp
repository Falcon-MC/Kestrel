#include "world/BlockAssets.h"

#include "TextureTools.h"
#include "Core/Json/Json.h"
#include "ui/Image.h"
#include "util/JsonText.h"
#include "util/Text.h"
#include "world/EntityAnimation.h"
#include "world/Geometry.h"
#include "world/ItemInfo.h"
#include "world/MolangScript.h"
#include "world/PackSource.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <optional>
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

void readClientEntity(const std::string& text, std::map<std::string, ClientEntity>& out)
{
    std::unique_ptr<json::Value> document = json::parse(stripJsonComments(text));
    const json::Value* entity = document ? document->get("minecraft:client_entity") : nullptr;
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
 * Turns a geometry into an animatable model. Geometry files mirror the x axis
 * and the x and y rotations, so pivots, cubes and rotations are flipped back
 * into world handedness; each cube is turned around its own pivot and its
 * quads stay unposed, tagged with their bone, with the box or per-face UV
 * unwrap over the whole texture.
 */
void buildEntityRig(const Geometry& geometry, EntityRig& model)
{
    std::map<std::string, int32_t> indexByName;
    for (const GeometryBone& bone : geometry.bones) {
        indexByName.emplace(lowercase(bone.name), static_cast<int32_t>(model.bones.size()));
        EntityBone rigBone;
        rigBone.name = bone.name;
        rigBone.pivot = { -bone.pivot[0], bone.pivot[1], bone.pivot[2] };
        rigBone.rotation = { -bone.rotation[0], -bone.rotation[1], bone.rotation[2] };
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
            bool mirror = cube.mirrorSet ? cube.mirror : bone.mirror;
            bool inverted = cube.size[0] < 0.0f || cube.size[1] < 0.0f || cube.size[2] < 0.0f;
            std::array<float, 3> cubeCenter = rotateEulerAround({ (min[0] + max[0]) * 0.5f, (min[1] + max[1]) * 0.5f, (min[2] + max[2]) * 0.5f }, cubePivot, cubeRotation);
            float x = cube.size[0];
            float y = cube.size[1];
            float z = cube.size[2];
            for (int face = 0; face < 6; ++face) {
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
                    for (int axis = 0; axis < 3; ++axis) {
                        quad.positions[corner][axis] = static_cast<int16_t>(std::lround(point[axis] * 16.0f));
                    }
                    const std::array<float, 2>& uv = uvs[corner];
                    quad.uvs[corner] = { static_cast<uint16_t>(std::clamp(uv[0], 0.0f, 15.0f) * 4096.0f), static_cast<uint16_t>(std::clamp(uv[1], 0.0f, 15.0f) * 4096.0f) };
                }
                quad.flags = FaceIds[face] | QuadTwoSided;
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
    std::vector<EntityPartRule> parts;
    std::string material;
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
        if (const json::Value* list = controller.get("materials"); list && list->isArray() && !list->mArray.empty()) {
            const json::Value& first = *list->mArray.front();
            if (!first.mKeys.empty()) {
                if (const json::Value* value = first.get(first.mKeys.front()); value && value->isString()) {
                    parsed.material = lowercase(value->mString);
                }
            }
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

std::shared_ptr<const EntityRig> buildSkinRig(const std::string& geometryData, const std::string& resourcePatch)
{
    if (geometryData.empty()) {
        return nullptr;
    }
    std::string name;
    if (std::unique_ptr<json::Value> patch = json::parse(stripJsonComments(resourcePatch))) {
        const json::Value* geometry = patch->get("geometry");
        const json::Value* fallback = geometry ? geometry->get("default") : nullptr;
        if (fallback && fallback->isString()) {
            name = fallback->mString;
        }
    }
    GeometryLibrary library;
    library.parse(stripJsonComments(geometryData));
    library.resolveInheritance();
    const Geometry* geometry = name.empty() ? nullptr : library.find(name);
    if (!geometry) {
        geometry = library.first();
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
 * the slim humanoid model for skins that ask for it.
 */
void BlockAssets::buildEntityModels(PackSource& pack, const std::vector<std::shared_ptr<const PackFiles>>& packs)
{
    GeometryLibrary library;
    std::map<std::string, ClientEntity> definitions;
    std::unordered_map<std::string, RenderControllerSource> controllerSources;
    auto parseAnimations = [&](const std::string& text) {
        if (std::unique_ptr<json::Value> document = json::parse(stripJsonComments(text))) {
            animations.parse(*document);
        }
    };
    // Server packs come after, file by file, so a pack's player.animation.json
    // adds to the game's animations instead of hiding the whole file.
    for (const char* archive : { "models", "models/entity" }) {
        for (const std::string& name : pack.archiveEntries(archive)) {
            std::string text;
            if (pack.readBaseArchived(archive, name, text)) {
                library.parse(stripJsonComments(text));
            }
        }
    }
    for (const std::string& name : pack.archiveEntries("entity")) {
        std::string text;
        if (pack.readBaseArchived("entity", name, text)) {
            readClientEntity(text, definitions);
        }
    }
    for (const std::string& name : pack.archiveEntries("render_controllers")) {
        std::string text;
        if (pack.readBaseArchived("render_controllers", name, text)) {
            readRenderControllers(text, controllerSources);
        }
    }
    for (const char* archive : { "animations", "animation_controllers" }) {
        for (const std::string& name : pack.archiveEntries(archive)) {
            std::string text;
            if (pack.readBaseArchived(archive, name, text)) {
                parseAnimations(text);
            }
        }
    }
    for (auto layer = packs.rbegin(); layer != packs.rend(); ++layer) {
        for (const auto& [path, content] : (*layer)->files) {
            if (!endsWith(path, ".json")) {
                continue;
            }
            if (startsWith(path, "models/")) {
                library.parse(stripJsonComments(content));
            } else if (startsWith(path, "entity/")) {
                readClientEntity(content, definitions);
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
        if (tilesX * tilesY == 1) {
            std::vector<uint8_t> resized = resizeNearest(rgba, width, height, EntityTextureSize);
            entityPixels.insert(entityPixels.end(), resized.begin(), resized.end());
        } else {
            // Too big for one layer: spread it over a grid of layers, row by row.
            uint32_t spanX = tilesX * EntityTextureSize;
            uint32_t spanY = tilesY * EntityTextureSize;
            for (uint32_t tileY = 0; tileY < tilesY; ++tileY) {
                for (uint32_t tileX = 0; tileX < tilesX; ++tileX) {
                    for (uint32_t y = 0; y < EntityTextureSize; ++y) {
                        uint32_t sourceY = (tileY * EntityTextureSize + y) * height / spanY;
                        for (uint32_t x = 0; x < EntityTextureSize; ++x) {
                            uint32_t sourceX = (tileX * EntityTextureSize + x) * width / spanX;
                            const uint8_t* texel = rgba.data() + (size_t(sourceY) * width + sourceX) * 4;
                            entityPixels.insert(entityPixels.end(), texel, texel + 4);
                        }
                    }
                }
            }
            entityTiles.emplace(layer, std::make_pair(tilesX, tilesY));
        }
        layerByTexture.emplace(path, layer);
        sizeByLayer.emplace(layer, std::make_pair(width, height));
        return layer;
    };
    auto modelOf = [&](const std::string& geometryName, const ClientEntity& definition, uint32_t layer) -> std::optional<EntityModel> {
        const Geometry* geometry = library.find(geometryName);
        if (!geometry) {
            return std::nullopt;
        }
        // Legacy geometry like geometry.humanoid.custom never says how big its
        // texture is; the game maps it onto the texture it gets, the 64x64 Steve.
        auto buildRig = [&](const Geometry& source, EntityRig& rig) {
            auto size = sizeByLayer.find(layer);
            if (source.textureSizeSet || size == sizeByLayer.end()) {
                buildEntityRig(source, rig);
                return;
            }
            Geometry sized = source;
            sized.textureWidth = static_cast<float>(size->second.first);
            sized.textureHeight = static_cast<float>(size->second.second);
            buildEntityRig(sized, rig);
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
            controller.parts = source->second.parts;
            for (const std::string& choice : source->second.geometryChoices) {
                controller.geometryChoices.push_back(rigOf(choice));
            }
            for (const std::string& choice : source->second.textureChoices) {
                controller.textureChoices.push_back(layerOf(choice));
            }
            const std::string& material = source->second.material;
            if (startsWith(material, "material.")) {
                if (auto named = definition.materials.find(material.substr(std::string("material.").size())); named != definition.materials.end()) {
                    controller.blend = blendOf(named->second);
                    controller.oneSided = named->second.find("one_sided") != std::string::npos;
                }
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

    static constexpr const char* ArmorGeometries[] = {
        "geometry.humanoid.armor.helmet",
        "geometry.humanoid.armor.chestplate",
        "geometry.humanoid.armor.leggings",
        "geometry.humanoid.armor.boots",
    };
    for (size_t slot = 0; slot < armorRigs.size(); ++slot) {
        if (const Geometry* geometry = library.find(ArmorGeometries[slot])) {
            buildEntityRig(*geometry, armorRigs[slot]);
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
            if (std::string_view(material) == "leather" && entityTileGrid(*layer) == std::make_pair(1u, 1u)) {
                size_t bytes = size_t(EntityTextureSize) * EntityTextureSize * 4;
                ui::applyDyeMask(std::span<uint8_t>(entityPixels).subspan(size_t(*layer) * bytes, bytes), ui::LeatherColor);
            }
        }
    }
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
