#include "ui/DrawList.h"
#include "world/Glint.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace kestrel::ui;

void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main()
{
    const std::array<std::array<float, 2>, 4> points { { { 0, 0 }, { 20, 0 }, { 20, 20 }, { 0, 20 } } };
    const std::array<std::array<float, 2>, 4> uvs { { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } };
    DrawList draw;
    draw.reset(1, 0, 0);
    draw.setOrigin(30, 40);
    draw.setLayer(2, 3, 0.5f);
    draw.setClip({ 5, 4, 10, 8 });
    draw.freeQuad(points, uvs, 0xffffffffu, 0.25f);
    require(!draw.indices().empty(), "A partially clipped model face must remain visible");
    double area = 0;
    for (const UiVertex& vertex : draw.vertices()) {
        require(vertex.x >= 37 && vertex.x <= 47 && vertex.y >= 47 && vertex.y <= 55, "Model faces must stay inside the translated preview clip");
        require(std::abs(vertex.u - (vertex.x - 32) / 20) < 1e-6f && std::abs(vertex.v - (vertex.y - 43) / 20) < 1e-6f, "Clipped faces must interpolate texture coordinates");
        require(vertex.depth == 0.25f && vertex.color == 0x80ffffffu, "Clipping must retain face depth and layer opacity");
    }
    const auto& vertices = draw.vertices();
    const auto& indices = draw.indices();
    for (size_t i = 0; i < indices.size(); i += 3) {
        const auto& a = vertices[indices[i]];
        const auto& b = vertices[indices[i + 1]];
        const auto& c = vertices[indices[i + 2]];
        area += std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * 0.5;
    }
    require(std::abs(area - 80) < 1e-6, "Clipped triangles must cover the preview without gaps or overlaps");
    draw.reset(1, 0, 0);
    draw.setOrigin(0, 0);
    draw.clearLayer();
    draw.setClip({ 5, 5, 10, 10 });
    const std::array<std::array<float, 2>, 4> rotated { { { 0, 10 }, { 10, 0 }, { 20, 10 }, { 10, 20 } } };
    draw.freeQuad(rotated, uvs, 0xffffffffu);
    require(!draw.indices().empty(), "A rotated face must survive clipping");
    for (const UiVertex& vertex : draw.vertices()) {
        require(vertex.x >= 5 && vertex.x <= 15 && vertex.y >= 5 && vertex.y <= 15, "Rotated face edges must stay inside the preview");
        require(std::abs(vertex.u - (vertex.x - vertex.y + 10) / 20) < 1e-6f && std::abs(vertex.v - (vertex.x + vertex.y - 10) / 20) < 1e-6f, "Rotated face clipping must preserve UV interpolation");
    }
    draw.reset(1, 0, 0);
    draw.setClip({ 25, 25, 10, 10 });
    draw.freeQuad(points, uvs, 0xffffffffu);
    require(draw.vertices().empty() && draw.indices().empty(), "Fully clipped model faces must emit nothing");
    draw.clearClip();
    draw.freeQuad(points, uvs, 0xffffffffu);
    require(draw.vertices().size() == 4 && draw.indices().size() == 6, "Unclipped model faces must retain the original quad path");
    draw.reset(1, 0, 0);
    draw.setClip({ 5, 5, 10, 10 });
    draw.freeQuad(rotated, uvs, 0xffffffffu);
    auto foil = kestrel::world::itemGlintParameters(1.0, 50.0f, 100.0f);
    draw.applyGlint(0, { 0, 0, 1, 1 }, { 0.5f, 0.5f, 0.75f, 0.75f }, foil);
    for (const UiVertex& vertex : draw.vertices()) {
        auto expected = kestrel::world::itemGlintUv(vertex.u, vertex.v, foil);
        require(vertex.glintUv == expected && vertex.glintStrength == 0.5f,
            "Clipped UI faces must preserve foil UV interpolation and strength");
    }
}
