#include "client/ParticleRenderer.h"

#include "ui/Image.h"
#include "world/BlockAssets.h"
#include "world/PackSource.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace kestrel {

namespace {

constexpr uint32_t EntityQuadFlag = 1u << 5;
constexpr uint32_t AdditiveQuadFlag = 1u << 6;
constexpr uint32_t ShadedQuadFlag = 1u << 8;
constexpr uint32_t UpFace = 4;
constexpr uint32_t MaxParticleTiles = world::MaxEntityTiles;

struct Corner {
    std::array<float, 3> position {};
    std::array<float, 2> uv {};
};

int16_t toShort(double value)
{
    double clamped = std::clamp(std::round(value), -32768.0, 32767.0);
    return static_cast<int16_t>(clamped);
}

world::ModelQuadGpu pack(const std::array<Corner, 4>& corners, uint32_t layer, uint32_t shadeWord, uint32_t lightWord)
{
    world::ModelQuadGpu gpu;
    std::array<int16_t, 12> positions {};
    for (size_t corner = 0; corner < 4; ++corner) {
        for (size_t axis = 0; axis < 3; ++axis) {
            positions[corner * 3 + axis] = toShort(corners[corner].position[axis]);
        }
    }
    for (size_t word = 0; word < 6; ++word) {
        gpu.words[word] = uint32_t(uint16_t(positions[word * 2])) | (uint32_t(uint16_t(positions[word * 2 + 1])) << 16);
    }
    for (size_t corner = 0; corner < 4; ++corner) {
        uint32_t u = static_cast<uint32_t>(std::clamp(corners[corner].uv[0] * 4096.0f + 0.5f, 0.0f, 65535.0f));
        uint32_t v = static_cast<uint32_t>(std::clamp(corners[corner].uv[1] * 4096.0f + 0.5f, 0.0f, 65535.0f));
        gpu.words[6 + corner] = u | (v << 16);
    }
    gpu.words[10] = layer;
    gpu.words[11] = shadeWord;
    gpu.words[12] = lightWord;
    return gpu;
}

/**
 * Nearest neighbour stretch of an image onto a square of the given size.
 */
std::vector<uint8_t> stretch(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, uint32_t size)
{
    std::vector<uint8_t> out(size_t(size) * size * 4);
    for (uint32_t y = 0; y < size; ++y) {
        uint32_t sourceY = std::min(y * height / size, height - 1);
        for (uint32_t x = 0; x < size; ++x) {
            uint32_t sourceX = std::min(x * width / size, width - 1);
            const uint8_t* texel = rgba.data() + (size_t(sourceY) * width + sourceX) * 4;
            std::copy(texel, texel + 4, out.data() + (size_t(y) * size + x) * 4);
        }
    }
    return out;
}

/**
 * A quad sampling a texture spread over a grid of layers, cut along the tile
 * edges its UV rectangle crosses; every piece samples the layer under it.
 */
void appendTiled(const std::array<Corner, 4>& corners, uint32_t layer, const world::EntityTileGrid& grid, uint32_t shadeWord, uint32_t lightWord, std::vector<world::ModelQuadGpu>& out)
{
    if (grid.single()) {
        out.push_back(pack(corners, layer, shadeWord, lightWord));
        return;
    }
    uint32_t tilesX = grid.tilesX;
    uint32_t tilesY = grid.tilesY;
    std::array<Corner, 4> scaled = corners;
    for (Corner& corner : scaled) {
        corner.uv[0] *= grid.coverX;
        corner.uv[1] *= grid.coverY;
    }
    auto cuts = [](float a, float b, uint32_t tiles) {
        std::vector<float> list { 0.0f, 1.0f };
        if (std::abs(b - a) < 1.0e-6f) {
            return list;
        }
        float count = static_cast<float>(tiles);
        float high = std::max(a, b) * count;
        for (float edge = std::floor(std::min(a, b) * count) + 1.0f; edge < high; edge += 1.0f) {
            float t = (edge / count - a) / (b - a);
            if (t > 1.0e-4f && t < 1.0f - 1.0e-4f) {
                list.push_back(t);
            }
        }
        std::sort(list.begin(), list.end());
        return list;
    };
    std::vector<float> across = cuts(scaled[0].uv[0], scaled[1].uv[0], tilesX);
    std::vector<float> down = cuts(scaled[0].uv[1], scaled[3].uv[1], tilesY);
    auto at = [&](float s, float t) {
        Corner point;
        float weights[4] = { (1.0f - s) * (1.0f - t), s * (1.0f - t), s * t, (1.0f - s) * t };
        for (size_t corner = 0; corner < 4; ++corner) {
            for (size_t axis = 0; axis < 3; ++axis) {
                point.position[axis] += scaled[corner].position[axis] * weights[corner];
            }
            point.uv[0] += scaled[corner].uv[0] * weights[corner];
            point.uv[1] += scaled[corner].uv[1] * weights[corner];
        }
        return point;
    };
    for (size_t row = 0; row + 1 < down.size(); ++row) {
        for (size_t column = 0; column + 1 < across.size(); ++column) {
            std::array<Corner, 4> piece {
                at(across[column], down[row]), at(across[column + 1], down[row]),
                at(across[column + 1], down[row + 1]), at(across[column], down[row + 1]),
            };
            float centerU = (piece[0].uv[0] + piece[2].uv[0]) * 0.5f;
            float centerV = (piece[0].uv[1] + piece[2].uv[1]) * 0.5f;
            uint32_t tileX = std::min(static_cast<uint32_t>(std::max(centerU, 0.0f) * float(tilesX)), tilesX - 1);
            uint32_t tileY = std::min(static_cast<uint32_t>(std::max(centerV, 0.0f) * float(tilesY)), tilesY - 1);
            for (Corner& corner : piece) {
                corner.uv[0] = std::clamp(corner.uv[0] * float(tilesX) - float(tileX), 0.0f, 1.0f);
                corner.uv[1] = std::clamp(corner.uv[1] * float(tilesY) - float(tileY), 0.0f, 1.0f);
            }
            out.push_back(pack(piece, layer + tileY * tilesX + tileX, shadeWord, lightWord));
        }
    }
}

std::unordered_map<std::string, world::EntityTileGrid>& tileGrids()
{
    static std::unordered_map<std::string, world::EntityTileGrid> grids;
    return grids;
}

}

/**
 * Reads every texture the library's effects sample through the pack stack
 * entity textures come from, the server packs over the game, and lays each
 * one onto entity texture layers from firstLayer on: a texture that fits one
 * layer is stretched onto it, a bigger one is copied texel for texel over a
 * grid of layers, the same way entity textures are. Textures that no longer
 * fit in maxLayers are left out.
 */
std::vector<uint8_t> ParticleRenderer::registerTextures(const world::ParticleLibrary& library, const std::vector<std::shared_ptr<const world::PackFiles>>& packs, uint32_t firstLayer, uint32_t layerSize, uint32_t maxLayers)
{
    std::vector<uint8_t> pixels;
    layers.clear();
    tileGrids().clear();
    std::set<std::string> paths;
    for (const auto& [identifier, effect] : library.all()) {
        if (!effect.texture.empty()) {
            paths.insert(effect.texture);
        }
    }
    if (paths.empty() || layerSize == 0) {
        return pixels;
    }
    std::filesystem::path root = world::PackSource::locateVanilla();
    if (root.empty()) {
        return pixels;
    }
    world::PackSource source(root);
    source.setOverlays(packs);
    uint32_t next = firstLayer;
    for (const std::string& path : paths) {
        std::string encoded;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;
        if (!source.readTexture(path, encoded) || !ui::decodeImage(encoded, width, height, rgba) || width == 0 || height == 0) {
            continue;
        }
        uint32_t tilesX = std::clamp<uint32_t>((width + layerSize - 1) / layerSize, 1, MaxParticleTiles);
        uint32_t tilesY = std::clamp<uint32_t>((height + layerSize - 1) / layerSize, 1, MaxParticleTiles);
        if (next - firstLayer + tilesX * tilesY > maxLayers) {
            continue;
        }
        if (tilesX * tilesY == 1) {
            std::vector<uint8_t> layer = stretch(rgba, width, height, layerSize);
            pixels.insert(pixels.end(), layer.begin(), layer.end());
        } else {
            uint32_t spanX = tilesX * layerSize;
            uint32_t spanY = tilesY * layerSize;
            uint32_t usedX = std::min(width, spanX);
            uint32_t usedY = std::min(height, spanY);
            static constexpr uint8_t Clear[4] = { 0, 0, 0, 0 };
            for (uint32_t tileY = 0; tileY < tilesY; ++tileY) {
                for (uint32_t tileX = 0; tileX < tilesX; ++tileX) {
                    for (uint32_t y = 0; y < layerSize; ++y) {
                        uint32_t gridY = tileY * layerSize + y;
                        for (uint32_t x = 0; x < layerSize; ++x) {
                            uint32_t gridX = tileX * layerSize + x;
                            const uint8_t* texel = Clear;
                            if (gridX < usedX && gridY < usedY) {
                                texel = rgba.data() + (size_t(gridY * height / usedY) * width + gridX * width / usedX) * 4;
                            }
                            pixels.insert(pixels.end(), texel, texel + 4);
                        }
                    }
                }
            }
            tileGrids()[path] = world::EntityTileGrid { tilesX, tilesY, float(usedX) / float(spanX), float(usedY) / float(spanY) };
        }
        layers[path] = next;
        next += tilesX * tilesY;
    }
    return pixels;
}

/**
 * Packs every particle as a camera facing entity quad around its center, in
 * 1/256 block relative to origin, lit by the particle's own light; alpha
 * tested particles go to opaque, blended and additive ones to blended.
 */
void ParticleRenderer::build(const std::vector<world::ParticleQuad>& quads, const std::array<double, 3>& origin, std::vector<world::ModelQuadGpu>& opaque, std::vector<world::ModelQuadGpu>& blended) const
{
    const auto& grids = tileGrids();
    for (const world::ParticleQuad& quad : quads) {
        auto found = layers.find(quad.texture);
        if (found == layers.end()) {
            continue;
        }
        if (quad.color[3] <= 0.0f) {
            continue;
        }
        std::array<double, 3> center {};
        for (size_t axis = 0; axis < 3; ++axis) {
            center[axis] = (quad.center[axis] - origin[axis]) * 256.0;
        }
        static constexpr float SideX[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
        static constexpr float SideY[4] = { 1.0f, 1.0f, -1.0f, -1.0f };
        std::array<Corner, 4> corners;
        for (size_t corner = 0; corner < 4; ++corner) {
            for (size_t axis = 0; axis < 3; ++axis) {
                double offset = (double(quad.right[axis]) * SideX[corner] + double(quad.up[axis]) * SideY[corner]) * 256.0;
                corners[corner].position[axis] = static_cast<float>(center[axis] + offset);
            }
        }
        corners[0].uv = { quad.uv[0], quad.uv[1] };
        corners[1].uv = { quad.uv[2], quad.uv[1] };
        corners[2].uv = { quad.uv[2], quad.uv[3] };
        corners[3].uv = { quad.uv[0], quad.uv[3] };
        bool additive = quad.material == world::ParticleMaterial::Add;
        uint32_t shadeWord = UpFace | EntityQuadFlag | (additive ? AdditiveQuadFlag : ShadedQuadFlag);
        uint32_t level = quad.light;
        uint32_t lightWord = level | (level << 8) | (level << 16) | (level << 24);
        auto grid = grids.find(quad.texture);
        world::EntityTileGrid tiles = grid == grids.end() ? world::EntityTileGrid {} : grid->second;
        std::vector<world::ModelQuadGpu>& target = quad.material == world::ParticleMaterial::AlphaTest ? opaque : blended;
        appendTiled(corners, found->second, tiles, shadeWord, lightWord, target);
    }
}

}
