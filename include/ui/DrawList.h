#pragma once

#include "ui/Types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kestrel::ui {

struct UiVertex {
    float x;
    float y;
    float u;
    float v;
    uint32_t color;
    float localX;
    float localY;
    float halfWidth;
    float halfHeight;
    float radius;
    float softness;
    float depth = 0.0f;
    std::array<float, 4> glintUv {};
    std::array<float, 4> glintRegion {};
    float glintStrength = 0.0f;
};

static_assert(offsetof(UiVertex, depth) == 44);
static_assert(sizeof(UiVertex) == 84);

class DrawList {
public:
    void reset(float scale, float whiteU, float whiteV);

    float scale() const
    {
        return pixelScale;
    }

    void fill(const Rect& logical, Color color);
    void applyGlint(size_t first, const std::array<float, 4>& baseRegion, const std::array<float, 4>& glintRegion,
        const std::array<float, 4>& parameters, float scaleU = 0.5f, float scaleV = 0.5f);
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t color, float topShift = 0.0f, float bottomShift = 0.0f);
    void freeQuad(const std::array<std::array<float, 2>, 4>& points, const std::array<std::array<float, 2>, 4>& uvs, uint32_t color, float depth = 0.0f);

    // In pixels, moving with the layer. Quads are cut down to it on the CPU, texture coordinates included.
    void setClip(const Rect& pixels)
    {
        clip = pixels;
    }

    void clearClip()
    {
        clip = {};
    }

    // Where the pixel (0, 0) of everything drawn afterwards lands, clip included.
    void setOrigin(float x, float y)
    {
        originX = x;
        originY = y;
    }

    // Shifts everything drawn afterwards by pixels and fades it, for screen transitions.
    void setLayer(float offsetX, float offsetY, float opacity)
    {
        layerX = offsetX;
        layerY = offsetY;
        layerOpacity = opacity;
    }

    void clearLayer()
    {
        layerX = 0.0f;
        layerY = 0.0f;
        layerOpacity = 1.0f;
    }

    const std::vector<UiVertex>& vertices() const
    {
        return vertexData;
    }

    const std::vector<uint32_t>& indices() const
    {
        return indexData;
    }

private:
    void pushIndices(uint32_t base);
    uint32_t layered(uint32_t color) const;

    std::vector<UiVertex> vertexData;
    std::vector<uint32_t> indexData;
    float pixelScale = 1.0f;
    float whiteU = 0.0f;
    float whiteV = 0.0f;
    Rect clip {};
    float originX = 0.0f;
    float originY = 0.0f;
    float layerX = 0.0f;
    float layerY = 0.0f;
    float layerOpacity = 1.0f;
};

}
