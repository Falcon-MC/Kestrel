#pragma once

#include "world/ChunkStore.h"
#include "world/Mesher.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace kestrel::world {

struct MeshResult {
    SubChunkKey key;
    uint64_t generation = 0;
    ChunkMesh mesh;
};

class MeshScheduler {
public:
    MeshScheduler();
    ~MeshScheduler();

    MeshScheduler(const MeshScheduler&) = delete;
    MeshScheduler& operator=(const MeshScheduler&) = delete;

    void submit(const SubChunkKey& key, uint64_t generation, MeshInput input, std::shared_ptr<const BlockAssets> assets, IdMapping ids, bool urgent = false);
    std::vector<MeshResult> takeResults();
    size_t pending() const;
    double averageMilliseconds() const;
    size_t workerCount() const
    {
        return workers.size();
    }
    void clear();

private:
    struct Job {
        SubChunkKey key;
        uint64_t generation = 0;
        MeshInput input;
        std::shared_ptr<const BlockAssets> assets;
        IdMapping ids;
    };

    void work();

    std::vector<std::thread> workers;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::map<SubChunkKey, Job> queued;
    std::deque<SubChunkKey> order;
    std::vector<MeshResult> results;
    size_t running = 0;
    double meshMilliseconds = 0.0;
    uint64_t meshCount = 0;
    bool stopping = false;
};

}
