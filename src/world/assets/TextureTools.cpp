#include "TextureTools.h"

#include <algorithm>

namespace kestrel::world {

void averageArea(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1, uint8_t out[4])
{
    uint64_t color[3] {};
    uint64_t alpha = 0;
    uint64_t count = 0;
    for (uint32_t y = y0; y < y1; ++y) {
        for (uint32_t x = x0; x < x1; ++x) {
            const uint8_t* texel = rgba.data() + (size_t(y) * width + x) * 4;
            for (size_t channel = 0; channel < 3; ++channel) {
                color[channel] += uint64_t(texel[channel]) * texel[3];
            }
            alpha += texel[3];
            ++count;
        }
    }
    for (size_t channel = 0; channel < 3; ++channel) {
        out[channel] = alpha > 0 ? static_cast<uint8_t>(color[channel] / alpha) : 0;
    }
    out[3] = count > 0 ? static_cast<uint8_t>(alpha / count) : 0;
}

std::vector<uint8_t> resizeNearest(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, uint32_t size)
{
    std::vector<uint8_t> out(size_t(size) * size * 4);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const uint8_t* source = rgba.data() + (size_t(y * height / size) * width + x * width / size) * 4;
            std::copy(source, source + 4, out.data() + (size_t(y) * size + x) * 4);
        }
    }
    return out;
}

std::vector<uint8_t> normalizeTexture(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height)
{
    uint32_t side = std::min(width, height);
    std::vector<uint8_t> out(TextureSize * TextureSize * 4);
    for (uint32_t y = 0; y < TextureSize; ++y) {
        uint32_t y0 = y * side / TextureSize;
        uint32_t y1 = std::max(y0 + 1, (y + 1) * side / TextureSize);
        for (uint32_t x = 0; x < TextureSize; ++x) {
            uint32_t x0 = x * side / TextureSize;
            uint32_t x1 = std::max(x0 + 1, (x + 1) * side / TextureSize);
            uint32_t sum[4] = {};
            uint32_t count = 0;
            for (uint32_t sy = y0; sy < y1; ++sy) {
                for (uint32_t sx = x0; sx < x1; ++sx) {
                    const uint8_t* texel = rgba.data() + (size_t(sy) * width + sx) * 4;
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += texel[c];
                    }
                    ++count;
                }
            }
            for (int c = 0; c < 4; ++c) {
                out[(size_t(y) * TextureSize + x) * 4 + c] = static_cast<uint8_t>(sum[c] / count);
            }
        }
    }
    return out;
}

std::vector<std::vector<uint8_t>> sliceFrames(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height)
{
    uint32_t frameSize = std::min(width, height);
    uint32_t count = std::max(width, height) / frameSize;
    bool horizontal = width > height;
    std::vector<std::vector<uint8_t>> frames;
    for (uint32_t frame = 0; frame < count; ++frame) {
        std::vector<uint8_t> pixels(size_t(frameSize) * frameSize * 4);
        for (uint32_t row = 0; row < frameSize; ++row) {
            uint32_t x = horizontal ? frame * frameSize : 0;
            uint32_t y = horizontal ? row : frame * frameSize + row;
            const uint8_t* source = rgba.data() + (size_t(y) * width + x) * 4;
            std::copy(source, source + size_t(frameSize) * 4, pixels.data() + size_t(row) * frameSize * 4);
        }
        frames.push_back(normalizeTexture(pixels, frameSize, frameSize));
    }
    return frames;
}

void applyTint(std::vector<uint8_t>& pixels, uint32_t rgb)
{
    uint32_t tint[3] = { (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF };
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            pixels[i + c] = static_cast<uint8_t>(pixels[i + c] * tint[c] / 255);
        }
        pixels[i + 3] = static_cast<uint8_t>(std::min<uint32_t>(pixels[i + 3], 180));
    }
}

void applyOverlay(std::vector<uint8_t>& pixels, uint32_t rgb)
{
    uint32_t tint[3] = { (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF };
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        uint32_t alpha = pixels[i + 3];
        for (int c = 0; c < 3; ++c) {
            uint32_t tinted = pixels[i + c] * tint[c] / 255;
            pixels[i + c] = static_cast<uint8_t>((pixels[i + c] * (255 - alpha) + tinted * alpha) / 255);
        }
        pixels[i + 3] = 255;
    }
}

std::vector<uint8_t> diagnosticTexture()
{
    std::vector<uint8_t> out(TextureSize * TextureSize * 4);
    for (uint32_t y = 0; y < TextureSize; ++y) {
        for (uint32_t x = 0; x < TextureSize; ++x) {
            bool magenta = ((x / 8) + (y / 8)) % 2 == 0;
            uint8_t* texel = out.data() + (size_t(y) * TextureSize + x) * 4;
            texel[0] = magenta ? 248 : 0;
            texel[1] = 0;
            texel[2] = magenta ? 248 : 0;
            texel[3] = 255;
        }
    }
    return out;
}

void buildMips(TextureArray& array, const std::vector<std::vector<uint8_t>>& layers, const std::vector<bool>& overlayLayers)
{
    array.layers = static_cast<uint32_t>(layers.size());
    uint32_t size = TextureSize;
    for (uint32_t level = 0; level < TextureMipLevels; ++level) {
        std::vector<uint8_t>& target = array.mips[level];
        target.assign(size_t(size) * size * 4 * layers.size(), 0);
        for (size_t layer = 0; layer < layers.size(); ++layer) {
            uint8_t* destination = target.data() + layer * size * size * 4;
            if (level == 0) {
                std::copy(layers[layer].begin(), layers[layer].end(), destination);
                continue;
            }
            uint32_t parent = size * 2;
            const uint8_t* source = array.mips[level - 1].data() + layer * parent * parent * 4;
            bool overlay = layer < overlayLayers.size() && overlayLayers[layer];
            for (uint32_t y = 0; y < size; ++y) {
                for (uint32_t x = 0; x < size; ++x) {
                    uint32_t alphaSum = 0;
                    uint32_t colour[3] = {};
                    for (uint32_t dy = 0; dy < 2; ++dy) {
                        for (uint32_t dx = 0; dx < 2; ++dx) {
                            const uint8_t* texel = source + ((size_t(y) * 2 + dy) * parent + x * 2 + dx) * 4;
                            alphaSum += texel[3];
                            for (int c = 0; c < 3; ++c) {
                                colour[c] += uint32_t(texel[c]) * (overlay ? 255u : texel[3]);
                            }
                        }
                    }
                    uint8_t* out = destination + (size_t(y) * size + x) * 4;
                    uint32_t weight = overlay ? 4u * 255u : alphaSum;
                    for (int c = 0; c < 3; ++c) {
                        out[c] = weight ? static_cast<uint8_t>(colour[c] / weight) : 0;
                    }
                    out[3] = static_cast<uint8_t>(alphaSum / 4);
                }
            }
        }
        size /= 2;
    }
}

}
