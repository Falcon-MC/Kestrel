#pragma once

#include "mod/Canvas.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace kestrel::mod {

using TextureHandle = uint64_t;

struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    // Row-major RGBA8, straight alpha. Exactly width * height * 4 bytes.
    std::vector<uint8_t> pixels;
};

struct TextureInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    bool valid = false;
};

// Main-thread only; handles belong to this mod and are released on unload.
// Zero/false/empty means invalid input.
class Textures {
public:
    virtual ~Textures() = default;

    virtual bool supported() const = 0;
    virtual TextureHandle load(const std::filesystem::path& path) = 0;
    virtual TextureHandle decode(std::span<const uint8_t> encoded) = 0;
    virtual TextureHandle create(Image image) = 0;
    virtual TextureInfo info(TextureHandle handle) const = 0;
    virtual Image read(TextureHandle handle) const = 0;
    virtual bool update(TextureHandle handle, Image image) = 0;
    virtual bool updateRegion(TextureHandle handle, uint32_t x, uint32_t y, Image image) = 0;
    // Draw only inside a HUD/UI render callback. Uses its Canvas clip and GUI scale.
    // Pixel changes appear after the next UI atlas upload, normally the next frame.
    virtual bool draw(Canvas& canvas, TextureHandle handle, Rect rect, Color tint = { 255, 255, 255, 255 }) = 0;
    virtual bool destroy(TextureHandle handle) = 0;
    virtual void clear() = 0;
};

}
