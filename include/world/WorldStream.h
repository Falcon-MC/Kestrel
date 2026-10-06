#pragma once

#include "world/ChunkStore.h"
#include "world/ChunkDecodeQueue.h"
#include "world/MeshPriority.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class LevelChunkPacket;
class SubChunkPacket;
class UpdateBlockPacket;
class UpdateSubChunkBlocksPacket;
class NetworkChunkPublisherUpdatePacket;
class BlockActorDataPacket;
class SubChunkRequestPacket;

namespace kestrel::world {

struct WorldStats {
    size_t columns = 0;
    size_t subChunks = 0;
    size_t pendingSubChunks = 0;
    uint64_t levelChunks = 0;
    uint64_t subChunkReplies = 0;
    uint64_t blockUpdates = 0;
    uint64_t decodeErrors = 0;
    uint64_t requestsSent = 0;
    uint64_t retries = 0;
    uint64_t evictedColumns = 0;
    std::string lastError;
};

class WorldStream {
public:
    using Clock = std::chrono::steady_clock;

    void reset(int32_t dimension, int32_t chunkX, int32_t chunkZ);
    void setBlockPaletteResolver(BlockPaletteResolver resolver) { blockPaletteResolver = std::make_shared<BlockPaletteResolver>(std::move(resolver)); }
    void setChunkRadius(int32_t radius);
    void changeDimension(int32_t dimension, int32_t chunkX, int32_t chunkZ);

    void handle(std::shared_ptr<const LevelChunkPacket> packet);
    void handle(std::shared_ptr<const SubChunkPacket> packet);
    void handle(const UpdateBlockPacket& packet);
    void handle(std::shared_ptr<const UpdateSubChunkBlocksPacket> packet);
    void handle(const NetworkChunkPublisherUpdatePacket& packet);
    void handle(std::shared_ptr<const BlockActorDataPacket> packet, size_t bytes);

    bool applyDecoded() { decoding.drain(); return decoding.empty(); }
    void cancelDecoding() { decoding.clear(); decodingReplies.clear(); }
    bool decodeBacklogged() const { return decoding.backlogged(); }

    std::vector<std::unique_ptr<SubChunkRequestPacket>> takeRequests(Clock::time_point now, const MeshViewPriority& priority = {});

    ChunkStore& store()
    {
        return chunks;
    }

    WorldStats stats() const;
    bool cohortLoaded() const;
    bool centerLoaded() const;

    /**
     * The server has sent nothing new for a while and nothing is on its way:
     * the columns still missing are never coming, so waiting on them stops.
     */
    bool settled() const;

    /**
     * Whether a sub-chunk was asked for and has not arrived yet, so its
     * blocks are unknown rather than air.
     */
    bool subChunkPending(const SubChunkKey& key) const;

    /**
     * Whether some sub-chunk of the column was asked for and has not arrived.
     */
    bool columnPending(const ChunkKey& key) const;

private:
    struct PendingSubChunk {
        Clock::time_point deadline {};
        uint8_t attempts = 0;
        bool sent = false;
    };

    void requestColumn(const ChunkKey& key, int32_t baseY, int32_t count);
    void retain();
    void evictColumn(const ChunkKey& key);
    void recordError(const std::string& error);

    void apply(const UpdateBlockPacket& packet);
    void apply(const UpdateSubChunkBlocksPacket& packet);
    void apply(const NetworkChunkPublisherUpdatePacket& packet);
    void apply(const BlockActorDataPacket& packet);
    void applyChunkRadius(int32_t radius);

    std::shared_ptr<BlockPaletteResolver> blockPaletteResolver;
    ChunkStore chunks;
    std::map<ChunkKey, std::map<int32_t, PendingSubChunk>> pending;
    std::map<SubChunkKey, size_t> decodingReplies;
    int32_t dimension = 0;
    int32_t centerX = 0;
    int32_t centerZ = 0;
    int32_t chunkRadius = 0;
    int32_t publisherRadius = 0;
    Clock::time_point lastColumnAt {};
    Clock::time_point lastRequestPoll {};
    bool requestsPaused = false;
    WorldStats counters;
    ChunkDecodeQueue decoding;
};

}
