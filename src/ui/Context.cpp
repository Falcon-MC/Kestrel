#include "ui/Context.h"

#include "ui/DrawList.h"
#include "ui/Theme.h"
#include "ui/Utf8.h"

#include <algorithm>
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

Context::Context(DrawList& drawList, const Font& font, const InputState& input, WidgetState& state, float scale)
    : drawList(drawList)
    , font(font)
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

bool Context::hovered(const Rect& rect) const
{
    return !blocked && rect.contains(mouseX(), mouseY()) && !excluded.contains(mouseX(), mouseY());
}

Interaction Context::interact(std::string_view id, const Rect& rect)
{
    if (excluded.w <= 0.0f || rect.y >= excluded.bottom()) {
        interactive.push_back(rect);
    }
    uint64_t key = hashId(id);
    Interaction result;
    result.hovered = hovered(rect);
    if (result.hovered) {
        wantedCursor = Cursor::Hand;
    }
    if (result.hovered && in.mousePressed) {
        state.active = key;
    }
    result.clicked = result.hovered && in.mouseReleased && state.active == key;
    result.pressed = result.hovered && in.mouseDown && state.active == key;
    return result;
}

void Context::fill(const Rect& rect, Color color, float radius)
{
    if (radius <= 0.0f) {
        drawList.fill(rect, color);
        return;
    }
    drawList.shape(rect, color, color, radius, 1.0f);
}

void Context::gradient(const Rect& rect, Color top, Color bottom, float radius)
{
    drawList.shape(rect, top, bottom, radius, 1.0f);
}

void Context::shadow(const Rect& rect, float radius, float blur, Color color)
{
    drawList.shape(rect, color, color, radius, blur);
}

void Context::glow(float x, float y, float size, Color color)
{
    drawList.shape({ x - size * 0.25f, y - size * 0.25f, size * 0.5f, size * 0.5f }, color, color, size * 0.25f, size);
}

void Context::card(const Rect& rect, Color background, Color border, float radius)
{
    fill(rect, border, radius);
    fill(rect.inset(1.0f), background, std::max(radius - 1.0f, 0.0f));
}

void Context::outline(const Rect& rect, Color color)
{
    fill({ rect.x, rect.y, rect.w, 1.0f }, color);
    fill({ rect.x, rect.bottom() - 1.0f, rect.w, 1.0f }, color);
    fill({ rect.x, rect.y + 1.0f, 1.0f, rect.h - 2.0f }, color);
    fill({ rect.right() - 1.0f, rect.y + 1.0f, 1.0f, rect.h - 2.0f }, color);
}

void Context::image(const Rect& rect, const ImageRef& source, float radius)
{
    drawList.image(rect, source.u0, source.v0, source.u1, source.v1, radius);
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

bool Context::button(std::string_view id, std::string_view label, const Rect& rect, ButtonKind kind, bool enabled)
{
    Interaction state = enabled ? interact(id, rect) : Interaction {};
    constexpr float radius = 10.0f;
    Rect body = state.pressed ? Rect { rect.x, rect.y + 1.0f, rect.w, rect.h } : rect;

    if (!enabled) {
        if (kind != ButtonKind::Ghost) {
            card(rect, theme::SurfaceAlt, theme::Line, radius);
        }
        textCentered(label, TextStyle::Label, rect.inset(8.0f), theme::Subtle);
        return false;
    }

    switch (kind) {
    case ButtonKind::Primary:
        shadow({ body.x, body.y + 6.0f, body.w, body.h }, radius, state.hovered ? 22.0f : 16.0f, theme::AccentGlow);
        gradient(body, state.hovered ? theme::AccentHover : theme::Accent, state.pressed ? theme::AccentPressed : theme::AccentDeep, radius);
        textCentered(label, TextStyle::Label, body.inset(8.0f), theme::OnAccent);
        break;
    case ButtonKind::Danger:
        gradient(body, state.hovered ? theme::DangerHover : theme::Danger, theme::DangerDeep, radius);
        textCentered(label, TextStyle::Label, body.inset(8.0f), theme::OnAccent);
        break;
    case ButtonKind::Ghost:
        if (state.hovered || state.pressed) {
            fill(body, state.pressed ? theme::Raised : theme::GhostHover, radius);
        }
        textCentered(label, TextStyle::Label, body.inset(8.0f), state.hovered ? theme::Text : theme::Muted);
        break;
    case ButtonKind::Secondary:
        card(body, state.pressed ? theme::Raised : state.hovered ? theme::Hover : theme::SurfaceAlt, state.hovered ? theme::LineStrong : theme::Line, radius);
        textCentered(label, TextStyle::Label, body.inset(8.0f), theme::Text);
        break;
    }
    return state.clicked;
}

bool Context::tab(std::string_view id, std::string_view label, const Rect& rect, bool active)
{
    Interaction state = interact(id, rect);
    Rect pill = rect.inset(0.0f);
    if (active) {
        card(pill, theme::AccentSoft, theme::AccentLine, pill.h * 0.5f);
    } else if (state.hovered) {
        fill(pill, theme::GhostHover, pill.h * 0.5f);
    }
    textCentered(label, TextStyle::Label, rect, active ? theme::Accent : state.hovered ? theme::Text : theme::Muted);
    return state.clicked;
}

bool Context::field(std::string_view id, std::string_view placeholder, std::string_view value, const Rect& rect, bool focused)
{
    Interaction state = interact(id, rect);
    if (state.hovered) {
        wantedCursor = Cursor::Text;
    }
    constexpr float radius = 10.0f;
    if (focused) {
        shadow(rect, radius, 14.0f, theme::AccentGlow);
    }
    card(rect, focused ? theme::FieldFocused : theme::Field, focused ? theme::Accent : state.hovered ? theme::LineStrong : theme::Line, radius);

    float inner = rect.w - 32.0f;
    float y = rect.y + (rect.h - lineHeight(TextStyle::Body)) * 0.5f;
    if (value.empty() && !focused) {
        text(placeholder, TextStyle::Body, rect.x + 16.0f, y, theme::Subtle, inner);
        return state.clicked;
    }

    std::string shown(value);
    while (!shown.empty() && measure(shown, TextStyle::Body) > inner - 4.0f) {
        dropFirstUtf8(shown);
    }
    text(shown, TextStyle::Body, rect.x + 16.0f, y, theme::Text);
    if (focused) {
        float caret = rect.x + 16.0f + measure(shown, TextStyle::Body) + 1.0f;
        fill({ caret, rect.y + rect.h * 0.25f, 2.0f, rect.h * 0.5f }, theme::Accent, 1.0f);
    }
    return state.clicked;
}

bool Context::toggle(std::string_view id, const Rect& rect, bool on)
{
    Interaction state = interact(id, rect);
    Rect box { rect.x + (rect.w - 18.0f) * 0.5f, rect.y + (rect.h - 18.0f) * 0.5f, 18.0f, 18.0f };
    if (on) {
        shadow(box, 6.0f, 10.0f, theme::AccentGlow);
        gradient(box, theme::AccentHover, theme::AccentDeep, 6.0f);
        fill(box.inset(6.0f), theme::OnAccent, 2.0f);
    } else {
        card(box, state.hovered ? theme::Hover : theme::Field, state.hovered ? theme::Accent : theme::LineStrong, 6.0f);
    }
    return state.clicked;
}

void Context::endFrame()
{
    if (!in.mouseDown) {
        state.active = 0;
    }
}

}
