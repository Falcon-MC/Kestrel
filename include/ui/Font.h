#pragma once

#include "ui/Types.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kestrel::ui {

class DrawList;
class GameAssets;
class Skin;

/**
 * Pixel is the classic bitmap font from font/default8.png. The rest come from the fonts the
 * HTML menus ship with: Seven for controls, Ten for headings, Five for sub headings and
 * Noto Sans for running text. Sizes are in menu units, see theme::css().
 */
enum class TextStyle {
    Pixel,
    Ui,
    UiSmall,
    UiLarge,
    Heading,
    HeadingSmall,
    HeadingLarge,
    SubHeading,
    Body,
    BodySmall,
    BodyBold,
    Count,
};

class Font {
public:
    static constexpr uint32_t ImageSlotSize = 128;
    static constexpr uint32_t TitleWidth = 512;
    static constexpr uint32_t TitleHeight = 128;

    bool load(GameAssets& assets, Skin& skin);
    void bake(float scale);

    const std::vector<uint8_t>& coverage() const
    {
        return pixels;
    }

    float whiteU() const
    {
        return white[0];
    }

    float whiteV() const
    {
        return white[1];
    }

    float measure(std::string_view text, TextStyle style) const;
    float lineHeight(TextStyle style) const;
    void draw(DrawList& list, std::string_view text, TextStyle style, float x, float y, Color color, float maxWidth) const;
    float drawWrapped(DrawList& list, std::string_view text, TextStyle style, float x, float y, float width, Color color) const;
    size_t wrap(std::string_view text, TextStyle style, float width, std::vector<std::string_view>& lines) const;

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
        std::vector<Glyph> glyphs;
        std::vector<char32_t> codepoints;
        float ascent = 0.0f;
        float lineHeight = 0.0f;
    };

    struct BitmapPage {
        std::array<uint8_t, 256> start {};
        std::array<uint8_t, 256> end {};
        uint32_t cell = 8;
        bool loaded = false;
    };

    const Glyph* glyph(const Face& face, char32_t cp) const;
    float advance(TextStyle style, char32_t cp) const;
    float boldStep(TextStyle style) const;
    char32_t scrambled(TextStyle style, char32_t cp, uint32_t seed) const;
    void emit(DrawList& list, std::string_view text, TextStyle style, float x, float y, Color color) const;
    void emitPixel(DrawList& list, std::string_view text, float x, float y, Color color) const;
    float pixelAdvance(char32_t cp) const;
    const BitmapPage* pixelPage(char32_t cp) const;
    void readPixelPage(size_t index);
    bool pack(uint32_t height, float scale);

    Skin* skin = nullptr;
    std::array<std::vector<unsigned char>, 5> sources;
    std::array<Face, static_cast<size_t>(TextStyle::Count)> faces;
    std::array<BitmapPage, 3> pages;
    std::vector<uint8_t> pixels;
    float scale = 1.0f;
    std::array<float, 2> white {};
};

}
