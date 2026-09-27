#pragma once

#include "platform/Input.h"
#include "platform/Window.h"
#include "ui/Font.h"
#include "ui/Image.h"
#include "ui/Skin.h"
#include "ui/Types.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kestrel::ui {

class DrawList;

struct WidgetState {
    uint64_t active = 0;
    uint64_t clicks = 0;
};

struct Interaction {
    bool hovered = false;
    bool pressed = false;
    bool clicked = false;
};

class Context {
public:
    Context(DrawList& drawList, const Font& font, Skin& skin, const InputState& input, WidgetState& state, float scale);

    const InputState& input() const
    {
        return in;
    }

    Skin& skin()
    {
        return art;
    }

    float pixelScale() const
    {
        return scale;
    }

    void setLayer(float offsetX, float offsetY, float opacity);
    void clearLayer();

    void spriteQuad(const std::array<std::array<float, 2>, 4>& points, std::string_view name, const std::array<std::array<float, 2>, 4>& texels, Color tint);

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

    bool isBlocked() const
    {
        return blocked;
    }

    void setClip(const Rect& rect);
    void clearClip();

    float mouseX() const;
    float mouseY() const;
    bool hovered(const Rect& rect) const;
    Interaction interact(std::string_view id, const Rect& rect);

    void fill(const Rect& rect, Color color);
    void outline(const Rect& rect, Color color, float thickness = 1.0f);
    void image(const Rect& rect, const ImageRef& image, Color tint = { 255, 255, 255, 255 });
    void sprite(const Rect& rect, std::string_view name, Color tint = { 255, 255, 255, 255 });
    void spriteRegion(const Rect& rect, std::string_view name, const Rect& texels, Color tint = { 255, 255, 255, 255 });
    void nineSlice(const Rect& rect, std::string_view name, Color tint = { 255, 255, 255, 255 });
    void borderImage(const Rect& rect, const BorderImage& border, Color tint = { 255, 255, 255, 255 });
    void border(const Rect& rect, std::string_view component, std::string_view state, Color tint = { 255, 255, 255, 255 });

    float measure(std::string_view text, TextStyle style) const;
    float lineHeight(TextStyle style) const;
    void text(std::string_view value, TextStyle style, float x, float y, Color color, float maxWidth = 0.0f);
    void textShadowed(std::string_view value, TextStyle style, float x, float y, Color color, Color shadow, float maxWidth = 0.0f);
    void textCentered(std::string_view value, TextStyle style, const Rect& rect, Color color);
    float paragraph(std::string_view value, TextStyle style, float x, float y, float width, Color color);
    float paragraphHeight(std::string_view value, TextStyle style, float width) const;

    bool classicButton(std::string_view id, std::string_view label, const Rect& rect, bool enabled = true);
    Interaction pressable(std::string_view id, std::string_view component, const Rect& rect, bool enabled = true, bool selected = false);
    bool pressableButton(std::string_view id, std::string_view component, std::string_view label, const Rect& rect, TextStyle style = TextStyle::Ui, bool enabled = true);

    void endFrame();

private:
    bool clipped(const Rect& rect) const;

    DrawList& drawList;
    const Font& font;
    Skin& art;
    const InputState& in;
    WidgetState& state;
    float scale;
    bool blocked = false;
    Rect clip {};
    std::vector<Rect> interactive;
    Cursor wantedCursor = Cursor::Arrow;
};

}
