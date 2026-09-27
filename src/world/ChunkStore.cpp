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

std::shared_ptr<const PalettedStorage> ChunkStore::biomes(const SubChunkKey& key) const
{
    auto column = columnsByKey.find(key.chunk());
    if (column == columnsByKey.end()) {
        return nullptr;
    }
    int64_t offset = int64_t(key.y) - column->second.biomeBaseY;
    if (offset < 0 || offset >= int64_t(column->second.biomes.size())) {
        return nullptr;
    }
    return column->second.biomes[size_t(offset)];
}

std::shared_ptr<const BlockEntityMap> ChunkStore::blockEntities(const SubChunkKey& key) const
{
    auto column = columnsByKey.find(key.chunk());
    if (column == columnsByKey.end()) {
        return nullptr;
    }
    auto entry = column->second.blockEntities.find(key.y);
    return entry == column->second.blockEntities.end() ? nullptr : entry->second;
}

void ChunkStore::setBlockEntity(int32_t dimension, int32_t x, int32_t y, int32_t z, Tag data)
{
    SubChunkKey key { dimension, x >> 4, y >> 4, z >> 4 };
    auto column = columnsByKey.find(key.chunk());
    if (column == columnsByKey.end()) {
        return;
    }
    std::shared_ptr<const BlockEntityMap>& slot = column->second.blockEntities[key.y];
    BlockEntityMap updated = slot ? *slot : BlockEntityMap {};
    updated[static_cast<uint16_t>(linearIndex(uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15)))] = std::move(data);
    slot = std::make_shared<const BlockEntityMap>(std::move(updated));
    dirty.insert(key);
}

void ChunkStore::replaceBlockEntities(const SubChunkKey& key, BlockEntityMap entities)
{
    auto column = columnsByKey.find(key.chunk());
    if (column == columnsByKey.end()) {
        return;
    }
    if (entities.empty()) {
        column->second.blockEntities.erase(key.y);
    } else {
        column->second.blockEntities[key.y] = std::make_shared<const BlockEntityMap>(std::move(entities));
    }
    dirty.insert(key);
}

void ChunkStore::setBiomes(const ChunkKey& key, int32_t baseY, std::vector<std::shared_ptr<const PalettedStorage>> storages)
{
    Column& column = columnsByKey[key];
    column.biomeBaseY = baseY;
    column.biomes = std::move(storages);
    for (int32_t dx = -1; dx <= 1; ++dx) {
        for (int32_t dz = -1; dz <= 1; ++dz) {
            for (size_t i = 0; i < column.biomes.size(); ++i) {
                dirty.insert({ key.dimension, key.x + dx, baseY + int32_t(i), key.z + dz });
            }
        }
    }
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
    for (int32_t dx = -1; dx <= 1; ++dx) {
        for (int32_t dz = -1; dz <= 1; ++dz) {
            auto column = columnsByKey.find({ key.dimension, key.x + dx, key.z + dz });
            if (column == columnsByKey.end()) {
                continue;
            }
            for (int32_t dy = -1; dy <= 1; ++dy) {
                if (column->second.subChunks.contains(key.y + dy)) {
                    dirty.insert({ key.dimension, key.x + dx, key.y + dy, key.z + dz });
                }
            }
        }
    }
}

}
