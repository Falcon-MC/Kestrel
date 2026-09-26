#include "world/ChunkStore.h"

namespace kestrel::world {

bool vanillaDimensionRange(int32_t dimension, DimensionRange& out)
{
    switch (dimension) {
    case 0:
        out = { -4, 24 };
        return true;
    case 1:
        out = { 0, 8 };
        return true;
    case 2:
        out = { 0, 16 };
        return true;
    default:
        return false;
    }
}

std::shared_ptr<const SubChunk> ChunkStore::subChunk(const SubChunkKey& key) const
{
    auto column = columnsByKey.find(key.chunk());
    if (column == columnsByKey.end()) {
        return nullptr;
    }
    auto entry = column->second.subChunks.find(key.y);
    return entry == column->second.subChunks.end() ? nullptr : entry->second;
}

bool ChunkStore::isLoaded(const ChunkKey& key) const
{
    return columnsByKey.contains(key);
}

void ChunkStore::markLoaded(const ChunkKey& key)
{
    columnsByKey.try_emplace(key);
}

void ChunkStore::commit(const SubChunkKey& key, SubChunk subChunk)
{
    Column& column = columnsByKey[key.chunk()];
    auto existing = column.subChunks.find(key.y);
    if (subChunk.empty()) {
        if (existing != column.subChunks.end()) {
            column.subChunks.erase(existing);
            --storedSubChunks;
        }
    } else if (existing != column.subChunks.end()) {
        existing->second = std::make_shared<const SubChunk>(std::move(subChunk));
    } else {
        column.subChunks.emplace(key.y, std::make_shared<const SubChunk>(std::move(subChunk)));
        ++storedSubChunks;
    }
    markDirty(key);
}

bool ChunkStore::updateBlocks(const SubChunkKey& key, const std::vector<BlockUpdate>& updates)
{
    auto column = columnsByKey.find(key.chunk());
    if (column == columnsByKey.end() || updates.empty()) {
        return false;
    }

    auto existing = column->second.subChunks.find(key.y);
    SubChunk updated = existing != column->second.subChunks.end() ? *existing->second : SubChunk {};
    updated.apply(updates);
    commit(key, std::move(updated));
    return true;
}

void ChunkStore::evict(const ChunkKey& key)
{
    auto column = columnsByKey.find(key);
    if (column == columnsByKey.end()) {
        return;
    }
    for (const auto& [y, subChunk] : column->second.subChunks) {
        markDirty({ key.dimension, key.x, y, key.z });
    }
    storedSubChunks -= column->second.subChunks.size();
    columnsByKey.erase(column);
}

void ChunkStore::clear()
{
    for (const auto& [key, column] : columnsByKey) {
        for (const auto& [y, subChunk] : column.subChunks) {
            dirty.insert({ key.dimension, key.x, y, key.z });
        }
    }
    columnsByKey.clear();
    storedSubChunks = 0;
}

std::vector<ChunkKey> ChunkStore::columns() const
{
    std::vector<ChunkKey> keys;
    keys.reserve(columnsByKey.size());
    for (const auto& [key, column] : columnsByKey) {
        keys.push_back(key);
    }
    return keys;
}

size_t ChunkStore::columnCount() const
{
    return columnsByKey.size();
}

size_t ChunkStore::subChunkCount() const
{
    return storedSubChunks;
}

std::vector<SubChunkKey> ChunkStore::takeDirty()
{
    std::vector<SubChunkKey> keys(dirty.begin(), dirty.end());
    dirty.clear();
    return keys;
}

void ChunkStore::markDirty(const SubChunkKey& key)
{
    dirty.insert(key);
    dirty.insert({ key.dimension, key.x - 1, key.y, key.z });
    dirty.insert({ key.dimension, key.x + 1, key.y, key.z });
    dirty.insert({ key.dimension, key.x, key.y - 1, key.z });
    dirty.insert({ key.dimension, key.x, key.y + 1, key.z });
    dirty.insert({ key.dimension, key.x, key.y, key.z - 1 });
    dirty.insert({ key.dimension, key.x, key.y, key.z + 1 });
}

}
