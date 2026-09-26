#include "world/WorldStream.h"

#include "Protocol/Packets/LevelChunkPacket.h"
#include "Protocol/Packets/NetworkChunkPublisherUpdatePacket.h"
#include "Protocol/Packets/SubChunkPacket.h"
#include "Protocol/Packets/SubChunkRequestPacket.h"
#include "Protocol/Packets/UpdateBlockPacket.h"
#include "Protocol/Packets/UpdateSubChunkBlocksPacket.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace kestrel::world {

namespace {

constexpr uint8_t MaxRetries = 2;
constexpr auto ResponseTimeout = std::chrono::seconds(2);

int32_t floorDiv16(int32_t value)
{
    return value >= 0 ? value / 16 : -((-value + 15) / 16);
}

uint8_t local16(int32_t value)
{
    return static_cast<uint8_t>(value & 15);
}

bool chunkInView(int32_t radius, int32_t chunkX, int32_t chunkZ, int32_t centerX, int32_t centerZ)
{
    int64_t view = std::max<int64_t>(int64_t(radius) + 1, 1);
    int64_t dx = std::llabs(int64_t(centerX) - chunkX);
    int64_t dz = std::llabs(int64_t(centerZ) - chunkZ);
    if (dx > view + 1 || dz > view + 1) {
        return false;
    }
    float threshold = (static_cast<float>(view) + 1.5f) + 1.7320508f;
    return static_cast<float>(dx * dx + dz * dz) < threshold * threshold;
}

}

void WorldStream::reset(int32_t newDimension, int32_t chunkX, int32_t chunkZ)
{
    chunks.clear();
    pending.clear();
    dimension = newDimension;
    centerX = chunkX;
    centerZ = chunkZ;
}

void WorldStream::setChunkRadius(int32_t radius)
{
    chunkRadius = radius;
    retain();
}

void WorldStream::changeDimension(int32_t newDimension, int32_t chunkX, int32_t chunkZ)
{
    for (const ChunkKey& key : chunks.columns()) {
        evictColumn(key);
    }
    pending.clear();
    dimension = newDimension;
    centerX = chunkX;
    centerZ = chunkZ;
}

void WorldStream::handle(const LevelChunkPacket& packet)
{
    ChunkKey key { packet.mDimension, packet.mChunkX, packet.mChunkZ };
    if (key.dimension != dimension) {
        return;
    }

    DimensionRange range;
    if (!vanillaDimensionRange(key.dimension, range)) {
        recordError("LevelChunk for unsupported dimension " + std::to_string(key.dimension));
        return;
    }
    ++counters.levelChunks;

    if (packet.mRequestSubChunks) {
        int32_t count = packet.mSubChunkLimit < 0 ? range.subChunkCount : std::min(packet.mSubChunkLimit, range.subChunkCount);
        chunks.markLoaded(key);
        if (count > 0) {
            requestColumn(key, range.baseSubChunkY, count);
        }
        return;
    }

    if (packet.mSubChunksLength > static_cast<uint32_t>(range.subChunkCount)) {
        recordError("LevelChunk carries " + std::to_string(packet.mSubChunksLength) + " sub-chunks");
        return;
    }

    std::vector<SubChunk> decoded(packet.mSubChunksLength);
    const uint8_t* data = reinterpret_cast<const uint8_t*>(packet.mData.data());
    size_t offset = 0;
    for (uint32_t i = 0; i < packet.mSubChunksLength; ++i) {
        size_t consumed = 0;
        std::string error;
        if (!SubChunk::decode(data + offset, packet.mData.size() - offset, decoded[i], consumed, error)) {
            recordError("LevelChunk " + std::to_string(key.x) + "," + std::to_string(key.z) + ": " + error);
            return;
        }
        int32_t expectedY = range.baseSubChunkY + static_cast<int32_t>(i);
        if (decoded[i].yIndex() && *decoded[i].yIndex() != expectedY) {
            recordError("LevelChunk sub-chunk index mismatch");
            return;
        }
        offset += consumed;
    }

    pending.erase(key);
    chunks.evict(key);
    chunks.markLoaded(key);
    for (uint32_t i = 0; i < decoded.size(); ++i) {
        chunks.commit({ key.dimension, key.x, range.baseSubChunkY + static_cast<int32_t>(i), key.z }, std::move(decoded[i]));
    }
}

void WorldStream::handle(const SubChunkPacket& packet)
{
    if (packet.mDimension != dimension) {
        return;
    }

    for (const SubChunkData& entry : packet.mSubChunks) {
        SubChunkKey key {
            packet.mDimension,
            packet.mCenterPosition.x + entry.mPosition.x,
            packet.mCenterPosition.y + entry.mPosition.y,
            packet.mCenterPosition.z + entry.mPosition.z,
        };
        ++counters.subChunkReplies;

        auto column = pending.find(key.chunk());
        if (column == pending.end() || !column->second.contains(key.y)) {
            continue;
        }
        column->second.erase(key.y);
        if (column->second.empty()) {
            pending.erase(column);
        }

        switch (entry.mResult) {
        case SubChunkRequestResult::Success: {
            if (!entry.mHasData) {
                chunks.commit(key, SubChunk {});
                break;
            }
            SubChunk subChunk;
            size_t consumed = 0;
            std::string error;
            if (!SubChunk::decode(reinterpret_cast<const uint8_t*>(entry.mData.data()), entry.mData.size(), subChunk, consumed, error)) {
                recordError("SubChunk " + std::to_string(key.x) + "," + std::to_string(key.y) + "," + std::to_string(key.z) + ": " + error);
                break;
            }
            chunks.commit(key, std::move(subChunk));
            break;
        }
        case SubChunkRequestResult::SuccessAllAir:
            chunks.commit(key, SubChunk {});
            break;
        default:
            break;
        }
    }
}

void WorldStream::handle(const UpdateBlockPacket& packet)
{
    SubChunkKey key {
        dimension,
        floorDiv16(packet.mBlockPosition.x),
        floorDiv16(packet.mBlockPosition.y),
        floorDiv16(packet.mBlockPosition.z),
    };
    if (packet.mDataLayer >= MaxStorageCount) {
        return;
    }
    BlockUpdate update {
        local16(packet.mBlockPosition.x),
        local16(packet.mBlockPosition.y),
        local16(packet.mBlockPosition.z),
        static_cast<uint8_t>(packet.mDataLayer),
        packet.mRuntimeId,
    };
    if (chunks.updateBlocks(key, { update })) {
        ++counters.blockUpdates;
    }
}

void WorldStream::handle(const UpdateSubChunkBlocksPacket& packet)
{
    std::map<SubChunkKey, std::vector<BlockUpdate>> grouped;
    auto collect = [&](const std::vector<BlockChangeEntry>& entries, uint8_t layer) {
        for (const BlockChangeEntry& entry : entries) {
            SubChunkKey key {
                dimension,
                floorDiv16(entry.mPosition.x),
                floorDiv16(entry.mPosition.y),
                floorDiv16(entry.mPosition.z),
            };
            grouped[key].push_back({ local16(entry.mPosition.x), local16(entry.mPosition.y), local16(entry.mPosition.z), layer, entry.mRuntimeId });
        }
    };
    collect(packet.mStandardBlocks, 0);
    collect(packet.mExtraBlocks, 1);

    for (const auto& [key, updates] : grouped) {
        if (chunks.updateBlocks(key, updates)) {
            counters.blockUpdates += updates.size();
        }
    }
}

void WorldStream::handle(const NetworkChunkPublisherUpdatePacket& packet)
{
    centerX = floorDiv16(packet.mPosition.x);
    centerZ = floorDiv16(packet.mPosition.z);
    if (chunkRadius <= 0) {
        chunkRadius = static_cast<int32_t>(packet.mRadius / 16);
    }
    retain();
}

std::vector<std::unique_ptr<SubChunkRequestPacket>> WorldStream::takeRequests(Clock::time_point now)
{
    std::vector<std::unique_ptr<SubChunkRequestPacket>> requests;
    std::vector<ChunkKey> abandoned;

    for (auto& [key, column] : pending) {
        std::vector<int32_t> due;
        for (auto it = column.begin(); it != column.end();) {
            PendingSubChunk& entry = it->second;
            if (entry.sent && now < entry.deadline) {
                ++it;
                continue;
            }
            if (entry.attempts > MaxRetries) {
                recordError("SubChunk " + std::to_string(key.x) + "," + std::to_string(it->first) + "," + std::to_string(key.z) + " never answered");
                it = column.erase(it);
                continue;
            }
            if (entry.sent) {
                ++counters.retries;
            }
            entry.sent = true;
            entry.deadline = now + ResponseTimeout;
            ++entry.attempts;
            due.push_back(it->first);
            ++it;
        }
        if (column.empty()) {
            abandoned.push_back(key);
        }
        if (due.empty()) {
            continue;
        }

        auto request = std::make_unique<SubChunkRequestPacket>();
        request->mDimension = key.dimension;
        request->mSubChunkPosition = Vector3i(key.x, due.front(), key.z);
        for (int32_t y : due) {
            request->mPositionOffsets.push_back(Vector3i(0, y - due.front(), 0));
        }
        requests.push_back(std::move(request));
        ++counters.requestsSent;
    }

    for (const ChunkKey& key : abandoned) {
        pending.erase(key);
    }
    return requests;
}

WorldStats WorldStream::stats() const
{
    WorldStats snapshot = counters;
    snapshot.columns = chunks.columnCount();
    snapshot.subChunks = chunks.subChunkCount();
    for (const auto& [key, column] : pending) {
        snapshot.pendingSubChunks += column.size();
    }
    return snapshot;
}

void WorldStream::requestColumn(const ChunkKey& key, int32_t baseY, int32_t count)
{
    std::map<int32_t, PendingSubChunk>& column = pending[key];
    for (int32_t i = 0; i < count; ++i) {
        column.try_emplace(baseY + i);
    }
}

void WorldStream::retain()
{
    if (chunkRadius <= 0) {
        return;
    }
    std::vector<ChunkKey> stale;
    for (const ChunkKey& key : chunks.columns()) {
        if (key.dimension != dimension || !chunkInView(chunkRadius, key.x, key.z, centerX, centerZ)) {
            stale.push_back(key);
        }
    }
    for (const auto& [key, column] : pending) {
        if (key.dimension != dimension || !chunkInView(chunkRadius, key.x, key.z, centerX, centerZ)) {
            stale.push_back(key);
        }
    }
    for (const ChunkKey& key : stale) {
        evictColumn(key);
    }
}

void WorldStream::evictColumn(const ChunkKey& key)
{
    pending.erase(key);
    if (chunks.isLoaded(key)) {
        chunks.evict(key);
        ++counters.evictedColumns;
    }
}

void WorldStream::recordError(const std::string& error)
{
    ++counters.decodeErrors;
    counters.lastError = error;
}

}
