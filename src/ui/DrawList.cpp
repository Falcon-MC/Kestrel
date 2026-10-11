#include "ui/DrawList.h"
#include "world/Glint.h"

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

void DrawList::applyGlint(size_t first, const std::array<float, 4>& base, const std::array<float, 4>& region,
    const std::array<float, 4>& parameters, float scaleU, float scaleV)
{
    if (base[2] <= base[0] || base[3] <= base[1]) return;
    for (size_t index = first; index < vertexData.size(); ++index) {
        auto& vertex = vertexData[index];
        float u = (vertex.u - base[0]) / (base[2] - base[0]);
        float v = (vertex.v - base[1]) / (base[3] - base[1]);
        vertex.glintUv = world::itemGlintUv(u, v, parameters, scaleU, scaleV);
        vertex.glintRegion = region;
        vertex.glintStrength = parameters[2];
    }
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
    float shiftX = originX + layerX;
    float shiftY = originY + layerY;
    x0 += shiftX;
    x1 += shiftX;
    y0 += shiftY;
    y1 += shiftY;
    color = layered(color);
    if (clip.w > 0.0f) {
        float cx0 = std::max(x0, clip.x + shiftX);
        float cy0 = std::max(y0, clip.y + shiftY);
        float cx1 = std::min(x1, clip.right() + shiftX);
        float cy1 = std::min(y1, clip.bottom() + shiftY);
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
    uint32_t tinted = layered(color);
    if (clip.w > 0.0f && std::any_of(points.begin(), points.end(), [&](const auto& point) {
            return point[0] < clip.x || point[0] > clip.right() || point[1] < clip.y || point[1] > clip.bottom();
        })) {
        // Clip each original triangle separately to preserve its UV interpolation.
        using Vertex = std::array<float, 4>;
        const float edges[] = { clip.x, clip.right(), clip.y, clip.bottom() };
        for (const std::array<size_t, 3>& triangle : { std::array<size_t, 3> { 0, 1, 2 }, std::array<size_t, 3> { 0, 2, 3 } }) {
            std::array<Vertex, 8> polygon {};
            size_t count = triangle.size();
            for (size_t i = 0; i < count; ++i) {
                size_t corner = triangle[i];
                polygon[i] = { points[corner][0], points[corner][1], uvs[corner][0], uvs[corner][1] };
            }
            for (size_t edge = 0; edge < 4 && count > 0; ++edge) {
                size_t axis = edge / 2;
                auto inside = [&](const Vertex& vertex) {
                    return edge % 2 == 0 ? vertex[axis] >= edges[edge] : vertex[axis] <= edges[edge];
                };
                std::array<Vertex, 8> output {};
                size_t size = 0;
                Vertex previous = polygon[count - 1];
                bool previousInside = inside(previous);
                for (size_t i = 0; i < count; ++i) {
                    const Vertex& current = polygon[i];
                    bool currentInside = inside(current);
                    if (previousInside != currentInside) {
                        float t = (edges[edge] - previous[axis]) / (current[axis] - previous[axis]);
                        Vertex intersection {};
                        for (size_t component = 0; component < intersection.size(); ++component) {
                            intersection[component] = previous[component] + t * (current[component] - previous[component]);
                        }
                        intersection[axis] = edges[edge];
                        output[size++] = intersection;
                    }
                    if (currentInside) output[size++] = current;
                    previous = current;
                    previousInside = currentInside;
                }
                polygon = output;
                count = size;
            }
            if (count < 3) continue;
            uint32_t base = static_cast<uint32_t>(vertexData.size());
            for (size_t i = 0; i < count; ++i) {
                const Vertex& vertex = polygon[i];
                vertexData.push_back({ vertex[0] + originX + layerX, vertex[1] + originY + layerY, vertex[2], vertex[3], tinted, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, depth });
            }
            for (uint32_t i = 1; i + 1 < count; ++i) indexData.insert(indexData.end(), { base, base + i, base + i + 1 });
        }
        return;
    }
    uint32_t base = static_cast<uint32_t>(vertexData.size());
    for (size_t corner = 0; corner < 4; ++corner) {
        vertexData.push_back({ points[corner][0] + originX + layerX, points[corner][1] + originY + layerY, uvs[corner][0], uvs[corner][1], tinted, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, depth });
    }
    pushIndices(base);
}

void DrawList::pushIndices(uint32_t base)
{
    indexData.insert(indexData.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
}

}
