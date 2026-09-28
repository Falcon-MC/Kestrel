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

struct GeometryBone {
    std::string name;
    std::string parent;
    Vec3f pivot {};
    Vec3f rotation {};
    bool mirror = false;
    float inflate = 0.0f;
    bool neverRender = false;
    std::vector<GeometryCube> cubes;
    // Which fields the file spelled out, so a legacy child geometry only
    // overrides those and keeps the rest of its parent's bone. reset drops
    // the parent's cubes.
    bool parentSet = false;
    bool pivotSet = false;
    bool rotationSet = false;
    bool mirrorSet = false;
    bool inflateSet = false;
    bool neverRenderSet = false;
    bool cubesSet = false;
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
