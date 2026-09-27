#pragma once

#include "ui/Types.h"

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
};

class DrawList {
public:
    void reset(float scale, float whiteU, float whiteV);

    float scale() const
    {
        return pixelScale;
    }

    void fill(const Rect& logical, Color color);
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t color);

    // In pixels. Quads are cut down to it on the CPU, texture coordinates included.
    void setClip(const Rect& pixels)
    {
        clip = pixels;
    }

    void clearClip()
    {
        clip = {};
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

    std::vector<UiVertex> vertexData;
    std::vector<uint32_t> indexData;
    float pixelScale = 1.0f;
    float whiteU = 0.0f;
    float whiteV = 0.0f;
    Rect clip {};
};

}
