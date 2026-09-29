#include "ui/Image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

#include <algorithm>

namespace kestrel::ui {

bool decodeImage(const std::string& encoded, uint32_t& width, uint32_t& height, std::vector<uint8_t>& outRgba)
{
    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()), static_cast<int>(encoded.size()), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        stbi_image_free(pixels);
        return false;
    }
    width = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);
    outRgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return true;
}

bool decodeSquareImage(const std::string& encoded, uint32_t size, std::vector<uint8_t>& outRgba)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()), static_cast<int>(encoded.size()), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return false;
    }

    int side = std::min(width, height);
    int offsetX = (width - side) / 2;
    int offsetY = (height - side) / 2;
    outRgba.assign(static_cast<size_t>(size) * size * 4, 0);

    for (uint32_t y = 0; y < size; ++y) {
        int sourceY0 = offsetY + static_cast<int>(static_cast<uint64_t>(y) * side / size);
        int sourceY1 = std::max(sourceY0 + 1, offsetY + static_cast<int>(static_cast<uint64_t>(y + 1) * side / size));
        for (uint32_t x = 0; x < size; ++x) {
            int sourceX0 = offsetX + static_cast<int>(static_cast<uint64_t>(x) * side / size);
            int sourceX1 = std::max(sourceX0 + 1, offsetX + static_cast<int>(static_cast<uint64_t>(x + 1) * side / size));
            uint32_t sum[4] = {};
            uint32_t count = 0;
            for (int sy = sourceY0; sy < sourceY1; ++sy) {
                for (int sx = sourceX0; sx < sourceX1; ++sx) {
                    const stbi_uc* texel = pixels + (static_cast<size_t>(sy) * width + sx) * 4;
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += texel[c];
                    }
                    ++count;
                }
            }
            uint8_t* target = outRgba.data() + (static_cast<size_t>(y) * size + x) * 4;
            for (int c = 0; c < 4; ++c) {
                target[c] = static_cast<uint8_t>(sum[c] / count);
            }
        }
    }

    stbi_image_free(pixels);
    return true;
}

void applyDyeMask(std::span<uint8_t> rgba, const std::array<uint8_t, 3>& color)
{
    for (size_t pixel = 0; pixel + 3 < rgba.size(); pixel += 4) {
        uint32_t mask = rgba[pixel + 3];
        if (mask == 0) {
            continue;
        }
        for (size_t channel = 0; channel < 3; ++channel) {
            uint32_t base = rgba[pixel + channel];
            uint32_t dyed = base * color[channel] / 255;
            rgba[pixel + channel] = static_cast<uint8_t>((base * (255 - mask) + dyed * mask) / 255);
        }
        rgba[pixel + 3] = 255;
    }
}

std::string encodePng(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height)
{
    std::string encoded;
    if (width == 0 || height == 0 || rgba.size() < size_t(width) * height * 4) {
        return encoded;
    }
    auto append = [](void* context, void* data, int size) {
        static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<size_t>(size));
    };
    if (!stbi_write_png_to_func(append, &encoded, static_cast<int>(width), static_cast<int>(height), 4, rgba.data(), static_cast<int>(width * 4))) {
        encoded.clear();
    }
    return encoded;
}

}
