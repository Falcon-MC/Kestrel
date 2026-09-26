#pragma once

#include "platform/Input.h"
#include "platform/Window.h"
#include "ui/Font.h"
#include "ui/Image.h"
#include "ui/Types.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace kestrel::ui {

class DrawList;

enum class ButtonKind {
    Secondary,
    Primary,
    Ghost,
    Danger,
};

struct WidgetState {
    uint64_t active = 0;
};

struct Interaction {
    bool hovered = false;
    bool pressed = false;
    bool clicked = false;
};

class Context {
public:
    Context(DrawList& drawList, const Font& font, const InputState& input, WidgetState& state, float scale);

    const InputState& input() const
    {
        return in;
    }

    const std::vector<Rect>& interactiveRects() const
    {
        return interactive;
    }

    Cursor cursor() const
    {
        return wantedCursor;
    }

    void setBlocked(bool value)
    {
        blocked = value;
    }

    float mouseX() const;
    float mouseY() const;
    bool hovered(const Rect& rect) const;
    Interaction interact(std::string_view id, const Rect& rect);

    void fill(const Rect& rect, Color color, float radius = 0.0f);
    void gradient(const Rect& rect, Color top, Color bottom, float radius = 0.0f);
    void shadow(const Rect& rect, float radius, float blur, Color color);
    void glow(float x, float y, float size, Color color);
    void card(const Rect& rect, Color background, Color border, float radius);
    void outline(const Rect& rect, Color color);
    void image(const Rect& rect, const ImageRef& image, float radius);

    float measure(std::string_view text, TextStyle style) const;
    float lineHeight(TextStyle style) const;
    void text(std::string_view value, TextStyle style, float x, float y, Color color, float maxWidth = 0.0f);
    void textCentered(std::string_view value, TextStyle style, const Rect& rect, Color color);
    float paragraph(std::string_view value, TextStyle style, float x, float y, float width, Color color);

    bool button(std::string_view id, std::string_view label, const Rect& rect, ButtonKind kind = ButtonKind::Secondary, bool enabled = true);
    bool tab(std::string_view id, std::string_view label, const Rect& rect, bool active);
    bool field(std::string_view id, std::string_view placeholder, std::string_view value, const Rect& rect, bool focused);
    bool toggle(std::string_view id, const Rect& rect, bool on);

    void endFrame();

private:
    DrawList& drawList;
    const Font& font;
    const InputState& in;
    WidgetState& state;
    float scale;
    bool blocked = false;
    std::vector<Rect> interactive;
    Cursor wantedCursor = Cursor::Arrow;
};

}
