#include "ui/DrawList.h"

#include <algorithm>
#include <cmath>

namespace kestrel::ui {

void DrawList::reset(float scale, float u, float v)
{
    vertexData.clear();
    indexData.clear();
    pixelScale = scale;
    whiteU = u;
    whiteV = v;
}

void DrawList::fill(const Rect& logical, Color color)
{
    float x0 = std::round(logical.x * pixelScale);
    float y0 = std::round(logical.y * pixelScale);
    float x1 = std::round(logical.right() * pixelScale);
    float y1 = std::round(logical.bottom() * pixelScale);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    quad(x0, y0, x1, y1, whiteU, whiteV, whiteU, whiteV, color.packed());
}

void DrawList::shape(const Rect& logical, Color top, Color bottom, float radius, float softness)
{
    if (logical.w <= 0.0f || logical.h <= 0.0f) {
        return;
    }

    float halfWidth = logical.w * pixelScale * 0.5f;
    float halfHeight = logical.h * pixelScale * 0.5f;
    float centerX = logical.x * pixelScale + halfWidth;
    float centerY = logical.y * pixelScale + halfHeight;
    float r = std::min(radius * pixelScale, std::min(halfWidth, halfHeight));
    float soft = std::max(softness * pixelScale, 1.0f);
    float expand = soft + 1.0f;

    float left = -halfWidth - expand;
    float right = halfWidth + expand;
    float up = -halfHeight - expand;
    float down = halfHeight + expand;
    uint32_t topColor = top.packed();
    uint32_t bottomColor = bottom.packed();

    uint32_t base = static_cast<uint32_t>(vertexData.size());
    vertexData.push_back({ centerX + left, centerY + up, whiteU, whiteV, topColor, left, up, halfWidth, halfHeight, r, soft });
    vertexData.push_back({ centerX + right, centerY + up, whiteU, whiteV, topColor, right, up, halfWidth, halfHeight, r, soft });
    vertexData.push_back({ centerX + right, centerY + down, whiteU, whiteV, bottomColor, right, down, halfWidth, halfHeight, r, soft });
    vertexData.push_back({ centerX + left, centerY + down, whiteU, whiteV, bottomColor, left, down, halfWidth, halfHeight, r, soft });
    pushIndices(base);
}

void DrawList::image(const Rect& logical, float u0, float v0, float u1, float v1, float radius)
{
    if (logical.w <= 0.0f || logical.h <= 0.0f) {
        return;
    }

    float halfWidth = logical.w * pixelScale * 0.5f;
    float halfHeight = logical.h * pixelScale * 0.5f;
    float centerX = logical.x * pixelScale + halfWidth;
    float centerY = logical.y * pixelScale + halfHeight;
    float r = std::min(radius * pixelScale, std::min(halfWidth, halfHeight));
    uint32_t white = Color { 255, 255, 255, 255 }.packed();

    uint32_t base = static_cast<uint32_t>(vertexData.size());
    vertexData.push_back({ centerX - halfWidth, centerY - halfHeight, u0, v0, white, -halfWidth, -halfHeight, halfWidth, halfHeight, r, 1.0f });
    vertexData.push_back({ centerX + halfWidth, centerY - halfHeight, u1, v0, white, halfWidth, -halfHeight, halfWidth, halfHeight, r, 1.0f });
    vertexData.push_back({ centerX + halfWidth, centerY + halfHeight, u1, v1, white, halfWidth, halfHeight, halfWidth, halfHeight, r, 1.0f });
    vertexData.push_back({ centerX - halfWidth, centerY + halfHeight, u0, v1, white, -halfWidth, halfHeight, halfWidth, halfHeight, r, 1.0f });
    pushIndices(base);
}

void DrawList::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t color)
{
    uint32_t base = static_cast<uint32_t>(vertexData.size());
    vertexData.push_back({ x0, y0, u0, v0, color, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f });
    vertexData.push_back({ x1, y0, u1, v0, color, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f });
    vertexData.push_back({ x1, y1, u1, v1, color, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f });
    vertexData.push_back({ x0, y1, u0, v1, color, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f });
    pushIndices(base);
}

void DrawList::pushIndices(uint32_t base)
{
    indexData.insert(indexData.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
}

}
