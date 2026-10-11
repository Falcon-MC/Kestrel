#include "ui/AssetDownloadScreen.h"
#include "platform/Paths.h"
#include "ui/FallbackArt.h"
#include "ui/Theme.h"

#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace kestrel::ui {
namespace {
constexpr std::array<float, 4> Sizes {14, 18, 26, 40};
constexpr float FontScale = 2.0f;
constexpr Color Sky {64, 86, 104, 255};

void panel(DrawList& list, Rect rect, Color color)
{
    list.fill({rect.x + 4, rect.y + 6, rect.w, rect.h}, {0, 0, 0, 70});
    list.fill(rect, theme::Divider);
    list.fill(rect.inset(2), color);
    list.fill({rect.x + 2, rect.y + 2, rect.w - 4, 2}, {255, 255, 255, 22});
}
}

AssetDownloadScreen::AssetDownloadScreen(const std::filesystem::path& path)
{
    std::ifstream input(path.empty() ? platform::startupFont() : path, std::ios::binary);
    std::vector<unsigned char> source {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (source.empty()) throw std::runtime_error("Cannot read a system font for the first-run screen");
    int offset = stbtt_GetFontOffsetForIndex(source.data(), 0);
    stbtt_fontinfo font {};
    if (offset < 0 || !stbtt_InitFont(&font, source.data(), offset)) {
        throw std::runtime_error("Cannot load the first-run screen font");
    }
    std::vector<uint8_t> coverage(AtlasSize * 640);
    stbtt_pack_context pack {};
    if (!stbtt_PackBegin(&pack, coverage.data(), AtlasSize, 640, 0, 1, nullptr)) {
        throw std::runtime_error("Cannot create the first-run screen font atlas");
    }
    bool packed = true;
    for (size_t i = 0; i < Sizes.size(); ++i) {
        std::array<stbtt_packedchar, 95> chars {};
        packed &= stbtt_PackFontRange(&pack, source.data(), 0, Sizes[i] * FontScale, 32, 95, chars.data()) != 0;
        for (size_t j = 0; j < chars.size(); ++j) {
            const auto& c = chars[j];
            glyphs[i][j] = {c.xoff / FontScale, c.yoff / FontScale,
                c.xoff2 / FontScale, c.yoff2 / FontScale, c.xadvance / FontScale,
                float(c.x0) / AtlasSize, float(c.y0) / AtlasSize,
                float(c.x1) / AtlasSize, float(c.y1) / AtlasSize};
        }
    }
    stbtt_PackEnd(&pack);
    if (!packed) throw std::runtime_error("Cannot pack the first-run screen font atlas");
    pixels.resize(AtlasSize * AtlasSize * 4);
    for (size_t i = 0; i < coverage.size(); ++i) {
        pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = coverage[i];
    }
    Bitmap landscape;
    drawFallbackArt("hbui/screenshot_3", landscape);
    for (uint32_t y = 0; y < landscape.height; ++y) {
        std::copy_n(landscape.rgba.data() + y * landscape.width * 4, landscape.width * 4,
            pixels.data() + ((y + 700) * AtlasSize) * 4);
    }
    for (size_t i = 0; i < 4; ++i) pixels[i] = 255;
}

float AssetDownloadScreen::measure(std::string_view value, size_t face) const
{
    float width = 0;
    for (unsigned char c : value) width += glyphs[face][c >= 32 && c <= 126 ? c - 32 : '?' - 32].advance;
    return width;
}

void AssetDownloadScreen::text(DrawList& list, std::string_view value, size_t face, float x, float y, Color color) const
{
    float scale = list.scale();
    float baseline = y + Sizes[face] * 0.85f;
    for (unsigned char c : value) {
        const auto& g = glyphs[face][c >= 32 && c <= 126 ? c - 32 : '?' - 32];
        if (g.x1 > g.x0) list.quad((x + g.x0) * scale, (baseline + g.y0) * scale,
            (x + g.x1) * scale, (baseline + g.y1) * scale, g.u0, g.v0, g.u1, g.v1, color.packed());
        x += g.advance;
    }
}

void AssetDownloadScreen::centered(DrawList& list, std::string_view value, size_t face, Rect area, Color color) const
{
    text(list, value, face, area.x + (area.w - measure(value, face)) * 0.5f,
        area.y + (area.h - Sizes[face]) * 0.5f, color);
}

void AssetDownloadScreen::button(DrawList& list, Rect rect, std::string_view label, bool primary, bool hovered, bool disabled) const
{
    Color surface = disabled ? theme::Disabled : primary ? theme::Primary : theme::Secondary;
    if (hovered && !disabled) surface = primary ? Color{82, 165, 53, 255} : Color{234, 236, 238, 255};
    list.fill(rect, theme::Divider);
    list.fill(rect.inset(2), surface);
    list.fill({rect.x + 2, rect.y + 2, rect.w - 4, 3}, {255, 255, 255, 65});
    list.fill({rect.x + 2, rect.bottom() - 6, rect.w - 4, 4}, {0, 0, 0, 55});
    centered(list, label, 1, rect, primary || disabled ? theme::White : theme::InkDark);
}

void AssetDownloadScreen::draw(DrawList& list, float width, float height, float scale,
    world::AssetDownloadProgress progress, float seconds, const std::string& error,
    bool cancelling, float mouseX, float mouseY)
{
    list.reset(scale, 0.5f / AtlasSize, 0.5f / AtlasSize);
    list.fill({0, 0, width, height}, Sky);
    // Original procedural landscape, never a bundled Minecraft panorama or screenshot.
    float imageHeight = std::max(height, width * 180 / 320);
    float imageWidth = imageHeight * 320 / 180;
    list.quad((width - imageWidth) * 0.5f * scale, (height - imageHeight) * 0.5f * scale,
        (width + imageWidth) * 0.5f * scale, (height + imageHeight) * 0.5f * scale,
        0, 700.0f / AtlasSize, 320.0f / AtlasSize, 880.0f / AtlasSize, theme::White.packed());
    list.fill({0, 0, width, height}, {20, 25, 29, 145});
    list.fill({0, 0, width, 48}, theme::HeaderBar);
    list.fill({0, 46, width, 2}, theme::HeaderEdge);
    text(list, "KESTREL", 1, 28, 14, theme::InkDark);
    text(list, "FIRST LAUNCH", 0, width - measure("FIRST LAUNCH", 0) - 28, 17, {96, 98, 101, 255});

    float cardWidth = std::min(576.0f, width - 48);
    float cardHeight = 388;
    Rect card {(width - cardWidth) * 0.5f, 64 + std::max(0.0f, (height - 112 - cardHeight) * 0.5f), cardWidth, cardHeight};
    panel(list, card, theme::Panel);
    list.fill({card.x + 2, card.y + 2, card.w - 4, 5}, theme::Primary);
    float x = card.x + 30, y = card.y + 26, inner = card.w - 60;
    text(list, "LET'S GET YOU READY", 0, x, y, theme::Caret);
    text(list, error.empty() ? "Preparing your game" : "Setup interrupted", 2, x, y + 26, theme::White);
    text(list, error.empty() ? "Kestrel needs Minecraft's resources to display"
        : "Kestrel couldn't download or install the resources.", 1, x, y + 67, theme::White);
    text(list, error.empty() ? "blocks, items and menus. We'll download them now."
        : "You can retry without restarting the game.", 1, x, y + 91, theme::White);

    Rect steps {x, y + 139, inner, 36};
    const std::array<std::string_view, 3> labels {"Download", "Install", "Play"};
    int current = progress.stage == world::AssetDownloadStage::Installing ? 1
        : progress.stage == world::AssetDownloadStage::Ready ? 2 : 0;
    for (int i = 0; i < 3; ++i) {
        float stepX = steps.x + i * (inner / 3);
        list.fill({stepX, steps.y, 24, 24}, i <= current ? theme::Primary : theme::PanelDark);
        centered(list, std::to_string(i + 1), 0, {stepX, steps.y, 24, 24}, theme::White);
        text(list, labels[i], 0, stepX + 32, steps.y + 5, i <= current ? theme::White : theme::Muted1);
    }

    std::string status;
    switch (progress.stage) {
    case world::AssetDownloadStage::Fonts: status = "Preparing fonts..."; break;
    case world::AssetDownloadStage::Resources: status = progress.received ? "Downloading Minecraft resources..." : "Connecting to Mojang..."; break;
    case world::AssetDownloadStage::Installing: status = "Installing resources..."; break;
    case world::AssetDownloadStage::Ready: status = "Ready! Opening Kestrel..."; break;
    }
    if (!error.empty()) status = "Setup could not finish. Try again below.";
    if (cancelling) status = "Cancelling download...";
    text(list, status, 0, x, y + 185, error.empty() ? theme::White : Color{255, 176, 176, 255});
    if (error.empty()) {
        auto percentage = std::to_string(int(std::clamp(progress.fraction, 0.0f, 1.0f) * 100)) + "%";
        text(list, percentage, 0, x + inner - measure(percentage, 0), y + 185, theme::White);
    }
    Rect bar {x, y + 209, inner, 18};
    list.fill(bar, theme::Divider);
    list.fill(bar.inset(2), theme::PanelDark);
    float filled = std::floor((bar.w - 4) * std::clamp(progress.fraction, 0.0f, 1.0f));
    list.fill({bar.x + 2, bar.y + 2, filled, bar.h - 4}, error.empty() ? theme::Primary : theme::Destructive);
    list.fill({bar.x + 2, bar.y + 2, filled, 3}, error.empty() ? theme::Caret : Color{230, 91, 91, 255});
    if (error.empty() && progress.stage == world::AssetDownloadStage::Installing) {
        for (int i = 0; i < 3; ++i) list.fill({x + inner - 26 + i * 9, y + 247, 4, 4},
            int(seconds * 3) % 3 == i ? theme::Caret : theme::Muted1);
    }
    std::string detail = "Downloaded once. Saved for your next launch.";
    if (!error.empty()) {
        detail = error.substr(0, 72);
        while (measure(detail, 0) > inner && !detail.empty()) detail.pop_back();
    } else if (progress.stage == world::AssetDownloadStage::Resources && progress.received) {
        detail = std::to_string(progress.received / (1024 * 1024)) + " MB downloaded. You can cancel at any time.";
    } else if (progress.stage == world::AssetDownloadStage::Installing) {
        detail = "Unpacking files. Kestrel will open automatically.";
    }
    text(list, detail, 0, x, y + 239, theme::Muted1);
    cancel = {x + inner - 116, y + 284, 116, 42};
    retry = {x, y + 284, 140, 42};
    if (!error.empty()) button(list, retry, "Try again", true, retry.contains(mouseX, mouseY), false);
    else text(list, "Kestrel will open when setup is complete.", 0, x, y + 298, theme::Muted1);
    button(list, cancel, error.empty() ? "Cancel" : "Close", false, cancel.contains(mouseX, mouseY), cancelling);
    centered(list, "Minecraft resources provided by Mojang", 0, {0, height - 35, width, 22}, theme::Muted0);
}

}
