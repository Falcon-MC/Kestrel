#pragma once

#include "Core/NBT/Tag.h"
#include "world/SubChunk.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace kestrel::world {

struct ChunkKey {
    int32_t dimension = 0;
    int32_t x = 0;
    int32_t z = 0;

    auto operator<=>(const ChunkKey&) const = default;
};

struct SubChunkKey {
    int32_t dimension = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    auto operator<=>(const SubChunkKey&) const = default;

    ChunkKey chunk() const
    {
        return { dimension, x, z };
    }
};

struct DimensionRange {
    int32_t baseSubChunkY = 0;
    int32_t subChunkCount = 0;
};

bool vanillaDimensionRange(int32_t dimension, DimensionRange& out);
void setServerDimensionHeight(int32_t dimension, int32_t minimumHeight, int32_t maximumHeight);
void clearServerDimensionHeights();

/**
 * Block entity compounds of one sub-chunk, keyed by the block's linear index
 * inside it.
 */
using BlockEntityMap = std::map<uint16_t, Tag>;

class ChunkStore {
public:
    std::shared_ptr<const SubChunk> subChunk(const SubChunkKey& key) const;
    std::shared_ptr<const PalettedStorage> biomes(const SubChunkKey& key) const;
    std::shared_ptr<const BlockEntityMap> blockEntities(const SubChunkKey& key) const;
    void setBlockEntity(int32_t dimension, int32_t x, int32_t y, int32_t z, Tag data);
    void replaceBlockEntities(const SubChunkKey& key, BlockEntityMap entities);
    bool isLoaded(const ChunkKey& key) const;

    void markLoaded(const ChunkKey& key);
    void setBiomes(const ChunkKey& key, int32_t baseY, std::vector<std::shared_ptr<const PalettedStorage>> storages);
    void commit(const SubChunkKey& key, SubChunk subChunk);
    bool updateBlocks(const SubChunkKey& key, const std::vector<BlockUpdate>& updates);
    void evict(const ChunkKey& key);
    void clear();

    std::vector<ChunkKey> columns() const;

    /**
     * Every loaded sub-chunk with its key; sub-chunks never change once
     * committed, so they can be read from any thread afterwards.
     */
    std::vector<std::pair<SubChunkKey, std::shared_ptr<const SubChunk>>> allSubChunks() const;

    /**
     * Asks for a new mesh of every loaded sub-chunk, after something that
     * changes how blocks are drawn.
     */
    void markAllDirty();
    size_t columnCount() const;
    size_t subChunkCount() const;
    std::vector<SubChunkKey> takeDirty();
    void deferDirty(const SubChunkKey& key, bool isUrgent = false);

    /**
     * The sub-chunks whose blocks changed one by one since the last call,
     * which want their mesh ahead of freshly loaded terrain.
     */
    std::set<SubChunkKey> takeUrgent();

private:
    struct Column {
        std::map<int32_t, std::shared_ptr<const SubChunk>> subChunks;
        int32_t biomeBaseY = 0;
        std::vector<std::shared_ptr<const PalettedStorage>> biomes;
        std::map<int32_t, std::shared_ptr<const BlockEntityMap>> blockEntities;
    };

    void markDirty(const SubChunkKey& key);

    std::map<ChunkKey, Column> columnsByKey;
    std::set<SubChunkKey> dirty;
    std::set<SubChunkKey> urgent;
    size_t storedSubChunks = 0;
};

}
