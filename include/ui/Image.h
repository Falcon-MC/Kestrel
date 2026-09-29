#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kestrel::ui {

struct ImageRef {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    bool valid = false;
};

bool decodeImage(const std::string& encoded, uint32_t& width, uint32_t& height, std::vector<uint8_t>& outRgba);
bool decodeSquareImage(const std::string& encoded, uint32_t size, std::vector<uint8_t>& outRgba);

/**
 * The RGBA pixels as a PNG file, or empty when encoding fails.
 */
std::string encodePng(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height);

// The undyed leather color, #A06540.
inline constexpr std::array<uint8_t, 3> LeatherColor { 0xA0, 0x65, 0x40 };

/**
 * Dyes a leather texture. Bedrock ships leather as a grey .tga whose alpha is
 * the dye mask rather than transparency: opaque texels take the dye, the
 * faint ones (alpha 1 to 3) are undyed trim that must still show.
 */
void applyDyeMask(std::span<uint8_t> rgba, const std::array<uint8_t, 3>& color);

}
