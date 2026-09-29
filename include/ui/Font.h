#pragma once

#include "ui/Types.h"

#include <array>
#include <cstdint>
#include <optional>
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
    ErrorBody,
    ErrorTab,
    // Minecraft Seven sized to the pixel font, for what default8 has no cell for.
    PixelFallback,
    // Minecraft Ten at the size of the pixel font, for JSON UI labels with font_type MinecraftTen.
    TenLabel,
    Count,
};

class Font {
public:
    static constexpr uint32_t ImageSlotSize = 128;
    static constexpr uint32_t TitleWidth = 512;
    static constexpr uint32_t TitleHeight = 128;

    // default8 for ASCII, then one glyph_XX sheet per high byte of the code point.
    static constexpr size_t PixelPageCount = 257;

    /**
     * The part of its cell a glyph covers, in source pixels; empty when right
     * is not past left.
     */
    struct GlyphBox {
        uint16_t left = 0;
        uint16_t top = 0;
        uint16_t right = 0;
        uint16_t bottom = 0;
    };

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
    /**
     * Draws a line of text, or its shadow when shadow is set: every color,
     * including those its codes pick, at a quarter of its brightness.
     */
    void draw(DrawList& list, std::string_view text, TextStyle style, float x, float y, Color color, float maxWidth, bool shadow = false) const;
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
    void drawPixelScaled(DrawList& list, std::string_view text, float x, float y, float magnify, Color color, bool shadow = false) const;

    /**
     * Text in any style with every unit magnify menu units wide, the way JSON
     * UI labels scale by font_size and font_scale_factor.
     */
    void drawScaled(DrawList& list, std::string_view text, TextStyle style, float x, float y, float magnify, Color color, bool shadow = false) const;
    // Centered multiline label in font pixels, independent of the UI atlas scale.
    void drawNameTag(DrawList& list, std::string_view text, Color color, bool background) const;

    /**
     * Pixel font text wrapped to width with every font pixel magnify menu
     * units wide, the way sized labels in the game's JSON UI draw. Returns the
     * height it took.
     */
    float drawWrappedPixel(DrawList& list, std::string_view text, float x, float y, float width, float magnify, Color color) const;

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
     * Draws a sheet from glyphs stored one by one (skin sprites named by
     * pixelGlyphName) instead of from the sheet, so a pack's HD sheet keeps
     * its detail without taking its whole size in the atlas.
     */
    void setPixelPageGlyphs(size_t index, uint32_t cell, const std::array<GlyphBox, 256>& boxes);
    void clearPixelPageGlyphs();
    static std::string pixelGlyphName(size_t index, uint32_t code);
    size_t wrap(std::string_view text, TextStyle style, float width, std::vector<std::string_view>& lines) const;

    /**
     * The section sign codes in effect at the end of text, for the next line
     * of a label to start with.
     */
    static std::string formattingAt(std::string_view text);

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
        float top = 0.0f;
        bool loaded = false;
        bool tried = false;
        std::string sprite;
        bool split = false;
        std::array<std::array<uint16_t, 2>, 256> rows {};
        std::vector<std::string> glyphSprites;
    };

    struct SplitPage {
        uint32_t cell = 16;
        std::array<GlyphBox, 256> boxes {};
    };

    const Glyph* glyph(const Face& face, char32_t cp) const;
    float advance(TextStyle style, char32_t cp) const;
    float boldStep(TextStyle style) const;
    char32_t scrambled(TextStyle style, char32_t cp, uint32_t seed) const;
    void emit(DrawList& list, std::string_view text, TextStyle style, float x, float y, Color color, bool shadow = false, float magnify = 1.0f) const;
    void emitPixel(DrawList& list, std::string_view text, float x, float y, Color color, bool shadow, float magnify = 1.0f) const;
    float pixelAdvance(char32_t cp) const;
    const Glyph* pixelFallback(char32_t cp) const;
    const BitmapPage* pixelPage(char32_t cp, size_t* index = nullptr) const;
    void readPixelPage(size_t index) const;
    static void placePage(size_t index, BitmapPage& page);
    bool pack(uint32_t height, float scale);

    Skin* skin = nullptr;
    std::array<std::vector<unsigned char>, 5> sources;
    std::array<Face, static_cast<size_t>(TextStyle::Count)> faces;
    mutable std::array<BitmapPage, PixelPageCount> pages;
    std::array<std::optional<SplitPage>, PixelPageCount> splitPages;
    std::vector<uint8_t> pixels;
    float scale = 1.0f;
    std::array<float, 2> white {};
};

}
