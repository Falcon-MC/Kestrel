#pragma once

#include "render/Renderer.h"

#include <cstring>
#include <span>

namespace kestrel {

inline std::vector<uint8_t> opaqueTextureLayers(const BlockTextureUpload& textures)
{
    std::vector<uint8_t> opaque(textures.layers, 1);
    for (uint32_t mip = 0; mip < textures.mipLevels; ++mip) {
        size_t side = mip < 32 ? std::max(textures.size >> mip, 1u) : 1u;
        size_t pixels = side * side;
        for (size_t layer = 0; layer < opaque.size(); ++layer) {
            if (!textures.mips || !textures.mips[mip]) {
                opaque[layer] = 0;
                continue;
            }
            const uint8_t* data = textures.mips[mip] + layer * pixels * 4;
            for (size_t pixel = 0; opaque[layer] && pixel < pixels; ++pixel) {
                opaque[layer] &= data[pixel * 4 + 3] == 255;
            }
        }
    }
    if (!textures.size || !textures.mipLevels) std::fill(opaque.begin(), opaque.end(), 0);
    return opaque;
}

inline bool solidCubeMaterial(uint32_t material, uint32_t tint, std::span<const uint8_t> opaque)
{
    size_t first = material & 0x1fffu;
    size_t frames = ((material >> 15) & 0x3fu) + 1;
    if (first >= opaque.size() || frames > opaque.size() - first) return false;
    // An overlay's alpha is a tint mask; the shader restores its surface coverage.
    if ((tint & 0xc0000000u) == 0xc0000000u) return true;
    for (size_t frame = 0; frame < frames; ++frame) {
        if (!opaque[first + frame]) return false;
    }
    return true;
}

struct OpaqueCubeRuns {
    std::array<uint32_t, 8> offsets {};
    std::vector<uint32_t> words;
};

inline OpaqueCubeRuns partitionCubeQuads(const void* source, uint32_t count, std::span<const uint8_t> opaque)
{
    OpaqueCubeRuns result;
    const auto* data = static_cast<const uint8_t*>(source);
    auto bucket = [&](const std::array<uint32_t, 5>& quad) {
        uint32_t face = (quad[0] >> 15) & 7u;
        return face < 6 && solidCubeMaterial(quad[1], quad[2], opaque) ? face : 6u;
    };
    for (uint32_t index = 0; index < count; ++index) {
        std::array<uint32_t, 5> quad;
        std::memcpy(quad.data(), data + size_t(index) * CubeQuadBytes, CubeQuadBytes);
        ++result.offsets[bucket(quad) + 1];
    }
    for (size_t index = 1; index < result.offsets.size(); ++index) result.offsets[index] += result.offsets[index - 1];
    auto next = result.offsets;
    result.words.resize(size_t(count) * 5);
    for (uint32_t index = 0; index < count; ++index) {
        std::array<uint32_t, 5> quad;
        std::memcpy(quad.data(), data + size_t(index) * CubeQuadBytes, CubeQuadBytes);
        std::memcpy(result.words.data() + size_t(next[bucket(quad)]++) * 5, quad.data(), CubeQuadBytes);
    }
    return result;
}

inline uint8_t facingCubeDirections(const std::array<int32_t, 3>& origin, const std::array<double, 3>& camera)
{
    uint8_t mask = 63;
    for (size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(camera[axis])) return 63;
        if (camera[axis] <= origin[axis]) mask &= uint8_t(~(1u << (axis * 2 + 1)));
        if (camera[axis] >= double(origin[axis]) + 16) mask &= uint8_t(~(1u << (axis * 2)));
    }
    return mask;
}

struct OpaqueModelRuns {
    uint32_t solidCount = 0;
    std::vector<uint32_t> words;
};

inline OpaqueModelRuns partitionModelQuads(const void* source, uint32_t count, std::span<const uint8_t> opaque)
{
    OpaqueModelRuns result;
    const auto* data = static_cast<const uint8_t*>(source);
    auto solid = [&](const std::array<uint32_t, 16>& quad) {
        // Entity-textured block models use another atlas and its material rules.
        return !(quad[11] & 0x20u) && solidCubeMaterial(quad[10], 0, opaque);
    };
    for (uint32_t index = 0; index < count; ++index) {
        std::array<uint32_t, 16> quad;
        std::memcpy(quad.data(), data + size_t(index) * ModelQuadBytes, ModelQuadBytes);
        result.solidCount += solid(quad);
    }
    uint32_t nextSolid = 0, nextCutout = result.solidCount;
    result.words.resize(size_t(count) * 16);
    for (uint32_t index = 0; index < count; ++index) {
        std::array<uint32_t, 16> quad;
        std::memcpy(quad.data(), data + size_t(index) * ModelQuadBytes, ModelQuadBytes);
        uint32_t destination = solid(quad) ? nextSolid++ : nextCutout++;
        std::memcpy(result.words.data() + size_t(destination) * 16, quad.data(), ModelQuadBytes);
    }
    return result;
}

template<class Draw>
void drawSolidCubeRuns(const std::array<uint32_t, 8>& offsets, uint8_t mask, Draw&& draw)
{
    uint32_t begin = 0, end = 0;
    for (size_t face = 0; face < 6; ++face) {
        if (offsets[face] == offsets[face + 1]) continue;
        if (mask & (1u << face)) {
            if (begin == end) begin = offsets[face];
            end = offsets[face + 1];
        } else if (begin != end) {
            draw(begin, end - begin);
            begin = end = offsets[face + 1];
        }
    }
    if (begin != end) draw(begin, end - begin);
}

}
