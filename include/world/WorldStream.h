#pragma once

#include "world/ChunkStore.h"

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
    void setChunkRadius(int32_t radius);
    void changeDimension(int32_t dimension, int32_t chunkX, int32_t chunkZ);

    void handle(const LevelChunkPacket& packet);
    void handle(const SubChunkPacket& packet);
    void handle(const UpdateBlockPacket& packet);
    void handle(const UpdateSubChunkBlocksPacket& packet);
    void handle(const NetworkChunkPublisherUpdatePacket& packet);
    void handle(const BlockActorDataPacket& packet);

    std::vector<std::unique_ptr<SubChunkRequestPacket>> takeRequests(Clock::time_point now);

    ChunkStore& store()
    {
        return chunks;
    }

    WorldStats stats() const;
    bool cohortLoaded() const;

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

    ChunkStore chunks;
    std::map<ChunkKey, std::map<int32_t, PendingSubChunk>> pending;
    int32_t dimension = 0;
    int32_t centerX = 0;
    int32_t centerZ = 0;
    int32_t chunkRadius = 0;
    int32_t publisherRadius = 0;
    WorldStats counters;
};

}
