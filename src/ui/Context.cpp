#include "ui/Context.h"

#include "ui/DrawList.h"
#include "render/Renderer.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace kestrel::ui {

namespace {

uint64_t hashId(std::string_view id)
{
    uint64_t hash = 1469598103934665603ull;
    for (char c : id) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

}

Context::Context(DrawList& drawList, const Font& font, Skin& skin, const InputState& input, WidgetState& state, float scale)
    : drawList(drawList)
    , font(font)
    , art(skin)
    , in(input)
    , state(state)
    , scale(scale)
{
}

float Context::mouseX() const
{
    return in.mouseX / scale;
}

float Context::mouseY() const
{
    return in.mouseY / scale;
}

void Context::setClip(const Rect& rect)
{
    clip = rect;
    drawList.setClip({ std::round(rect.x * scale), std::round(rect.y * scale), std::round(rect.w * scale), std::round(rect.h * scale) });
}

void Context::clearClip()
{
    clip = {};
    drawList.clearClip();
}

bool Context::clipped(const Rect& rect) const
{
    (void)rect;
    return clip.w > 0.0f && !clip.contains(mouseX(), mouseY());
}

bool Context::hovered(const Rect& rect) const
{
    return !blocked && rect.contains(mouseX(), mouseY()) && !clipped(rect);
}

Interaction Context::interact(std::string_view id, const Rect& rect)
{
    interactive.push_back(rect);
    uint64_t key = hashId(id);
    Interaction result;
    result.hovered = hovered(rect);
    if (result.hovered && in.mousePressed) {
        state.active = key;
    }
    result.clicked = result.hovered && in.mouseReleased && state.active == key;
    if (result.clicked) {
        ++state.clicks;
    }
    result.pressed = result.hovered && in.mouseDown && state.active == key;
    return result;
}

void Context::fill(const Rect& rect, Color color)
{
    drawList.fill(rect, color);
}

void Context::outline(const Rect& rect, Color color, float thickness)
{
    fill({ rect.x, rect.y, rect.w, thickness }, color);
    fill({ rect.x, rect.bottom() - thickness, rect.w, thickness }, color);
    fill({ rect.x, rect.y + thickness, thickness, rect.h - thickness * 2.0f }, color);
    fill({ rect.right() - thickness, rect.y + thickness, thickness, rect.h - thickness * 2.0f }, color);
}

void Context::image(const Rect& rect, const ImageRef& source, Color tint)
{
    if (!source.valid) {
        return;
    }
    drawList.quad(std::round(rect.x * scale), std::round(rect.y * scale), std::round(rect.right() * scale), std::round(rect.bottom() * scale),
        source.u0, source.v0, source.u1, source.v1, tint.packed());
}

void Context::sprite(const Rect& rect, std::string_view name, Color tint)
{
    image(rect, art.sprite(name).image, tint);
}

void Context::spriteRegion(const Rect& rect, std::string_view name, const Rect& texels, Color tint)
{
    const Sprite& source = art.sprite(name);
    if (!source.valid) {
        return;
    }
    float du = (source.image.u1 - source.image.u0) / source.width;
    float dv = (source.image.v1 - source.image.v0) / source.height;
    ImageRef region { source.image.u0 + texels.x * du, source.image.v0 + texels.y * dv, source.image.u0 + texels.right() * du, source.image.v0 + texels.bottom() * dv, true };
    image(rect, region, tint);
}

void Context::setLayer(float offsetX, float offsetY, float opacity)
{
    drawList.setLayer(std::round(offsetX * scale), std::round(offsetY * scale), opacity);
}

void Context::clearLayer()
{
    drawList.clearLayer();
}

void Context::spriteQuad(const std::array<std::array<float, 2>, 4>& points, std::string_view name, const std::array<std::array<float, 2>, 4>& texels, Color tint)
{
    const Sprite& source = art.sprite(name);
    if (!source.valid || source.width <= 0.0f || source.height <= 0.0f) {
        return;
    }
    float du = (source.image.u1 - source.image.u0) / source.width;
    float dv = (source.image.v1 - source.image.v0) / source.height;
    std::array<std::array<float, 2>, 4> physical {};
    std::array<std::array<float, 2>, 4> uvs {};
    for (size_t corner = 0; corner < 4; ++corner) {
        physical[corner] = { points[corner][0] * scale, points[corner][1] * scale };
        uvs[corner] = { source.image.u0 + texels[corner][0] * du, source.image.v0 + texels[corner][1] * dv };
    }
    drawList.freeQuad(physical, uvs, tint.packed());
}

void Context::nineSlice(const Rect& rect, std::string_view name, Color tint)
{
    const Sprite& source = art.sprite(name);
    BorderImage border;
    border.sprite = std::string(name);
    border.slice = source.texels;
    border.width = source.slice;
    border.fill = true;
    border.valid = true;
    borderImage(rect, border, tint);
}

void Context::borderImage(const Rect& rect, const BorderImage& border, Color tint)
{
    const Sprite& source = art.sprite(border.sprite);
    if (!border.valid || !source.valid) {
        return;
    }
    Rect outer { rect.x - border.outset.left, rect.y - border.outset.top, rect.w + border.outset.left + border.outset.right, rect.h + border.outset.top + border.outset.bottom };
    float xs[4] = {
        std::round(outer.x * scale),
        std::round((outer.x + border.width.left) * scale),
        std::round((outer.right() - border.width.right) * scale),
        std::round(outer.right() * scale),
    };
    float ys[4] = {
        std::round(outer.y * scale),
        std::round((outer.y + border.width.top) * scale),
        std::round((outer.bottom() - border.width.bottom) * scale),
        std::round(outer.bottom() * scale),
    };
    float du = (source.image.u1 - source.image.u0) / source.width;
    float dv = (source.image.v1 - source.image.v0) / source.height;
    float us[4] = { source.image.u0, source.image.u0 + border.slice.left * du, source.image.u1 - border.slice.right * du, source.image.u1 };
    float vs[4] = { source.image.v0, source.image.v0 + border.slice.top * dv, source.image.v1 - border.slice.bottom * dv, source.image.v1 };
    uint32_t color = tint.packed();
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            if (row == 1 && column == 1 && !border.fill) {
                continue;
            }
            if (xs[column + 1] > xs[column] && ys[row + 1] > ys[row]) {
                drawList.quad(xs[column], ys[row], xs[column + 1], ys[row + 1], us[column], vs[row], us[column + 1], vs[row + 1], color);
            }
        }
    }
}

void Context::border(const Rect& rect, std::string_view component, std::string_view stateName, Color tint)
{
    borderImage(rect, art.border(component, stateName), tint);
}

float Context::measure(std::string_view text, TextStyle style) const
{
    return font.measure(text, style);
}

float Context::lineHeight(TextStyle style) const
{
    return font.lineHeight(style);
}

void Context::text(std::string_view value, TextStyle style, float x, float y, Color color, float maxWidth)
{
    font.draw(drawList, value, style, x, y, color, maxWidth);
}

float Context::shadowOffset(TextStyle style) const
{
    return style == TextStyle::Pixel ? 1.0f : 1.0f / scale * std::max(1.0f, std::round(scale * theme::css(2.0f)));
}

void Context::textShadowed(std::string_view value, TextStyle style, float x, float y, Color color, Color shadow, float maxWidth)
{
    float offset = shadowOffset(style);
    font.draw(drawList, value, style, x + offset, y + offset, shadow, maxWidth);
    font.draw(drawList, value, style, x, y, color, maxWidth);
}

void Context::textCentered(std::string_view value, TextStyle style, const Rect& rect, Color color)
{
    float width = std::min(measure(value, style), rect.w);
    float x = rect.x + (rect.w - width) * 0.5f;
    float y = rect.y + (rect.h - lineHeight(style)) * 0.5f;
    text(value, style, x, y, color, rect.w);
}

float Context::paragraph(std::string_view value, TextStyle style, float x, float y, float width, Color color)
{
    return font.drawWrapped(drawList, value, style, x, y, width, color);
}

void Context::pixelTextScaled(std::string_view value, float x, float y, float magnify, Color color, bool shadow)
{
    font.drawPixelScaled(drawList, value, x, y, magnify, color, shadow);
}

void Context::nameTag(std::string_view value, float x, float y, float magnify, float depth, bool sneaking)
{
    // Project a camera-facing plane without snapping its moving vertices to screen pixels.
    auto pass = [&](float z, uint8_t alpha, bool background) {
        DrawList label;
        label.reset(1.0f, font.whiteU(), font.whiteV());
        font.drawNameTag(label, value, { 255, 255, 255, alpha }, background);
        const auto& vertices = label.vertices();
        for (size_t i = 0; i + 3 < vertices.size(); i += 4) {
            std::array<std::array<float, 2>, 4> points {}, uvs {};
            for (size_t j = 0; j < 4; ++j) {
                const auto& vertex = vertices[i + j];
                points[j] = { (x + vertex.x * magnify) * scale, (y + vertex.y * magnify) * scale };
                uvs[j] = { vertex.u, vertex.v };
            }
            drawList.freeQuad(points, uvs, vertices[i].color, z);
        }
    };
    if (!sneaking) {
        // The faint pass sees through terrain, but stays behind the first-person hand.
        pass(HandDepthRange, 32, true);
    }
    pass(depth, sneaking ? 128 : 255, sneaking);
}

void Context::rotatedPixelText(std::string_view value, float centerX, float centerY, float magnify, float radians, Color color)
{
    DrawList textList;
    textList.reset(scale, font.whiteU(), font.whiteV());
    size_t lines = 1 + static_cast<size_t>(std::count(value.begin(), value.end(), '\n'));
    float y = -static_cast<float>(lines) * 9.0f * magnify * 0.5f;
    while (!value.empty()) {
        size_t end = value.find('\n');
        std::string_view line = value.substr(0, end);
        float x = -measure(line, TextStyle::Pixel) * magnify * 0.5f;
        font.drawPixelScaled(textList, line, x + magnify, y + magnify, magnify, color, true);
        font.drawPixelScaled(textList, line, x, y, magnify, color);
        y += 9.0f * magnify;
        if (end == std::string_view::npos) break;
        value.remove_prefix(end + 1);
    }
    float c = std::cos(radians), s = std::sin(radians);
    const auto& vertices = textList.vertices();
    for (size_t i = 0; i + 3 < vertices.size(); i += 4) {
        std::array<std::array<float, 2>, 4> points {}, uvs {};
        for (size_t j = 0; j < 4; ++j) {
            const auto& v = vertices[i + j];
            points[j] = { centerX * scale + v.x * c - v.y * s, centerY * scale + v.x * s + v.y * c };
            uvs[j] = { v.u, v.v };
        }
        drawList.freeQuad(points, uvs, vertices[i].color);
    }
}

float Context::paragraphShadowed(std::string_view value, TextStyle style, float x, float y, float width, Color color)
{
    return font.drawWrappedShadowed(drawList, value, style, x, y, width, color, shadowOffset(style));
}

float Context::paragraphHeight(std::string_view value, TextStyle style, float width) const
{
    std::vector<std::string_view> lines;
    return static_cast<float>(font.wrap(value, style, width, lines)) * font.lineHeight(style);
}

float Context::pixelParagraph(std::string_view value, float x, float y, float width, float magnify, Color color)
{
    return font.drawWrappedPixel(drawList, value, x, y, width, magnify, color);
}

size_t Context::wrap(std::string_view value, TextStyle style, float width, std::vector<std::string_view>& lines) const
{
    return font.wrap(value, style, width, lines);
}

bool Context::classicButton(std::string_view id, std::string_view label, const Rect& rect, bool enabled)
{
    Interaction interaction = enabled ? interact(id, rect) : Interaction {};
    fill(rect, { 19, 19, 19, 255 });
    const char* face = !enabled ? "ui/button_borderless_dark"
        : interaction.pressed ? "ui/button_borderless_lightpressed"
        : interaction.hovered ? "ui/button_borderless_lighthover"
                              : "ui/button_borderless_light";
    nineSlice(rect.inset(1.0f), face);
    Color ink = !enabled ? Color { 140, 140, 140, 255 } : interaction.hovered ? theme::ButtonTextHover : theme::ButtonText;
    float y = rect.y + std::floor((rect.h - 8.0f) * 0.5f) + (interaction.pressed ? 1.0f : 0.0f);
    float width = measure(label, TextStyle::Pixel);
    text(label, TextStyle::Pixel, rect.x + std::floor((rect.w - width) * 0.5f), y, ink, rect.w - 4.0f);
    return interaction.clicked;
}

Interaction Context::pressable(std::string_view id, std::string_view component, const Rect& rect, bool enabled, bool selected)
{
    Interaction interaction = enabled ? interact(id, rect) : Interaction {};
    const char* stateName = !enabled ? "Disabled" : (interaction.pressed || selected) ? "Pressed" : interaction.hovered ? "Hovered" : "Default";
    border(rect, component, stateName);
    return interaction;
}

bool Context::pressableButton(std::string_view id, std::string_view component, std::string_view label, const Rect& rect, TextStyle style, bool enabled)
{
    Interaction interaction = pressable(id, component, rect, enabled);
    bool light = component.find("Secondary") != std::string_view::npos || component.find("Neutral") != std::string_view::npos;
    Color ink = !enabled ? theme::Disabled : light ? Color { 0x1e, 0x1e, 0x1f, 255 } : theme::White;
    Rect face = rect;
    if (!interaction.pressed) {
        face.h -= theme::css(4.0f);
    }
    textCentered(label, style, face, ink);
    return interaction.clicked;
}

void Context::endFrame()
{
    if (!in.mouseDown) {
        state.active = 0;
    }
}

}
