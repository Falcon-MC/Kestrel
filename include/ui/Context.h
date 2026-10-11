#pragma once

#include "platform/Input.h"
#include "platform/Window.h"
#include "ui/Font.h"
#include "ui/Image.h"
#include "ui/Skin.h"
#include "ui/Types.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel::ui {

class DrawList;

struct WidgetState {
    uint64_t active = 0;
    uint64_t clicks = 0;
};

/**
 * A control the frame laid out, for automation: its id, the label drawn on
 * it when it has one, and where it sits in interface units. Visible is false
 * when a scroll clip hides it.
 */
struct Widget {
    std::string id;
    std::string label;
    Rect rect;
    bool visible = true;
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
    const Font& textFont() const { return font; }

    /**
     * Moves what is drawn and where the mouse reads from afterwards, so a
     * screen can lay itself out from (0, 0) inside part of the window.
     */
    void setOrigin(float x, float y);

    void setLayer(float offsetX, float offsetY, float opacity);
    void clearLayer();

    void spriteQuad(const std::array<std::array<float, 2>, 4>& points, std::string_view name, const std::array<std::array<float, 2>, 4>& texels, Color tint, bool enchanted = false);

    void setGlint(double now, float strength, float speed);

    const std::vector<Rect>& interactiveRects() const
    {
        return interactive;
    }

    /**
     * Keeps the id, label and place of every control laid out this frame,
     * which costs a string per control, so it stays off unless asked for.
     */
    void recordWidgets(bool value)
    {
        recording = value;
    }

    const std::vector<Widget>& widgets() const
    {
        return recorded;
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

    /**
     * Counts a click the way interact does, for controls that follow the
     * mouse themselves, so the click sound still plays.
     */
    void countClick()
    {
        ++state.clicks;
    }

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
    void pixelTextScaled(std::string_view value, float x, float y, float magnify, Color color, bool shadow = false);
    void textScaled(std::string_view value, TextStyle style, float x, float y, float magnify, Color color, bool shadow = false);
    void textLayoutLine(const Font::TextLine& line, TextStyle style, float x, float y, float magnify, Color color, bool shadow = false);
    void rotatedPixelText(std::string_view value, float centerX, float centerY, float magnify, float radians, Color color);
    void nameTag(std::string_view value, float x, float y, float magnify, float depth, bool sneaking);
    float paragraphShadowed(std::string_view value, TextStyle style, float x, float y, float width, Color color);
    float paragraphHeight(std::string_view value, TextStyle style, float width) const;
    float pixelParagraph(std::string_view value, float x, float y, float width, float magnify, Color color);
    size_t wrap(std::string_view value, TextStyle style, float width, std::vector<std::string_view>& lines) const;

    bool classicButton(std::string_view id, std::string_view label, const Rect& rect, bool enabled = true);
    Interaction pressable(std::string_view id, std::string_view component, const Rect& rect, bool enabled = true, bool selected = false);
    bool pressableButton(std::string_view id, std::string_view component, std::string_view label, const Rect& rect, TextStyle style = TextStyle::Ui, bool enabled = true);

    void endFrame();

private:
    bool clipped(const Rect& rect) const;
    void labelLast(std::string_view label);
    float shadowOffset(TextStyle style) const;

    std::array<float, 4> glintParameters {};
    void applyGlint(size_t first, const Sprite& source, bool armor = false);
    DrawList& drawList;
    const Font& font;
    Skin& art;
    const InputState& in;
    WidgetState& state;
    float scale;
    float originX = 0.0f;
    float originY = 0.0f;
    bool blocked = false;
    Rect clip {};
    std::vector<Rect> interactive;
    bool recording = false;
    std::vector<Widget> recorded;
    Cursor wantedCursor = Cursor::Arrow;
};

}
