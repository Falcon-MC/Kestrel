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

uint32_t DrawList::layered(uint32_t color) const
{
    if (layerOpacity >= 1.0f) {
        return color;
    }
    uint32_t alpha = static_cast<uint32_t>(std::lround(static_cast<float>(color >> 24) * std::clamp(layerOpacity, 0.0f, 1.0f)));
    return (color & 0x00FFFFFFu) | (alpha << 24);
}

void DrawList::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t color, float topShift, float bottomShift)
{
    x0 += layerX;
    x1 += layerX;
    y0 += layerY;
    y1 += layerY;
    color = layered(color);
    if (clip.w > 0.0f) {
        float cx0 = std::max(x0, clip.x);
        float cy0 = std::max(y0, clip.y);
        float cx1 = std::min(x1, clip.right());
        float cy1 = std::min(y1, clip.bottom());
        if (cx1 <= cx0 || cy1 <= cy0) {
            return;
        }
        float du = (u1 - u0) / (x1 - x0);
        float dv = (v1 - v0) / (y1 - y0);
        float nu0 = u0 + (cx0 - x0) * du;
        float nu1 = u1 - (x1 - cx1) * du;
        float nv0 = v0 + (cy0 - y0) * dv;
        float nv1 = v1 - (y1 - cy1) * dv;
        x0 = cx0;
        y0 = cy0;
        x1 = cx1;
        y1 = cy1;
        u0 = nu0;
        u1 = nu1;
        v0 = nv0;
        v1 = nv1;
    }
    uint32_t base = static_cast<uint32_t>(vertexData.size());
    vertexData.push_back({ x0 + topShift, y0, u0, v0, color, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f });
    vertexData.push_back({ x1 + topShift, y0, u1, v0, color, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f });
    vertexData.push_back({ x1 + bottomShift, y1, u1, v1, color, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f });
    vertexData.push_back({ x0 + bottomShift, y1, u0, v1, color, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f });
    pushIndices(base);
}

void DrawList::freeQuad(const std::array<std::array<float, 2>, 4>& points, const std::array<std::array<float, 2>, 4>& uvs, uint32_t color, float depth)
{
    uint32_t base = static_cast<uint32_t>(vertexData.size());
    uint32_t tinted = layered(color);
    for (size_t corner = 0; corner < 4; ++corner) {
        vertexData.push_back({ points[corner][0] + layerX, points[corner][1] + layerY, uvs[corner][0], uvs[corner][1], tinted, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, depth });
    }
    pushIndices(base);
}

void DrawList::pushIndices(uint32_t base)
{
    indexData.insert(indexData.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
}

}
