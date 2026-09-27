#pragma once

#include "ui/Types.h"

#include <array>
#include <cstdint>
#include <string>
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

    // default8 for ASCII, then one glyph_XX sheet per high byte of the code point.
    static constexpr size_t PixelPageCount = 257;

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

    /**
     * Like drawWrapped, with every line first drawn offset by shadowOffset in
     * a quarter of its colors, the way labels with a shadow look in game.
     */
    float drawWrappedShadowed(DrawList& list, std::string_view text, TextStyle style, float x, float y, float width, Color color, float shadowOffset) const;

    /**
     * Pixel font text with every font pixel magnify menu units wide, for text
     * that sits in the world and shrinks with distance.
     */
    void drawPixelScaled(DrawList& list, std::string_view text, float x, float y, float magnify, Color color) const;

    /**
     * The skin sprite of a pixel font sheet: font/default8 for index 0, then
     * font/glyph_00 to font/glyph_FF.
     */
    static std::string pixelPageName(size_t index);

    /**
     * Forgets the measured glyph sheets so they are read again, after a server
     * pack replaced some of them.
     */
    void reloadPixelPages();

    /**
     * The width a glyph sheet had before it was shrunk for the atlas, which
     * is what sets how big its glyphs are drawn; 0 uses the sheet as loaded.
     */
    void setPixelPageSourceWidth(size_t index, uint32_t width);
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
        float height = 8.0f;
        bool loaded = false;
        bool tried = false;
        std::string sprite;
    };

    const Glyph* glyph(const Face& face, char32_t cp) const;
    float advance(TextStyle style, char32_t cp) const;
    float boldStep(TextStyle style) const;
    char32_t scrambled(TextStyle style, char32_t cp, uint32_t seed) const;
    void emit(DrawList& list, std::string_view text, TextStyle style, float x, float y, Color color, bool shadow = false) const;
    void emitPixel(DrawList& list, std::string_view text, float x, float y, Color color, bool shadow, float magnify = 1.0f) const;
    float pixelAdvance(char32_t cp) const;
    const BitmapPage* pixelPage(char32_t cp, size_t* index = nullptr) const;
    void readPixelPage(size_t index) const;
    bool pack(uint32_t height, float scale);

    Skin* skin = nullptr;
    std::array<std::vector<unsigned char>, 5> sources;
    std::array<Face, static_cast<size_t>(TextStyle::Count)> faces;
    mutable std::array<BitmapPage, PixelPageCount> pages;
    std::array<uint32_t, PixelPageCount> sourceWidths {};
    std::vector<uint8_t> pixels;
    float scale = 1.0f;
    std::array<float, 2> white {};
};

}
