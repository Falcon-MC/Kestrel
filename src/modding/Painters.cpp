#include "modding/Painters.h"

#include <algorithm>
#include <cmath>

namespace kestrel::modding {

namespace {

ui::TextStyle textStyle(mod::TextStyle style)
{
    switch (style) {
    case mod::TextStyle::Pixel:
        return ui::TextStyle::Pixel;
    case mod::TextStyle::Ui:
        return ui::TextStyle::Ui;
    case mod::TextStyle::UiSmall:
        return ui::TextStyle::UiSmall;
    case mod::TextStyle::UiLarge:
        return ui::TextStyle::UiLarge;
    case mod::TextStyle::Heading:
        return ui::TextStyle::Heading;
    }
    return ui::TextStyle::Pixel;
}

// The game darkens text to a quarter for its drop shadow.
mod::Color shadowOf(mod::Color color)
{
    return { static_cast<uint8_t>(color.r / 4), static_cast<uint8_t>(color.g / 4), static_cast<uint8_t>(color.b / 4), color.a };
}

// Blocks per font pixel on a name tag, which text3d matches at scale 1.
constexpr float FontPixelBlocks = 1.6f / 60.0f;

using Point = std::array<float, 3>;

/**
 * The first three columns of one row of a column major matrix, made unit
 * length. Rows 0 and 1 of a view projection point along the screen's right
 * and up in the world.
 */
Point unitRow(const std::array<float, 16>& matrix, size_t row)
{
    Point axis { matrix[row], matrix[4 + row], matrix[8 + row] };
    float length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (!(length > 0.0f)) {
        return {};
    }
    for (float& value : axis) {
        value /= length;
    }
    return axis;
}

Point relativeTo(const mod::Vec3& point, const mod::Vec3& eye)
{
    return { static_cast<float>(point.x - eye.x), static_cast<float>(point.y - eye.y), static_cast<float>(point.z - eye.z) };
}

Point cross(const Point& a, const Point& b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

}

UiCanvas::UiCanvas(ui::Context& context, ShaderStore& shaders, float width, float height)
    : context(context)
    , shaders(shaders)
    , canvasWidth(width)
    , canvasHeight(height)
{
}

float UiCanvas::width() const
{
    return canvasWidth;
}

float UiCanvas::height() const
{
    return canvasHeight;
}

void UiCanvas::fill(const mod::Rect& rect, mod::Color color)
{
    context.fill(rect, color);
}

void UiCanvas::outline(const mod::Rect& rect, mod::Color color, float thickness)
{
    context.outline(rect, color, thickness);
}

void UiCanvas::text(std::string_view value, float x, float y, mod::Color color, mod::TextStyle style, bool shadow)
{
    if (shadow) {
        context.textShadowed(value, textStyle(style), x, y, color, shadowOf(color));
    } else {
        context.text(value, textStyle(style), x, y, color);
    }
}

void UiCanvas::textCentered(std::string_view value, const mod::Rect& rect, mod::Color color, mod::TextStyle style)
{
    context.textCentered(value, textStyle(style), rect, color);
}

float UiCanvas::measure(std::string_view value, mod::TextStyle style) const
{
    return context.measure(value, textStyle(style));
}

float UiCanvas::lineHeight(mod::TextStyle style) const
{
    return context.lineHeight(textStyle(style));
}

void UiCanvas::sprite(const mod::Rect& rect, std::string_view name, mod::Color tint)
{
    context.sprite(rect, name, tint);
}

void UiCanvas::nineSlice(const mod::Rect& rect, std::string_view name, mod::Color tint)
{
    context.nineSlice(rect, name, tint);
}

void UiCanvas::setClip(const mod::Rect& rect)
{
    context.setClip(rect);
}

void UiCanvas::clearClip()
{
    context.clearClip();
}

void UiCanvas::shaderTriangles(const mod::Shader& shader, const std::vector<mod::ShaderVertex>& vertices, const mod::ShaderParams& params, bool aboveHud)
{
    scratch.clear();
    scratch.reserve(vertices.size());
    for (const mod::ShaderVertex& vertex : vertices) {
        scratch.push_back({ vertex.x, vertex.y, 0.0f, vertex.u, vertex.v, packColor(vertex.color) });
    }
    shaders.queue(aboveHud ? CustomLayer::AboveUi : CustomLayer::BelowUi, shader, scratch.data(), scratch.size(), params);
}

PostPasses::PostPasses(ShaderStore& shaders)
    : shaders(shaders)
{
}

bool PostPasses::supported() const
{
    return shaders.supportsPost();
}

void PostPasses::pass(const mod::Shader& shader, const mod::ShaderParams& params, bool keepInput)
{
    shaders.queuePost(shader, params, keepInput);
}

WorldCanvas::WorldCanvas(ShaderStore& shaders, const ui::Font* font, const mod::Vec3& camera, const std::array<float, 16>& viewProjection)
    : shaders(shaders)
    , font(font)
    , eye(camera)
    , right(unitRow(viewProjection, 0))
    , up(unitRow(viewProjection, 1))
{
}

mod::Vec3 WorldCanvas::camera() const
{
    return eye;
}

void WorldCanvas::triangles(const mod::Shader& shader, const std::vector<mod::WorldVertex>& vertices, const mod::ShaderParams& params)
{
    scratch.clear();
    scratch.reserve(vertices.size());
    for (const mod::WorldVertex& vertex : vertices) {
        scratch.push_back({ static_cast<float>(vertex.position.x - eye.x), static_cast<float>(vertex.position.y - eye.y), static_cast<float>(vertex.position.z - eye.z),
            vertex.u, vertex.v, packColor(vertex.color) });
    }
    shaders.queue(CustomLayer::World, shader, scratch.data(), scratch.size(), params);
}

void WorldCanvas::lines(const std::vector<std::pair<mod::Vec3, mod::Vec3>>& segments, mod::Color color, float width, bool throughWalls)
{
    if (!(width > 0.0f)) {
        return;
    }
    scratch.clear();
    scratch.reserve(segments.size() * 6);
    uint32_t packed = packColor(color);
    for (const auto& [from, to] : segments) {
        Point a = relativeTo(from, eye);
        Point b = relativeTo(to, eye);
        Point along { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        Point middle { (a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f };
        Point side = cross(along, middle);
        float length = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (!(length > 1e-6f) || !std::isfinite(length)) {
            continue;
        }
        float half = width * 0.5f / length;
        for (float& value : side) {
            value *= half;
        }
        const Point corners[4] = {
            { a[0] - side[0], a[1] - side[1], a[2] - side[2] },
            { a[0] + side[0], a[1] + side[1], a[2] + side[2] },
            { b[0] + side[0], b[1] + side[1], b[2] + side[2] },
            { b[0] - side[0], b[1] - side[1], b[2] - side[2] },
        };
        for (int corner : { 0, 1, 2, 0, 2, 3 }) {
            pushSolid(corners[corner][0], corners[corner][1], corners[corner][2], packed);
        }
    }
    queueSolid(throughWalls);
}

void WorldCanvas::wireBox(const mod::Vec3& min, const mod::Vec3& max, mod::Color color, float width, bool throughWalls)
{
    const mod::Vec3 c[8] = {
        { min.x, min.y, min.z }, { max.x, min.y, min.z }, { max.x, max.y, min.z }, { min.x, max.y, min.z },
        { min.x, min.y, max.z }, { max.x, min.y, max.z }, { max.x, max.y, max.z }, { min.x, max.y, max.z },
    };
    const int edges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    };
    std::vector<std::pair<mod::Vec3, mod::Vec3>> segments;
    segments.reserve(12);
    for (const auto& edge : edges) {
        segments.emplace_back(c[edge[0]], c[edge[1]]);
    }
    lines(segments, color, width, throughWalls);
}

void WorldCanvas::filledBox(const mod::Vec3& min, const mod::Vec3& max, mod::Color color, bool throughWalls)
{
    const Point c[8] = {
        relativeTo({ min.x, min.y, min.z }, eye), relativeTo({ max.x, min.y, min.z }, eye),
        relativeTo({ max.x, max.y, min.z }, eye), relativeTo({ min.x, max.y, min.z }, eye),
        relativeTo({ min.x, min.y, max.z }, eye), relativeTo({ max.x, min.y, max.z }, eye),
        relativeTo({ max.x, max.y, max.z }, eye), relativeTo({ min.x, max.y, max.z }, eye),
    };
    const int faces[6][4] = { { 0, 1, 2, 3 }, { 5, 4, 7, 6 }, { 4, 0, 3, 7 }, { 1, 5, 6, 2 }, { 3, 2, 6, 7 }, { 4, 5, 1, 0 } };
    scratch.clear();
    scratch.reserve(36);
    uint32_t packed = packColor(color);
    for (const auto& face : faces) {
        for (int corner : { 0, 1, 2, 0, 2, 3 }) {
            const Point& point = c[face[corner]];
            pushSolid(point[0], point[1], point[2], packed);
        }
    }
    queueSolid(throughWalls);
}

void WorldCanvas::text3d(const mod::Vec3& position, std::string_view text, mod::Color color, float scale, bool throughWalls)
{
    if (!font || text.empty() || !(scale > 0.0f) || !std::isfinite(scale)) {
        return;
    }
    label.reset(1.0f, font->whiteU(), font->whiteV());
    font->drawNameTag(label, text, color, false);
    const std::vector<ui::UiVertex>& vertices = label.vertices();
    if (vertices.size() < 4) {
        return;
    }
    auto [lowest, highest] = std::minmax_element(vertices.begin(), vertices.end(), [](const ui::UiVertex& a, const ui::UiVertex& b) {
        return a.y < b.y;
    });
    float middle = (lowest->y + highest->y) * 0.5f;
    float pixel = FontPixelBlocks * scale;
    Point center = relativeTo(position, eye);
    scratch.clear();
    scratch.reserve(vertices.size() / 4 * 6);
    for (size_t quad = 0; quad + 3 < vertices.size(); quad += 4) {
        for (int corner : { 0, 1, 2, 0, 2, 3 }) {
            const ui::UiVertex& vertex = vertices[quad + corner];
            float across = vertex.x * pixel;
            float down = (vertex.y - middle) * pixel;
            scratch.push_back({ center[0] + right[0] * across - up[0] * down, center[1] + right[1] * across - up[1] * down,
                center[2] + right[2] * across - up[2] * down, vertex.u, vertex.v, vertex.color });
        }
    }
    queueSolid(throughWalls);
}

void WorldCanvas::pushSolid(float x, float y, float z, uint32_t color)
{
    float u = font ? font->whiteU() : 0.0f;
    float v = font ? font->whiteV() : 0.0f;
    scratch.push_back({ x, y, z, u, v, color });
}

void WorldCanvas::queueSolid(bool throughWalls)
{
    shaders.queueBuiltin(CustomLayer::World, throughWalls ? CustomBuiltin::PrimitiveOverlay : CustomBuiltin::Primitive, scratch.data(), scratch.size());
}

}
