#pragma once

#include "world/BlockAssets.h"
#include "world/ServerPack.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kestrel::world {

using Vec3f = std::array<float, 3>;

struct GeometryFace {
    bool present = false;
    std::array<float, 2> uv {};
    std::array<float, 2> size {};
    std::string materialInstance;
};

struct GeometryCube {
    Vec3f origin {};
    Vec3f size {};
    Vec3f pivot {};
    Vec3f rotation {};
    float inflate = 0.0f;
    bool mirror = false;
    bool mirrorSet = false;
    bool boxUv = true;
    std::array<float, 2> uv {};
    std::array<GeometryFace, 6> faces {};
};

/**
 * A texture turned into a solid one texel thick, the way item geometry like
 * the bow's is written: the texture lies across x and z around local pivot,
 * is scaled and turned, and its pivot lands on position.
 */
struct GeometryTextureMesh {
    std::string texture;
    Vec3f position {};
    Vec3f localPivot {};
    Vec3f rotation {};
    Vec3f scale { 1.0f, 1.0f, 1.0f };
};

/**
 * One corner of an authored polygon: its position in model pixels, its normal
 * and its texture coordinate, v measured up from the bottom of the texture.
 */
struct GeometryPolyVertex {
    Vec3f position {};
    Vec3f normal {};
    std::array<float, 2> uv {};
};

/**
 * A bone's polygon mesh expanded into triangles, three corners each, with
 * UVs in texels or normalized to the texture when normalizedUvs is set.
 */
struct GeometryPolyMesh {
    bool normalizedUvs = false;
    std::vector<GeometryPolyVertex> triangles;
};

struct GeometryBone {
    std::string name;
    std::string parent;
    // An attachable bone with a binding hangs from the holder's bone, not its own parent.
    std::string binding;
    Vec3f pivot {};
    Vec3f rotation {};
    bool mirror = false;
    float inflate = 0.0f;
    bool neverRender = false;
    std::vector<GeometryCube> cubes;
    std::vector<GeometryTextureMesh> textureMeshes;
    GeometryPolyMesh polyMesh;
    // Which fields the file spelled out, so a legacy child geometry only
    // overrides those and keeps the rest of its parent's bone. reset drops
    // the parent's cubes.
    bool parentSet = false;
    bool pivotSet = false;
    bool rotationSet = false;
    bool mirrorSet = false;
    bool inflateSet = false;
    bool neverRenderSet = false;
    bool bindingSet = false;
    bool cubesSet = false;
    bool textureMeshesSet = false;
    bool polyMeshSet = false;
    bool reset = false;
};

/**
 * One Bedrock geometry: bones with pivots and rotations, and cubes with box or
 * per-face UVs in texture pixels.
 */
struct Geometry {
    float textureWidth = 16.0f;
    float textureHeight = 16.0f;
    bool textureSizeSet = false;
    std::vector<GeometryBone> bones;
};

struct BlockTransform {
    Vec3f rotation {};
    Vec3f translation {};
    Vec3f scale { 1.0f, 1.0f, 1.0f };
};

class GeometryLibrary {
public:
    void load(const std::vector<std::shared_ptr<const PackFiles>>& packs);
    void parse(const std::string& text);
    void resolveInheritance();
    const Geometry* find(const std::string& identifier) const;

    /**
     * The geometry of that identifier, the exact spelling first and then any
     * one differing only in letter case, as skins name their models.
     */
    const Geometry* findIgnoringCase(const std::string& identifier) const;
    const Geometry* first() const;

private:
    std::map<std::string, Geometry> byIdentifier;
    std::map<std::string, std::string> parents;
};

/**
 * Converts a geometry to block model quads. Sides are indexed West, East,
 * Down, Up, North, South in world space and the material callback receives
 * the face's material instance name (empty when unset) and its world side.
 */
std::vector<ModelQuad> buildGeometryQuads(const Geometry& geometry, const BlockTransform& transform, const std::function<uint32_t(const std::string&, int)>& material);

}
