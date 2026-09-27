#include "ui/Font.h"

#include "ui/DrawList.h"
#include "ui/GameAssets.h"
#include "ui/Skin.h"
#include "ui/Theme.h"
#include "ui/Utf8.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>

namespace kestrel::ui {

namespace {

enum Source {
    Seven,
    Ten,
    Five,
    Noto,
    NotoBold,
};

struct FaceSpec {
    Source source;
    float cssSize;
};

// Indexed by TextStyle, Pixel has no TrueType face.
constexpr std::array<FaceSpec, static_cast<size_t>(TextStyle::Count)> Specs { {
    { Seven, 16.0f },
    { Seven, 16.0f },
    { Seven, 14.0f },
    { Seven, 20.0f },
    { Ten, 16.0f },
    { Ten, 12.0f },
    { Ten, 24.0f },
    { Five, 16.0f },
    { Noto, 14.0f },
    { Noto, 12.0f },
    { NotoBold, 14.0f },
} };

constexpr uint32_t AtlasWidth = Skin::AtlasSize;
constexpr uint32_t FontRows = Skin::ImageTop;
constexpr float PixelLineHeight = 10.0f;
constexpr float PixelSpace = 4.0f;
constexpr const char* PixelPages[] = { "font/default8", "font/glyph_00", "font/glyph_01" };

std::vector<char32_t> coveredCodepoints()
{
    std::vector<char32_t> result;
    for (char32_t cp = 32; cp < 0x180; ++cp) {
        if (cp < 0x7F || cp >= 0xA0) {
            result.push_back(cp);
        }
    }
    for (char32_t cp : { 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2122 }) {
        result.push_back(cp);
    }
    return result;
}

constexpr char32_t FormatSign = 0xA7;
constexpr float ItalicSlant = 0.2f;

/**
 * The formatting in effect at one point of a string with section sign codes:
 * the color, the code that set it, and the bold, italic and obfuscated styles.
 */
struct Formatting {
    Color color;
    char32_t colorCode = 0;
    bool bold = false;
    bool italic = false;
    bool obfuscated = false;
};

char32_t lowerCode(char32_t code)
{
    return code >= U'A' && code <= U'Z' ? code + 32 : code;
}

/**
 * The RGB of a color code, covering the sixteen classic colors and the
 * material colors g to v.
 */
std::optional<uint32_t> formatColor(char32_t code)
{
    switch (lowerCode(code)) {
    case U'0':
        return 0x000000;
    case U'1':
        return 0x0000AA;
    case U'2':
        return 0x00AA00;
    case U'3':
        return 0x00AAAA;
    case U'4':
        return 0xAA0000;
    case U'5':
        return 0xAA00AA;
    case U'6':
        return 0xFFAA00;
    case U'7':
        return 0xAAAAAA;
    case U'8':
        return 0x555555;
    case U'9':
        return 0x5555FF;
    case U'a':
        return 0x55FF55;
    case U'b':
        return 0x55FFFF;
    case U'c':
        return 0xFF5555;
    case U'd':
        return 0xFF55FF;
    case U'e':
        return 0xFFFF55;
    case U'f':
        return 0xFFFFFF;
    case U'g':
        return 0xDDD605;
    case U'h':
        return 0xE3D4D1;
    case U'i':
        return 0xCECACA;
    case U'j':
        return 0x443A3B;
    case U'm':
        return 0x971607;
    case U'n':
        return 0xB4684D;
    case U'p':
        return 0xDEB12D;
    case U'q':
        return 0x47A036;
    case U's':
        return 0x2CBAA8;
    case U't':
        return 0x21497B;
    case U'u':
        return 0x9A5CC6;
    case U'v':
        return 0xEB7114;
    default:
        return std::nullopt;
    }
}

/**
 * Applies one code: a color resets the styles, r resets to the base color,
 * l, o and k switch on bold, italic and obfuscated; unknown codes are hidden.
 * Colored text keeps the alpha of the base color.
 */
void applyFormat(Formatting& state, char32_t code, Color base)
{
    code = lowerCode(code);
    if (std::optional<uint32_t> rgb = formatColor(code)) {
        state = Formatting {};
        state.color = { static_cast<uint8_t>(*rgb >> 16), static_cast<uint8_t>(*rgb >> 8), static_cast<uint8_t>(*rgb), base.a };
        state.colorCode = code;
        return;
    }
    switch (code) {
    case U'l':
        state.bold = true;
        break;
    case U'o':
        state.italic = true;
        break;
    case U'k':
        state.obfuscated = true;
        break;
    case U'r':
        state = Formatting {};
        state.color = base;
        break;
    default:
        break;
    }
}

/**
 * Reads the next codepoint, consuming any formatting codes before it into
 * state; returns false at the end of the text.
 */
bool nextVisible(std::string_view text, size_t& i, Formatting& state, Color base, char32_t& out)
{
    while (i < text.size()) {
        char32_t cp = nextCodepoint(text, i);
        if (cp != FormatSign) {
            out = cp;
            return true;
        }
        if (i < text.size()) {
            applyFormat(state, nextCodepoint(text, i), base);
        }
    }
    return false;
}

std::string encodeCode(char32_t code)
{
    std::string text = "\xC2\xA7";
    text.push_back(static_cast<char>(code));
    return text;
}

/**
 * The codes that recreate the formatting in effect at the end of text, so a
 * wrapped line keeps the look of the one before it.
 */
std::string activeFormatting(std::string_view text)
{
    Formatting state;
    size_t i = 0;
    char32_t cp = 0;
    while (nextVisible(text, i, state, Color {}, cp)) {
    }
    std::string prefix;
    if (state.colorCode) {
        prefix += encodeCode(state.colorCode);
    }
    if (state.bold) {
        prefix += encodeCode(U'l');
    }
    if (state.italic) {
        prefix += encodeCode(U'o');
    }
    if (state.obfuscated) {
        prefix += encodeCode(U'k');
    }
    return prefix;
}

uint32_t obfuscationTick()
{
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 50);
}

}

bool Font::load(GameAssets& assets, Skin& target)
{
    skin = &target;
    sources[Seven] = assets.readHbuiFont("Minecraft-Seven-v4");
    sources[Ten] = assets.readPackFile("font/minecraft-ten.ttf");
    sources[Five] = assets.readHbuiFont("MinecraftFiveV3");
    sources[Noto] = assets.readHbuiFont("NotoSansMerged-Regular");
    sources[NotoBold] = assets.readHbuiFont("NotoSans-Bold");
    if (sources[Ten].empty()) {
        sources[Ten] = assets.readHbuiFont("Minecraft-Ten");
    }
    if (sources[Five].empty()) {
        sources[Five] = sources[Seven];
    }
    if (sources[Ten].empty()) {
        sources[Ten] = sources[Five];
    }
    if (sources[NotoBold].empty()) {
        sources[NotoBold] = sources[Noto];
    }
    for (size_t i = 0; i < pages.size(); ++i) {
        readPixelPage(i);
    }
    return !sources[Seven].empty() && !sources[Noto].empty() && pages[0].loaded;
}

void Font::readPixelPage(size_t index)
{
    const Bitmap* bitmap = skin->bitmap(PixelPages[index]);
    if (!bitmap || bitmap->width != bitmap->height || bitmap->width % 16 != 0) {
        return;
    }
    BitmapPage& page = pages[index];
    page.cell = bitmap->width / 16;
    for (uint32_t code = 0; code < 256; ++code) {
        uint32_t cellX = (code % 16) * page.cell;
        uint32_t cellY = (code / 16) * page.cell;
        int first = -1;
        int last = -1;
        for (uint32_t column = 0; column < page.cell; ++column) {
            for (uint32_t row = 0; row < page.cell; ++row) {
                if (bitmap->rgba[((static_cast<size_t>(cellY) + row) * bitmap->width + cellX + column) * 4 + 3] > 0) {
                    if (first < 0) {
                        first = static_cast<int>(column);
                    }
                    last = static_cast<int>(column);
                    break;
                }
            }
        }
        page.start[code] = static_cast<uint8_t>(first < 0 ? 0 : first);
        page.end[code] = static_cast<uint8_t>(last < 0 ? 0 : last + 1);
    }
    page.loaded = true;
}

void Font::bake(float newScale)
{
    scale = newScale;
    for (uint32_t height : { 1024u, FontRows }) {
        if (pack(height, newScale)) {
            return;
        }
    }
}

bool Font::pack(uint32_t height, float packScale)
{
    pixels.assign(static_cast<size_t>(AtlasWidth) * FontRows, 0);

    stbtt_pack_context context;
    if (!stbtt_PackBegin(&context, pixels.data(), static_cast<int>(AtlasWidth), static_cast<int>(height - 16), static_cast<int>(AtlasWidth), 1, nullptr)) {
        return false;
    }

    std::vector<char32_t> wanted = coveredCodepoints();
    std::vector<int> codepoints(wanted.begin(), wanted.end());
    bool packed = true;
    for (size_t style = 1; style < faces.size(); ++style) {
        const FaceSpec& spec = Specs[style];
        const std::vector<unsigned char>& data = sources[spec.source];
        float pixelSize = std::round(theme::css(spec.cssSize) * packScale);
        stbtt_PackSetOversampling(&context, 1, 1);

        std::vector<stbtt_packedchar> chars(codepoints.size());
        stbtt_pack_range range {};
        range.font_size = pixelSize;
        range.array_of_unicode_codepoints = codepoints.data();
        range.num_chars = static_cast<int>(codepoints.size());
        range.chardata_for_range = chars.data();
        if (!stbtt_PackFontRanges(&context, data.data(), 0, &range, 1)) {
            packed = false;
            break;
        }

        Face& target = faces[style];
        target.codepoints = wanted;
        target.glyphs.resize(chars.size());
        for (size_t i = 0; i < chars.size(); ++i) {
            float x = 0.0f;
            float y = 0.0f;
            stbtt_aligned_quad quad;
            stbtt_GetPackedQuad(chars.data(), static_cast<int>(AtlasWidth), static_cast<int>(AtlasWidth), static_cast<int>(i), &x, &y, &quad, 0);
            target.glyphs[i] = { quad.x0, quad.y0, quad.x1, quad.y1, quad.s0, quad.t0, quad.s1, quad.t1, x };
        }

        stbtt_fontinfo info;
        stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0));
        int ascent = 0;
        int descent = 0;
        int gap = 0;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
        float unit = stbtt_ScaleForPixelHeight(&info, pixelSize);
        target.ascent = std::round(ascent * unit);
        target.lineHeight = std::round((ascent - descent + gap) * unit);
    }
    stbtt_PackEnd(&context);
    if (!packed) {
        return false;
    }

    for (uint32_t y = FontRows - 6; y < FontRows - 2; ++y) {
        for (uint32_t x = 1; x < 5; ++x) {
            pixels[static_cast<size_t>(y) * AtlasWidth + x] = 255;
        }
    }
    white = { 3.0f / AtlasWidth, (FontRows - 4.0f) / AtlasWidth };
    return true;
}

const Font::Glyph* Font::glyph(const Face& face, char32_t cp) const
{
    auto found = std::lower_bound(face.codepoints.begin(), face.codepoints.end(), cp);
    if (found != face.codepoints.end() && *found == cp) {
        return &face.glyphs[static_cast<size_t>(found - face.codepoints.begin())];
    }
    found = std::lower_bound(face.codepoints.begin(), face.codepoints.end(), U'?');
    return found != face.codepoints.end() ? &face.glyphs[static_cast<size_t>(found - face.codepoints.begin())] : nullptr;
}

const Font::BitmapPage* Font::pixelPage(char32_t cp) const
{
    size_t index = cp < 0x80 ? 0 : cp < 0x100 ? 1 : cp < 0x200 ? 2 : 0;
    if (!pages[index].loaded) {
        index = 0;
    }
    return &pages[index];
}

float Font::pixelAdvance(char32_t cp) const
{
    if (cp == U' ') {
        return PixelSpace;
    }
    const BitmapPage* page = pixelPage(cp);
    uint32_t code = static_cast<uint32_t>(cp & 0xFF);
    float texel = 8.0f / static_cast<float>(page->cell);
    float width = page == &pages[0] ? page->end[code] : page->end[code] - page->start[code];
    return std::round(width * texel) + 1.0f;
}

float Font::advance(TextStyle style, char32_t cp) const
{
    if (style == TextStyle::Pixel) {
        return pixelAdvance(cp);
    }
    const Glyph* g = glyph(faces[static_cast<size_t>(style)], cp);
    return g ? g->advance / scale : 0.0f;
}

float Font::boldStep(TextStyle style) const
{
    return style == TextStyle::Pixel ? 1.0f : std::max(1.0f, std::round(scale)) / scale;
}

char32_t Font::scrambled(TextStyle style, char32_t cp, uint32_t seed) const
{
    if (cp == U' ') {
        return cp;
    }
    float target = advance(style, cp);
    for (uint32_t attempt = 0; attempt < 94; ++attempt) {
        char32_t candidate = static_cast<char32_t>(33 + (seed + attempt) % 94);
        if (std::abs(advance(style, candidate) - target) < 0.01f) {
            return candidate;
        }
    }
    return cp;
}

float Font::measure(std::string_view text, TextStyle style) const
{
    float width = 0.0f;
    Formatting state;
    size_t i = 0;
    char32_t cp = 0;
    while (nextVisible(text, i, state, Color {}, cp)) {
        width += advance(style, cp) + (state.bold ? boldStep(style) : 0.0f);
    }
    if (style == TextStyle::Pixel && width > 0.0f) {
        width -= 1.0f;
    }
    return width;
}

float Font::lineHeight(TextStyle style) const
{
    if (style == TextStyle::Pixel) {
        return PixelLineHeight;
    }
    return faces[static_cast<size_t>(style)].lineHeight / scale;
}

void Font::emitPixel(DrawList& list, std::string_view text, float x, float y, Color color) const
{
    float pen = std::round(x * scale);
    float top = std::round(y * scale);
    uint32_t tick = obfuscationTick();
    Formatting state;
    state.color = color;
    size_t i = 0;
    uint32_t index = 0;
    char32_t visible = 0;
    while (nextVisible(text, i, state, color, visible)) {
        char32_t cp = state.obfuscated ? scrambled(TextStyle::Pixel, visible, tick * 2654435761u ^ index * 40503u) : visible;
        ++index;
        uint32_t packed = state.color.packed();
        const BitmapPage* page = pixelPage(cp);
        size_t pageIndex = static_cast<size_t>(page - pages.data());
        const Sprite& sheet = skin->sprite(PixelPages[pageIndex]);
        float bold = state.bold ? scale : 0.0f;
        float step = pixelAdvance(cp) * scale + bold;
        if (cp != U' ' && sheet.valid) {
            uint32_t code = static_cast<uint32_t>(cp & 0xFF);
            float texel = 8.0f / static_cast<float>(page->cell) * scale;
            float first = pageIndex == 0 ? 0.0f : page->start[code];
            float last = page->end[code];
            float du = (sheet.image.u1 - sheet.image.u0) / sheet.width;
            float dv = (sheet.image.v1 - sheet.image.v0) / sheet.height;
            float cellX = static_cast<float>((code % 16) * page->cell);
            float cellY = static_cast<float>((code / 16) * page->cell);
            float u0 = sheet.image.u0 + (cellX + first) * du;
            float u1 = sheet.image.u0 + (cellX + last) * du;
            float v0 = sheet.image.v0 + cellY * dv;
            float v1 = sheet.image.v0 + (cellY + page->cell) * dv;
            float height = page->cell * texel;
            float slant = state.italic ? height * ItalicSlant : 0.0f;
            list.quad(pen, top, pen + (last - first) * texel, top + height, u0, v0, u1, v1, packed, slant, 0.0f);
            if (state.bold) {
                list.quad(pen + bold, top, pen + bold + (last - first) * texel, top + height, u0, v0, u1, v1, packed, slant, 0.0f);
            }
        }
        pen += step;
    }
}

void Font::emit(DrawList& list, std::string_view text, TextStyle style, float x, float y, Color color) const
{
    if (style == TextStyle::Pixel) {
        emitPixel(list, text, x, y, color);
        return;
    }
    const Face& source = faces[static_cast<size_t>(style)];
    float pen = std::round(x * scale);
    float baseline = std::round(y * scale) + source.ascent;
    uint32_t tick = obfuscationTick();
    Formatting state;
    state.color = color;
    size_t i = 0;
    uint32_t index = 0;
    char32_t visible = 0;
    while (nextVisible(text, i, state, color, visible)) {
        char32_t cp = state.obfuscated ? scrambled(style, visible, tick * 2654435761u ^ index * 40503u) : visible;
        ++index;
        const Glyph* g = glyph(source, cp);
        if (!g) {
            continue;
        }
        float bold = state.bold ? std::max(1.0f, std::round(scale)) : 0.0f;
        if (g->x1 > g->x0) {
            uint32_t packed = state.color.packed();
            float topShift = state.italic ? -g->y0 * ItalicSlant : 0.0f;
            float bottomShift = state.italic ? -g->y1 * ItalicSlant : 0.0f;
            list.quad(pen + g->x0, baseline + g->y0, pen + g->x1, baseline + g->y1, g->u0, g->v0, g->u1, g->v1, packed, topShift, bottomShift);
            if (state.bold) {
                list.quad(pen + bold + g->x0, baseline + g->y0, pen + bold + g->x1, baseline + g->y1, g->u0, g->v0, g->u1, g->v1, packed, topShift, bottomShift);
            }
        }
        pen += g->advance + bold;
    }
}

void Font::draw(DrawList& list, std::string_view text, TextStyle style, float x, float y, Color color, float maxWidth) const
{
    if (maxWidth <= 0.0f || measure(text, style) <= maxWidth) {
        emit(list, text, style, x, y, color);
        return;
    }

    constexpr std::string_view Ellipsis = "...";
    float budget = maxWidth - measure(Ellipsis, style);
    float width = 0.0f;
    std::string clipped;
    Formatting state;
    size_t i = 0;
    while (i < text.size()) {
        size_t start = i;
        char32_t cp = nextCodepoint(text, i);
        if (cp == FormatSign) {
            if (i < text.size()) {
                applyFormat(state, nextCodepoint(text, i), color);
            }
            clipped.append(text.substr(start, i - start));
            continue;
        }
        float step = advance(style, cp) + (state.bold ? boldStep(style) : 0.0f);
        if (width + step > budget) {
            break;
        }
        width += step;
        clipped.append(text.substr(start, i - start));
    }
    clipped.append(Ellipsis);
    emit(list, clipped, style, x, y, color);
}

size_t Font::wrap(std::string_view text, TextStyle style, float width, std::vector<std::string_view>& lines) const
{
    lines.clear();
    size_t lineStart = 0;
    size_t lineEnd = 0;
    size_t position = 0;
    while (position <= text.size()) {
        size_t end = text.find_first_of(" \n", position);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        if (lineEnd > lineStart && measure(text.substr(lineStart, end - lineStart), style) > width) {
            lines.push_back(text.substr(lineStart, lineEnd - lineStart));
            lineStart = position;
        }
        lineEnd = end;
        if (end < text.size() && text[end] == '\n') {
            lines.push_back(text.substr(lineStart, end - lineStart));
            lineStart = end + 1;
            lineEnd = lineStart;
        }
        position = end + 1;
    }
    if (lineEnd > lineStart) {
        lines.push_back(text.substr(lineStart, lineEnd - lineStart));
    }
    return lines.size();
}

float Font::drawWrapped(DrawList& list, std::string_view text, TextStyle style, float x, float y, float width, Color color) const
{
    std::vector<std::string_view> lines;
    wrap(text, style, width, lines);
    float height = lineHeight(style);
    std::string carried;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string line = carried + std::string(lines[i]);
        emit(list, line, style, x, y + static_cast<float>(i) * height, color);
        carried = activeFormatting(line);
    }
    return static_cast<float>(lines.size()) * height;
}

}
