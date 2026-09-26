#pragma once

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

class ChunkStore {
public:
    std::shared_ptr<const SubChunk> subChunk(const SubChunkKey& key) const;
    bool isLoaded(const ChunkKey& key) const;

    void markLoaded(const ChunkKey& key);
    void commit(const SubChunkKey& key, SubChunk subChunk);
    bool updateBlocks(const SubChunkKey& key, const std::vector<BlockUpdate>& updates);
    void evict(const ChunkKey& key);
    void clear();

    std::vector<ChunkKey> columns() const;
    size_t columnCount() const;
    size_t subChunkCount() const;
    std::vector<SubChunkKey> takeDirty();

private:
    struct Column {
        std::map<int32_t, std::shared_ptr<const SubChunk>> subChunks;
    };

    void markDirty(const SubChunkKey& key);

    std::map<ChunkKey, Column> columnsByKey;
    std::set<SubChunkKey> dirty;
    size_t storedSubChunks = 0;
};

}
