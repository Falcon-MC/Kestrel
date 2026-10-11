#include "world/WorldStream.h"
#include "client/DebugLog.h"

#include "Core/NBT/NbtIo.h"
#include "Core/Utility/ReadOnlyBinaryStream.h"
#include "Protocol/Packets/BlockActorDataPacket.h"
#include "Protocol/Packets/LevelChunkPacket.h"
#include "Protocol/Packets/NetworkChunkPublisherUpdatePacket.h"
#include "Protocol/Packets/SubChunkPacket.h"
#include "Protocol/Packets/SubChunkRequestPacket.h"
#include "Protocol/Packets/UpdateBlockPacket.h"
#include "Protocol/Packets/UpdateBlockSyncedPacket.h"
#include "Protocol/Packets/UpdateSubChunkBlocksPacket.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace kestrel::world {

namespace {

constexpr uint8_t MaxRetries = 2;
constexpr size_t MaxInFlightColumns = 32;
constexpr auto ResponseTimeout = std::chrono::seconds(2);
constexpr auto CohortSettleTime = std::chrono::seconds(2);

int32_t floorDiv16(int32_t value)
{
    return value >= 0 ? value / 16 : -((-value + 15) / 16);
}

int32_t tagInt(const Tag& tag, const char* key)
{
    const Tag* value = tag.get(key);
    if (!value) {
        return 0;
    }
    switch (value->getType()) {
    case Tag::Type::Byte:
        return value->asByte();
    case Tag::Type::Short:
        return value->asShort();
    case Tag::Type::Int:
        return value->asInt();
    default:
        return 0;
    }
}

/**
 * The block entity compounds trailing chunk data: back-to-back network NBT
 * compounds, each carrying its world position in x, y and z.
 */
std::vector<Tag> readBlockEntities(const uint8_t* data, size_t size)
{
    std::vector<Tag> entities;
    if (size == 0) {
        return entities;
    }
    ReadOnlyBinaryStream stream(std::string(reinterpret_cast<const char*>(data), size));
    try {
        while (stream.getRemainingLength() > 0) {
            Tag tag = NbtIo::readTag(stream, NbtVariant::Network);
            if (tag.getType() == Tag::Type::Compound) {
                entities.push_back(std::move(tag));
            }
        }
    } catch (const std::exception&) {
    }
    return entities;
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

/**
 * Reads a column's biome storages the way the game does: a column that ends
 * early, or a storage it cannot read, stops the reading without losing the
 * chunk, and the sections left without a storage repeat the last one read,
 * or take plains when none was.
 */
bool decodeBiomes(ByteReader& reader, int32_t count, std::vector<std::shared_ptr<const PalettedStorage>>& out, std::string& error)
{
    constexpr uint32_t PlainsBiome = 1;
    out.clear();
    for (int32_t i = 0; i < count && reader.remaining() > 0; ++i) {
        uint8_t header = 0;
        if (!reader.readByte(header, error, "biome palette header")) {
            break;
        }
        if (header == 0xFF) {
            if (out.empty()) {
                break;
            }
            out.push_back(out.back());
            continue;
        }
        PalettedStorage storage;
        if (!PalettedStorage::decodeWithHeader(reader, header, storage, error, {}, true)) {
            reader.skipToEnd();
            break;
        }
        out.push_back(std::make_shared<const PalettedStorage>(std::move(storage)));
    }
    error.clear();
    auto fill = out.empty() ? std::make_shared<const PalettedStorage>(PalettedStorage::uniform(PlainsBiome)) : out.back();
    while (out.size() < size_t(std::max(count, int32_t(0)))) {
        out.push_back(fill);
    }
    return true;
}

}

void WorldStream::reset(int32_t newDimension, int32_t chunkX, int32_t chunkZ)
{
    cancelDecoding();
    chunks.clear();
    pending.clear();
    lastRequestPoll = {};
    requestsPaused = false;
    dimension = newDimension;
    centerX = chunkX;
    centerZ = chunkZ;
}

void WorldStream::setChunkRadius(int32_t radius)
{
    if (decoding.empty()) applyChunkRadius(radius);
    else decoding.append(0, [this, radius] { applyChunkRadius(radius); });
}

void WorldStream::applyChunkRadius(int32_t radius)
{
    chunkRadius = radius;
    retain();
}

std::vector<SubChunkKey> WorldStream::pendingKeys() const
{
    std::vector<SubChunkKey> keys;
    for (const auto& [column, entries] : pending) {
        for (const auto& [y, entry] : entries) keys.push_back({ column.dimension, column.x, y, column.z });
    }
    for (const auto& [key, count] : decodingReplies) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

bool WorldStream::subChunkPending(const SubChunkKey& key) const
{
    auto column = pending.find(key.chunk());
    return column != pending.end() && column->second.contains(key.y);
}

void WorldStream::changeDimension(int32_t newDimension, int32_t chunkX, int32_t chunkZ)
{
    cancelDecoding();
    counters.evictedColumns += chunks.columnCount();
    chunks.clear();
    pending.clear();
    lastRequestPoll = {};
    requestsPaused = false;
    dimension = newDimension;
    centerX = chunkX;
    centerZ = chunkZ;
}

void WorldStream::handle(std::shared_ptr<const LevelChunkPacket> packet)
{
    ChunkKey key { packet->mDimension, packet->mChunkX, packet->mChunkZ };
    if (key.dimension != dimension) return;
    DimensionRange range;
    if (!vanillaDimensionRange(key.dimension, range)) {
        decoding.append(0, [this, key] { recordError("LevelChunk for unsupported dimension " + std::to_string(key.dimension)); });
        return;
    }
    const size_t bytes = packet->mData.size() + packet->mBlobIds.size() * sizeof(uint64_t)
        + size_t(std::max(range.subChunkCount, 0)) * 4096;
    ++counters.levelChunks;
    decoding.submit(bytes, [this, packet = std::move(packet), key, range, resolver = blockPaletteResolver] {
        const BlockPaletteResolver emptyResolver;
        const BlockPaletteResolver& palette = resolver ? *resolver : emptyResolver;
        struct Result {
            std::vector<SubChunk> chunks;
            std::vector<int32_t> y;
            std::vector<std::shared_ptr<const PalettedStorage>> biomes;
            std::vector<Tag> entities;
            std::string error;
            int32_t requestCount = 0;
            bool request = false;
        } result;
        result.request = packet->mRequestSubChunks;
        result.requestCount = packet->mSubChunkLimit < 0 ? range.subChunkCount : std::min(packet->mSubChunkLimit, range.subChunkCount);
        const auto& data = packet->mData;
        const auto* raw = reinterpret_cast<const uint8_t*>(data.data());
        size_t offset = 0;
        if (!result.request) {
            if (packet->mSubChunksLength > static_cast<uint32_t>(range.subChunkCount)) {
                result.error = "LevelChunk carries " + std::to_string(packet->mSubChunksLength) + " sub-chunks";
            } else {
                result.chunks.resize(packet->mSubChunksLength);
                result.y.resize(packet->mSubChunksLength);
                for (uint32_t i = 0; i < packet->mSubChunksLength; ++i) {
                    size_t consumed = 0;
                    std::string error;
                    if (!SubChunk::decode(raw + offset, data.size() - offset, result.chunks[i], consumed, error, palette)) {
                        result.error = "LevelChunk " + std::to_string(key.x) + "," + std::to_string(key.z) + ": " + error;
                        break;
                    }
                    int32_t y = result.chunks[i].yIndex().value_or(range.baseSubChunkY + static_cast<int32_t>(i));
                    if (y < range.baseSubChunkY || y >= range.baseSubChunkY + range.subChunkCount
                        || std::find(result.y.begin(), result.y.begin() + i, y) != result.y.begin() + i) {
                        result.error = "LevelChunk invalid or duplicate sub-chunk Y index: " + std::to_string(y);
                        break;
                    }
                    result.y[i] = y;
                    offset += consumed;
                }
            }
        }
        if (result.error.empty()) {
            ByteReader reader(raw + offset, data.size() - offset);
            std::string error;
            decodeBiomes(reader, range.subChunkCount, result.biomes, error);
            uint8_t borderBlocks = 0;
            if (!result.request && reader.readByte(borderBlocks, error, "border blocks") && reader.remaining() >= borderBlocks) {
                size_t start = reader.position() + borderBlocks;
                result.entities = readBlockEntities(raw + offset + start, data.size() - offset - start);
            }
        }
        return [this, key, range, result = std::move(result)]() mutable {
            if (!result.error.empty()) {
                recordError(result.error);
                return;
            }
            if (publisherRadius > 0 && chunkRadius > 0 && !chunkInView(chunkRadius, key.x, key.z, centerX, centerZ)) return;
            if (!result.request) {
                pending.erase(key);
                chunks.evict(key);
            }
            chunks.markLoaded(key);
            lastColumnAt = Clock::now();
            chunks.setBiomes(key, range.baseSubChunkY, std::move(result.biomes));
            if (result.request) {
                if (result.requestCount > 0) requestColumn(key, range.baseSubChunkY, result.requestCount);
                return;
            }
            for (size_t i = 0; i < result.chunks.size(); ++i)
                chunks.commit({ key.dimension, key.x, result.y[i], key.z }, std::move(result.chunks[i]));
            for (Tag& entity : result.entities) {
                const int32_t x = tagInt(entity, "x"), y = tagInt(entity, "y"), z = tagInt(entity, "z");
                chunks.setBlockEntity(key.dimension, x, y, z, std::move(entity));
            }
        };
    });
}

void WorldStream::handle(std::shared_ptr<const SubChunkPacket> packet)
{
    if (packet->mDimension != dimension) return;
    size_t bytes = 0;
    for (const auto& entry : packet->mSubChunks) {
        bytes += entry.mData.size() + entry.mHeightMapData.size() + entry.mRenderHeightMapData.size() + 4096;
        ++decodingReplies[{ packet->mDimension, packet->mCenterPosition.x + entry.mPosition.x,
            packet->mCenterPosition.y + entry.mPosition.y, packet->mCenterPosition.z + entry.mPosition.z }];
    }
    counters.subChunkReplies += packet->mSubChunks.size();
    decoding.submit(bytes, [this, packet = std::move(packet), resolver = blockPaletteResolver] {
        const BlockPaletteResolver emptyResolver;
        const BlockPaletteResolver& palette = resolver ? *resolver : emptyResolver;
        struct Reply {
            SubChunkKey key;
            SubChunkRequestResult status;
            SubChunk chunk;
            BlockEntityMap entities;
            std::string error;
            bool replaceEntities = false;
        };
        std::vector<Reply> replies;
        replies.reserve(packet->mSubChunks.size());
        for (const SubChunkData& entry : packet->mSubChunks) {
            Reply reply;
            reply.key = { packet->mDimension, packet->mCenterPosition.x + entry.mPosition.x,
                packet->mCenterPosition.y + entry.mPosition.y, packet->mCenterPosition.z + entry.mPosition.z };
            reply.status = entry.mResult;
            reply.replaceEntities = entry.mResult == SubChunkRequestResult::SuccessAllAir;
            if (entry.mResult == SubChunkRequestResult::Success && entry.mHasData) {
                size_t consumed = 0;
                const auto* raw = reinterpret_cast<const uint8_t*>(entry.mData.data());
                if (!SubChunk::decode(raw, entry.mData.size(), reply.chunk, consumed, reply.error, palette)) {
                    reply.error = "SubChunk " + std::to_string(reply.key.x) + "," + std::to_string(reply.key.y) + "," + std::to_string(reply.key.z) + ": " + reply.error;
                } else {
                    reply.replaceEntities = true;
                    for (Tag& entity : readBlockEntities(raw + consumed, entry.mData.size() - consumed)) {
                        int32_t x = tagInt(entity, "x"), y = tagInt(entity, "y"), z = tagInt(entity, "z");
                        reply.entities[static_cast<uint16_t>(linearIndex(uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15)))] = std::move(entity);
                    }
                }
            }
            replies.push_back(std::move(reply));
        }
        return [this, replies = std::move(replies)]() mutable {
            for (Reply& reply : replies) {
                auto decodingReply = decodingReplies.find(reply.key);
                if (decodingReply != decodingReplies.end() && --decodingReply->second == 0)
                    decodingReplies.erase(decodingReply);
                auto column = pending.find(reply.key.chunk());
                if (column == pending.end() || !column->second.contains(reply.key.y)) continue;
                column->second.erase(reply.key.y);
                if (column->second.empty()) pending.erase(column);
                if (!reply.error.empty()) { recordError(reply.error); continue; }
                if (reply.status != SubChunkRequestResult::Success && reply.status != SubChunkRequestResult::SuccessAllAir) continue;
                chunks.commit(reply.key, std::move(reply.chunk));
                if (reply.replaceEntities) chunks.replaceBlockEntities(reply.key, std::move(reply.entities));
            }
        };
    });
}

void WorldStream::handle(std::shared_ptr<const BlockActorDataPacket> packet, size_t bytes)
{
    if (decoding.empty()) apply(*packet);
    else decoding.append(bytes, [this, packet = std::move(packet)] { apply(*packet); });
}

void WorldStream::apply(const BlockActorDataPacket& packet)
{
    chunks.setBlockEntity(dimension, packet.mBlockPosition.x, packet.mBlockPosition.y, packet.mBlockPosition.z, packet.mData);
}

void WorldStream::handle(const UpdateBlockPacket& packet)
{
    if (decoding.empty()) apply(packet);
    else decoding.append(sizeof(packet), [this, packet] { apply(packet); });
}

void WorldStream::handle(const UpdateBlockSyncedPacket& packet)
{
    UpdateBlockPacket update;
    update.mBlockPosition = packet.mBlockPosition;
    update.mRuntimeId = packet.mRuntimeId;
    update.mFlags = packet.mFlags;
    update.mDataLayer = packet.mDataLayer;
    handle(update);
}

void WorldStream::apply(const UpdateBlockPacket& packet)
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

void WorldStream::handle(std::shared_ptr<const UpdateSubChunkBlocksPacket> packet)
{
    if (decoding.empty()) apply(*packet);
    else {
        const size_t bytes = (packet->mStandardBlocks.size() + packet->mExtraBlocks.size()) * sizeof(BlockChangeEntry);
        decoding.append(bytes, [this, packet = std::move(packet)] { apply(*packet); });
    }
}

void WorldStream::apply(const UpdateSubChunkBlocksPacket& packet)
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

/**
 * Every column the server has announced around its publisher center has
 * arrived. The edge ring is left out because servers round the circle
 * differently. Some servers announce a publisher radius wider than the chunk
 * radius they agreed to and never send the difference, so the smaller wins.
 * Others stop even shorter than both; once the columns around the center are
 * in and none has come for a while, what arrived is taken as the whole set.
 */
bool WorldStream::cohortLoaded() const
{
    int32_t radius = publisherRadius > 0 && chunkRadius > 0 ? std::min(publisherRadius, chunkRadius) : std::max(publisherRadius, chunkRadius);
    if (radius <= 0) {
        return false;
    }
    int32_t inner = std::max(radius - 1, 0);
    bool complete = true;
    for (int32_t dx = -inner; dx <= inner && complete; ++dx) {
        for (int32_t dz = -inner; dz <= inner; ++dz) {
            if (dx * dx + dz * dz <= inner * inner && !chunks.isLoaded({ dimension, centerX + dx, centerZ + dz })) {
                complete = false;
                break;
            }
        }
    }
    if (complete) {
        return true;
    }
    return settled();
}

bool WorldStream::columnPending(const ChunkKey& key) const
{
    auto found = pending.find(key);
    return found != pending.end() && !found->second.empty();
}

bool WorldStream::settled() const
{
    return decoding.empty() && pending.empty() && lastColumnAt != Clock::time_point {} && Clock::now() - lastColumnAt >= CohortSettleTime;
}

/**
 * The column under the publisher center and its neighbours have arrived. The
 * game settles a dimension change on this alone, and proxies like Hive only
 * send those nine columns until the client says it is done.
 */
bool WorldStream::centerLoaded() const
{
    for (int32_t dx = -1; dx <= 1; ++dx) {
        for (int32_t dz = -1; dz <= 1; ++dz) {
            if (!chunks.isLoaded({ dimension, centerX + dx, centerZ + dz })) {
                return false;
            }
        }
    }
    return true;
}

void WorldStream::handle(const NetworkChunkPublisherUpdatePacket& packet)
{
    if (decoding.empty()) apply(packet);
    else decoding.append(sizeof(packet), [this, position = packet.mPosition, radius = packet.mRadius] {
        NetworkChunkPublisherUpdatePacket deferred;
        deferred.mPosition = position;
        deferred.mRadius = radius;
        apply(deferred);
    });
}

void WorldStream::apply(const NetworkChunkPublisherUpdatePacket& packet)
{
    centerX = floorDiv16(packet.mPosition.x);
    centerZ = floorDiv16(packet.mPosition.z);
    publisherRadius = static_cast<int32_t>(packet.mRadius / 16);
    if (chunkRadius <= 0) {
        chunkRadius = static_cast<int32_t>(packet.mRadius / 16);
    }
    retain();
}

std::vector<std::unique_ptr<SubChunkRequestPacket>> WorldStream::takeRequests(Clock::time_point now, const MeshViewPriority& priority)
{
    std::vector<std::unique_ptr<SubChunkRequestPacket>> requests;
    std::vector<ChunkKey> abandoned;
    bool paused = decoding.backlogged();
    if (requestsPaused && now > lastRequestPoll) {
        // Receiving is paused under decode backpressure; queued replies have not had a chance to arrive.
        auto delay = now - lastRequestPoll;
        for (auto& [key, column] : pending) {
            for (auto& [y, entry] : column) {
                if (entry.sent) entry.deadline += delay;
            }
        }
    }
    requestsPaused = paused;
    lastRequestPoll = now;
    if (paused) return requests;

    struct Candidate {
        ChunkKey key;
        MeshPriority priority;
        bool inFlight;
    };
    thread_local std::vector<Candidate> candidates;
    candidates.clear();
    candidates.reserve(pending.size());
    size_t inFlight = 0;
    for (const auto& [key, column] : pending) {
        bool sent = std::any_of(column.begin(), column.end(), [](const auto& entry) { return entry.second.sent; });
        inFlight += sent;
        candidates.push_back({ key, priority.rankColumn(key.x, key.z), sent });
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return left.priority < right.priority;
    });
    for (const auto& candidate : candidates) {
        if (!candidate.inFlight && inFlight >= MaxInFlightColumns) continue;
        const ChunkKey& key = candidate.key;
        auto& column = pending.at(key);
        std::vector<int32_t> due;
        for (auto it = column.begin(); it != column.end();) {
            PendingSubChunk& entry = it->second;
            // A reply waiting for decode must not time out and be requested again.
            if (entry.sent && (now < entry.deadline || decodingReplies.contains({ key.dimension, key.x, it->first, key.z }))) {
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
            inFlight -= candidate.inFlight;
        }
        if (due.empty()) {
            continue;
        }
        if (!candidate.inFlight) ++inFlight;

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
    std::sort(stale.begin(), stale.end());
    stale.erase(std::unique(stale.begin(), stale.end()), stale.end());
    for (const ChunkKey& key : stale) {
        pending.erase(key);
        if (chunks.isLoaded(key)) ++counters.evictedColumns;
    }
    chunks.evict(stale);
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
    if (counters.decodeErrors <= 16 || counters.decodeErrors % 100 == 0)
        debugLog("world decode error #" + std::to_string(counters.decodeErrors) + ": " + error);
    counters.lastError = error;
}

}
