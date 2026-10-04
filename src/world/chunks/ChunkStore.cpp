#include "world/ChunkStore.h"

#include <array>
#include <cmath>
#include <optional>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <algorithm>

namespace kestrel::world {

struct ChunkStore::Retirement {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Column> columns;
    bool stopping = false;
    std::thread worker;
    Retirement() : worker([this] {
        for (;;) {
            Column retired;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopping || !columns.empty(); });
                if (columns.empty()) return;
                retired = std::move(columns.front());
                columns.pop_front();
            }
            wake.notify_all();
        }
    }) {}
    ~Retirement() {
        { std::lock_guard lock(mutex); stopping = true; }
        wake.notify_all();
        worker.join();
    }
    void enqueue(Column column) {
        std::unique_lock lock(mutex);
        wake.wait(lock, [&] { return columns.size() < 128; });
        columns.push_back(std::move(column));
        lock.unlock();
        wake.notify_all();
    }
};

ChunkStore::ChunkStore() = default;
ChunkStore::~ChunkStore() = default;

namespace {

std::array<std::optional<DimensionRange>, 3> serverRanges;

}

void setServerDimensionHeight(int32_t dimension, int32_t minimumHeight, int32_t maximumHeight)
{
    if (dimension < 0 || dimension >= static_cast<int32_t>(serverRanges.size()) || maximumHeight <= minimumHeight) {
        return;
    }
    int32_t base = static_cast<int32_t>(std::floor(minimumHeight / 16.0));
    int32_t top = static_cast<int32_t>(std::ceil(maximumHeight / 16.0));
    serverRanges[static_cast<size_t>(dimension)] = DimensionRange { base, top - base };
}

void clearServerDimensionHeights()
{
    serverRanges.fill(std::nullopt);
}

bool vanillaDimensionRange(int32_t dimension, DimensionRange& out)
{
    if (dimension >= 0 && dimension < static_cast<int32_t>(serverRanges.size()) && serverRanges[static_cast<size_t>(dimension)]) {
        out = *serverRanges[static_cast<size_t>(dimension)];
        return true;
    }
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

std::vector<SubChunkKey> ChunkStore::sectionsOf(const ChunkKey& key) const
{
    std::vector<SubChunkKey> sections;
    auto found = columnsByKey.find(key);
    if (found == columnsByKey.end()) {
        return sections;
    }
    for (const auto& [y, subChunk] : found->second.subChunks) {
        sections.push_back({ key.dimension, key.x, y, key.z });
    }
    return sections;
}

bool ChunkStore::isDirty(const SubChunkKey& key) const
{
    return dirty.contains(key);
}

void ChunkStore::markLoaded(const ChunkKey& key)
{
    columnsByKey.try_emplace(key);
}

void ChunkStore::commit(const SubChunkKey& key, SubChunk subChunk)
{
    Column& column = columnsByKey[key.chunk()];
    auto existing = column.subChunks.find(key.y);
    if (existing != column.subChunks.end() && existing->second->storages() == subChunk.storages()) {
        return;
    }
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
    if (updates.size() == 1) {
        const BlockUpdate& update = updates.front();
        if (update.layer >= MaxStorageCount || update.x >= 16 || update.y >= 16 || update.z >= 16) return false;
        uint32_t current = existing != column->second.subChunks.end()
            ? existing->second->runtimeId(update.layer, update.x, update.y, update.z) : ImplicitAir;
        if (current == update.runtimeId) return false;
    }
    SubChunk updated = existing != column->second.subChunks.end() ? *existing->second : SubChunk {};
    if (!updated.apply(updates)) {
        return false;
    }
    commit(key, std::move(updated));
    urgent.insert(key);
    return true;
}

void ChunkStore::evict(const ChunkKey& key)
{
    evict(std::vector<ChunkKey> { key });
}

void ChunkStore::evict(const std::vector<ChunkKey>& keys)
{
    std::set<ChunkKey> removed(keys.begin(), keys.end());
    std::map<ChunkKey, int32_t> affected;
    for (const auto& key : removed) {
        auto found = columnsByKey.find(key);
        if (found == columnsByKey.end()) continue;
        for (const auto& [y, sub] : found->second.subChunks) dirty.insert({ key.dimension, key.x, y, key.z });
        if (found->second.subChunks.empty()) continue;
        int32_t highest = found->second.subChunks.rbegin()->first;
        for (int32_t dx = -1; dx <= 1; ++dx) for (int32_t dz = -1; dz <= 1; ++dz) {
            ChunkKey neighbour { key.dimension, key.x + dx, key.z + dz };
            if (removed.contains(neighbour)) continue;
            auto [entry, inserted] = affected.emplace(neighbour, highest);
            if (!inserted) entry->second = std::max(entry->second, highest);
        }
    }
    for (const auto& [key, highest] : affected) {
        auto found = columnsByKey.find(key);
        if (found == columnsByKey.end()) continue;
        for (const auto& [y, sub] : found->second.subChunks) {
            if (int64_t(y) <= int64_t(highest) + 1) dirty.insert({ key.dimension, key.x, y, key.z });
        }
    }
    for (const auto& key : removed) {
        auto node = columnsByKey.extract(key);
        if (node.empty()) continue;
        storedSubChunks -= node.mapped().subChunks.size();
        if (!retirement) retirement = std::make_unique<Retirement>();
        retirement->enqueue(std::move(node.mapped()));
    }
}

void ChunkStore::clear()
{
    evict(columns());
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

std::vector<std::pair<SubChunkKey, std::shared_ptr<const SubChunk>>> ChunkStore::allSubChunks() const
{
    std::vector<std::pair<SubChunkKey, std::shared_ptr<const SubChunk>>> list;
    for (const auto& [key, column] : columnsByKey) {
        for (const auto& [y, subChunk] : column.subChunks) {
            if (subChunk) {
                list.push_back({ { key.dimension, key.x, y, key.z }, subChunk });
            }
        }
    }
    return list;
}

void ChunkStore::markAllDirty()
{
    for (const auto& [key, column] : columnsByKey) {
        for (const auto& [y, subChunk] : column.subChunks) {
            dirty.insert({ key.dimension, key.x, y, key.z });
        }
    }
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

std::set<SubChunkKey> ChunkStore::takeUrgent()
{
    return std::exchange(urgent, {});
}

void ChunkStore::deferDirty(const SubChunkKey& key, bool isUrgent)
{
    dirty.insert(key);
    if (isUrgent) urgent.insert(key);
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
            // Every lower mesh samples this column when deciding direct sky.
            for (const auto& [y, subChunk] : column->second.subChunks) {
                if (y < key.y - 1) {
                    dirty.insert({ key.dimension, key.x + dx, y, key.z + dz });
                }
            }
        }
    }
}

}
