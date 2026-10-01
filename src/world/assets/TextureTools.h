#pragma once

#include "world/BlockAssets.h"

#include <cstdint>
#include <vector>

namespace kestrel::world {

/**
 * The RGBA image scaled to a square of the given side by picking the nearest
 * texel.
 */
std::vector<uint8_t> resizeNearest(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, uint32_t size);

/**
 * The average of the texels from x0 to x1 and y0 to y1 (both ends
 * exclusive), colour weighted by coverage, written to out as RGBA.
 */
void averageArea(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1, uint8_t out[4]);

/**
 * The image's top left square averaged down (or repeated up) to one block
 * texture layer.
 */
std::vector<uint8_t> normalizeTexture(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height);

/**
 * The square frames of a flipbook strip, stacked vertically or side by side,
 * each as one block texture layer.
 */
std::vector<std::vector<uint8_t>> sliceFrames(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height);

/**
 * Multiplies every texel by an 0xRRGGBB color and caps its alpha, the way
 * tinted overlay layers are baked.
 */
void applyTint(std::vector<uint8_t>& pixels, uint32_t rgb);

/**
 * Blends an 0xRRGGBB color into every texel by its alpha and makes it opaque,
 * which is how an overlay_color bakes into a texture that is never tinted
 * later.
 */
void applyOverlay(std::vector<uint8_t>& pixels, uint32_t rgb);

/**
 * The magenta and black checker used for blocks whose texture is missing.
 */
std::vector<uint8_t> diagnosticTexture();

/**
 * Fills the texture array with the layers and their mip chain; overlay
 * layers average their color without weighting it by alpha.
 */
void buildMips(TextureArray& array, const std::vector<std::vector<uint8_t>>& layers, const std::vector<bool>& overlayLayers, const std::vector<bool>& cutoutLayers = {});

}
