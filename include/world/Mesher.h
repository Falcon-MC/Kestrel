#pragma once

#include "world/BlockAssets.h"
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

struct PackedQuad {
    uint32_t geometry = 0;
    uint32_t material = 0;

    static PackedQuad make(uint32_t x, uint32_t y, uint32_t z, Face face, uint32_t width, uint32_t height, uint32_t material)
    {
        return {
            x | (y << 5) | (z << 10) | (uint32_t(face) << 15) | ((width - 1) << 18) | ((height - 1) << 22),
            material,
        };
    }
};

static_assert(sizeof(PackedQuad) == 8);

/**
 * A model quad placed in its sub-chunk: six words of signed 16-bit positions in
 * 1/256 block, four words of 16-bit UVs in 1/4096 texture, the material word
 * and the face shade index.
 */
struct ModelQuadGpu {
    std::array<uint32_t, 12> words {};
};

static_assert(sizeof(ModelQuadGpu) == 48);

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

struct MeshInput {
    std::shared_ptr<const SubChunk> center;
    std::array<std::shared_ptr<const SubChunk>, 6> neighbours;
};

struct IdMapping {
    bool hashed = false;
    std::shared_ptr<const SequentialMap> sequential;
};

ChunkMesh meshSubChunk(const BlockAssets& assets, const IdMapping& ids, const MeshInput& input);

}
