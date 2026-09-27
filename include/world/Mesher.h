#pragma once

#include "world/BlockAssets.h"
#include "world/ChunkStore.h"
#include "world/SubChunk.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace kestrel::world {

enum class Face : uint8_t {
    NegativeX = 0,
    PositiveX = 1,
    NegativeY = 2,
    PositiveY = 3,
    NegativeZ = 4,
    PositiveZ = 5,
};

inline constexpr uint32_t QuadTinted = 1u << 31;
inline constexpr uint32_t QuadTintOverlay = 1u << 30;

/**
 * Per-corner lighting of one quad: light holds for each corner a byte of block
 * light (low nibble) and sky light (high nibble), ao holds two bits of
 * ambient occlusion per corner.
 */
struct QuadLight {
    uint32_t light = 0;
    uint32_t ao = 0;

    bool operator==(const QuadLight&) const = default;
};

/**
 * A greedy cube quad: packed geometry, material word, biome tint word
 * (QuadTinted, QuadTintOverlay and a 0xRRGGBB color) and corner lighting.
 */
struct PackedQuad {
    uint32_t geometry = 0;
    uint32_t material = 0;
    uint32_t tint = 0;
    uint32_t light = 0;
    uint32_t ao = 0;

    static PackedQuad make(uint32_t x, uint32_t y, uint32_t z, Face face, uint32_t width, uint32_t height, uint32_t material, uint32_t tint, QuadLight lighting)
    {
        return {
            x | (y << 5) | (z << 10) | (uint32_t(face) << 15) | ((width - 1) << 18) | ((height - 1) << 22),
            material,
            tint,
            lighting.light,
            lighting.ao,
        };
    }
};

static_assert(sizeof(PackedQuad) == 20);

/**
 * A model quad placed in its sub-chunk: six words of signed 16-bit positions in
 * 1/256 block, four words of 16-bit UVs in 1/4096 texture, the material word,
 * the face shade index with the tint, then the corner light and occlusion.
 */
struct ModelQuadGpu {
    std::array<uint32_t, 16> words {};
};

static_assert(sizeof(ModelQuadGpu) == 64);

struct ChunkMesh {
    std::vector<PackedQuad> cubes;
    std::vector<ModelQuadGpu> models;
    std::vector<PackedQuad> translucentCubes;
    std::vector<ModelQuadGpu> translucentModels;

    bool empty() const
    {
        return cubes.empty() && models.empty() && translucentCubes.empty() && translucentModels.empty();
    }

    size_t quadCount() const
    {
        return cubes.size() + models.size() + translucentCubes.size() + translucentModels.size();
    }
};

/**
 * Biome storages are the 3x3 horizontal neighbourhood at the same height,
 * indexed (dz + 1) * 3 + (dx + 1). Around holds the 3x3x3 sub-chunks indexed
 * (dx + 1) * 9 + (dy + 1) * 3 + (dz + 1); above holds, per horizontal
 * neighbour, the loaded sub-chunks higher than that neighbourhood, which
 * decide where the sky reaches its top.
 */
struct MeshInput {
    std::shared_ptr<const SubChunk> center;
    std::array<std::shared_ptr<const SubChunk>, 6> neighbours;
    std::array<std::shared_ptr<const PalettedStorage>, 9> biomes;
    std::array<std::shared_ptr<const SubChunk>, 27> around;
    std::array<std::vector<std::shared_ptr<const SubChunk>>, 9> above;
    std::shared_ptr<const BlockEntityMap> blockEntities;
    std::array<int32_t, 3> origin {};
    bool skyLight = true;
};

struct IdMapping {
    bool hashed = false;
    std::shared_ptr<const SequentialMap> sequential;
};

ChunkMesh meshSubChunk(const BlockAssets& assets, const IdMapping& ids, const MeshInput& input);

}
