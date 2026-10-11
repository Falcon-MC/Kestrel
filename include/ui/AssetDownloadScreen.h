#pragma once

#include "ui/DrawList.h"
#include "world/DownloadedAssets.h"

#include <array>
#include <string>
#include <vector>

namespace kestrel::ui {

// Uses a system font and original procedural art, so it also works on an empty installation.
class AssetDownloadScreen {
public:
    explicit AssetDownloadScreen(const std::filesystem::path& font = {});
    const std::vector<uint8_t>& atlas() const { return pixels; }
    static constexpr uint32_t AtlasSize = 1024;
    void draw(DrawList& list, float width, float height, float scale,
        world::AssetDownloadProgress progress, float seconds, const std::string& error,
        bool cancelling, float mouseX, float mouseY);
    Rect cancelButton() const { return cancel; }
    Rect retryButton() const { return retry; }

private:
    struct Glyph {
        float x0, y0, x1, y1, advance;
        float u0, v0, u1, v1;
    };
    std::array<std::array<Glyph, 95>, 4> glyphs {};
    std::vector<uint8_t> pixels;
    Rect cancel, retry;
    float measure(std::string_view text, size_t face) const;
    void text(DrawList& list, std::string_view value, size_t face, float x, float y, Color color) const;
    void centered(DrawList& list, std::string_view value, size_t face, Rect area, Color color) const;
    void button(DrawList& list, Rect rect, std::string_view label, bool primary, bool hovered, bool disabled) const;
};

}
