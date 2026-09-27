#pragma once

#include "ui/Types.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kestrel::ui {

class DrawList;

enum class TextStyle {
    Caption,
    Body,
    Label,
    Heading,
    Display,
};

class Font {
public:
    static constexpr uint32_t ImageSlotSize = 128;
    static constexpr uint32_t TitleWidth = 512;
    static constexpr uint32_t TitleHeight = 128;

    struct ImageSlot {
        uint32_t x;
        uint32_t y;
        uint32_t size;
    };

    bool load();
    void bake(float scale);

    const std::vector<uint8_t>& atlasPixels() const
    {
        return pixels;
    }

    uint32_t atlasSize() const
    {
        return size;
    }

    float whiteU() const
    {
        return white[0];
    }

    float whiteV() const
    {
        return white[1];
    }

    ImageSlot imageSlot() const;
    ImageSlot titleSlot() const;

    float measure(std::string_view text, TextStyle style) const;
    float lineHeight(TextStyle style) const;
    void draw(DrawList& list, std::string_view text, TextStyle style, float x, float y, Color color, float maxWidth) const;
    float drawWrapped(DrawList& list, std::string_view text, TextStyle style, float x, float y, float width, Color color) const;

private:
    struct Glyph {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
        float advance = 0.0f;
    };

    struct Face {
        std::vector<Glyph> ascii;
        std::vector<Glyph> extra;
        float ascent = 0.0f;
        float lineHeight = 0.0f;
    };

    const Face& face(TextStyle style) const;
    const Glyph& glyph(const Face& face, char32_t cp) const;
    void emit(DrawList& list, std::string_view text, TextStyle style, float x, float y, Color color) const;
    bool pack(uint32_t atlas, float scale);

    std::vector<unsigned char> regular;
    std::vector<unsigned char> bold;
    std::array<Face, 5> faces;
    std::vector<uint8_t> pixels;
    uint32_t size = 0;
    float scale = 1.0f;
    std::array<float, 2> white {};
};

}
