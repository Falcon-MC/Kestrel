#include "ui/Context.h"

#include "ui/DrawList.h"
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

void Context::nineSlice(const Rect& rect, std::string_view name, Color tint)
{
    const Sprite& source = art.sprite(name);
    BorderImage border;
    border.sprite = std::string(name);
    border.slice = source.slice;
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

void Context::textShadowed(std::string_view value, TextStyle style, float x, float y, Color color, Color shadow, float maxWidth)
{
    float offset = style == TextStyle::Pixel ? 1.0f : 1.0f / scale * std::max(1.0f, std::round(scale * theme::css(2.0f)));
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

float Context::paragraphHeight(std::string_view value, TextStyle style, float width) const
{
    std::vector<std::string_view> lines;
    return static_cast<float>(font.wrap(value, style, width, lines)) * font.lineHeight(style);
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
