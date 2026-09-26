#include "ui/Font.h"

#include "ui/DrawList.h"
#include "ui/Utf8.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>

namespace kestrel::ui {

namespace {

constexpr char32_t FirstCodepoint = 32;
constexpr int AsciiCount = 224;
constexpr std::array<int, 6> ExtraCodepoints { 0x2013, 0x2014, 0x2019, 0x2022, 0x2026, 0x00B7 };
constexpr std::array<float, 5> LogicalSizes { 13.0f, 15.0f, 15.0f, 22.0f, 38.0f };
constexpr uint32_t ReservedRows = 140;

std::vector<unsigned char> readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::vector<unsigned char> readFirst(std::initializer_list<const char*> candidates)
{
    for (const char* candidate : candidates) {
        std::vector<unsigned char> data = readFile(candidate);
        if (!data.empty()) {
            return data;
        }
    }
    return {};
}

}

bool Font::load()
{
#if defined(_WIN32)
    regular = readFirst({ "assets/fonts/regular.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf" });
    bold = readFirst({ "assets/fonts/bold.ttf", "C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf" });
#elif defined(__APPLE__)
    regular = readFirst({ "assets/fonts/regular.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf" });
    bold = readFirst({ "assets/fonts/bold.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/Library/Fonts/Arial Bold.ttf" });
#else
    regular = readFirst({
        "assets/fonts/regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    });
    bold = readFirst({
        "assets/fonts/bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-SemiBold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    });
#endif
    if (bold.empty()) {
        bold = regular;
    }
    return !regular.empty();
}

void Font::bake(float newScale)
{
    scale = newScale;
    for (uint32_t atlas : { 1024u, 2048u, 4096u }) {
        if (pack(atlas, newScale)) {
            return;
        }
    }
}

bool Font::pack(uint32_t atlas, float packScale)
{
    pixels.assign(static_cast<size_t>(atlas) * atlas, 0);

    stbtt_pack_context context;
    if (!stbtt_PackBegin(&context, pixels.data(), static_cast<int>(atlas), static_cast<int>(atlas - ReservedRows), static_cast<int>(atlas), 1, nullptr)) {
        return false;
    }

    bool packed = true;
    for (size_t style = 0; style < faces.size(); ++style) {
        const std::vector<unsigned char>& data = style >= 2 ? bold : regular;
        float pixelSize = std::round(LogicalSizes[style] * packScale);
        stbtt_PackSetOversampling(&context, pixelSize < 24.0f ? 2 : 1, 1);

        std::vector<stbtt_packedchar> ascii(AsciiCount);
        std::vector<stbtt_packedchar> extra(ExtraCodepoints.size());
        std::array<int, ExtraCodepoints.size()> codepoints = ExtraCodepoints;

        stbtt_pack_range ranges[2] {};
        ranges[0].font_size = pixelSize;
        ranges[0].first_unicode_codepoint_in_range = FirstCodepoint;
        ranges[0].num_chars = AsciiCount;
        ranges[0].chardata_for_range = ascii.data();
        ranges[1].font_size = pixelSize;
        ranges[1].array_of_unicode_codepoints = codepoints.data();
        ranges[1].num_chars = static_cast<int>(codepoints.size());
        ranges[1].chardata_for_range = extra.data();

        if (!stbtt_PackFontRanges(&context, data.data(), 0, ranges, 2)) {
            packed = false;
            break;
        }

        Face& target = faces[style];
        auto convert = [&](std::vector<stbtt_packedchar>& source, std::vector<Glyph>& out) {
            out.resize(source.size());
            for (size_t i = 0; i < source.size(); ++i) {
                float x = 0.0f;
                float y = 0.0f;
                stbtt_aligned_quad quad;
                stbtt_GetPackedQuad(source.data(), static_cast<int>(atlas), static_cast<int>(atlas), static_cast<int>(i), &x, &y, &quad, 0);
                out[i] = { quad.x0, quad.y0, quad.x1, quad.y1, quad.s0, quad.t0, quad.s1, quad.t1, x };
            }
        };
        convert(ascii, target.ascii);
        convert(extra, target.extra);

        stbtt_fontinfo info;
        stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0));
        int ascent = 0;
        int descent = 0;
        int gap = 0;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
        float unit = stbtt_ScaleForPixelHeight(&info, pixelSize);
        target.ascent = ascent * unit;
        target.lineHeight = (ascent - descent + gap) * unit;
    }
    stbtt_PackEnd(&context);

    if (!packed) {
        return false;
    }

    for (uint32_t y = atlas - 6; y < atlas - 2; ++y) {
        for (uint32_t x = 1; x < 5; ++x) {
            pixels[static_cast<size_t>(y) * atlas + x] = 255;
        }
    }
    white = { 3.0f / atlas, (atlas - 4.0f) / atlas };
    size = atlas;
    return true;
}

Font::ImageSlot Font::imageSlot() const
{
    return { 16, size - ImageSlotSize - 8, ImageSlotSize };
}

const Font::Face& Font::face(TextStyle style) const
{
    return faces[static_cast<size_t>(style)];
}

const Font::Glyph& Font::glyph(const Face& source, char32_t cp) const
{
    if (cp >= FirstCodepoint && cp < FirstCodepoint + AsciiCount) {
        return source.ascii[cp - FirstCodepoint];
    }
    for (size_t i = 0; i < ExtraCodepoints.size(); ++i) {
        if (static_cast<char32_t>(ExtraCodepoints[i]) == cp) {
            return source.extra[i];
        }
    }
    return source.ascii[U'?' - FirstCodepoint];
}

float Font::measure(std::string_view text, TextStyle style) const
{
    const Face& source = face(style);
    float width = 0.0f;
    size_t i = 0;
    while (i < text.size()) {
        width += glyph(source, nextCodepoint(text, i)).advance;
    }
    return width / scale;
}

float Font::lineHeight(TextStyle style) const
{
    return face(style).lineHeight / scale;
}

void Font::emit(DrawList& list, std::string_view text, TextStyle style, float x, float y, Color color) const
{
    const Face& source = face(style);
    float pen = std::round(x * scale);
    float baseline = std::round(y * scale + source.ascent);
    uint32_t packed = color.packed();
    size_t i = 0;
    while (i < text.size()) {
        const Glyph& g = glyph(source, nextCodepoint(text, i));
        if (g.x1 > g.x0) {
            list.quad(pen + g.x0, baseline + g.y0, pen + g.x1, baseline + g.y1, g.u0, g.v0, g.u1, g.v1, packed);
        }
        pen += g.advance;
    }
}

void Font::draw(DrawList& list, std::string_view text, TextStyle style, float x, float y, Color color, float maxWidth) const
{
    if (maxWidth <= 0.0f || measure(text, style) <= maxWidth) {
        emit(list, text, style, x, y, color);
        return;
    }

    constexpr std::string_view ellipsis = "\xE2\x80\xA6";
    const Face& source = face(style);
    float budget = maxWidth - measure(ellipsis, style);
    float width = 0.0f;
    std::string clipped;
    size_t i = 0;
    while (i < text.size()) {
        size_t start = i;
        float advance = glyph(source, nextCodepoint(text, i)).advance / scale;
        if (width + advance > budget) {
            break;
        }
        width += advance;
        clipped.append(text.substr(start, i - start));
    }
    clipped.append(ellipsis);
    emit(list, clipped, style, x, y, color);
}

float Font::drawWrapped(DrawList& list, std::string_view text, TextStyle style, float x, float y, float width, Color color) const
{
    float height = lineHeight(style);
    float space = measure(" ", style);
    std::string line;
    float lineWidth = 0.0f;
    int lines = 0;
    size_t position = 0;
    while (position <= text.size()) {
        size_t end = text.find(' ', position);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view word = text.substr(position, end - position);
        float wordWidth = measure(word, style);
        if (!line.empty() && lineWidth + space + wordWidth > width) {
            emit(list, line, style, x, y + lines * height, color);
            ++lines;
            line.clear();
            lineWidth = 0.0f;
        }
        if (!line.empty()) {
            line.push_back(' ');
            lineWidth += space;
        }
        line.append(word);
        lineWidth += wordWidth;
        position = end + 1;
    }
    if (!line.empty()) {
        emit(list, line, style, x, y + lines * height, color);
        ++lines;
    }
    return lines * height;
}

}
