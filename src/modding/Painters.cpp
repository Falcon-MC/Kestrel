#include "Painters.h"

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

WorldCanvas::WorldCanvas(ShaderStore& shaders, const mod::Vec3& camera)
    : shaders(shaders)
    , eye(camera)
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

}
