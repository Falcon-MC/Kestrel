#pragma once

#include "world/ChunkStore.h"
#include "world/Mesher.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
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

    void submit(const SubChunkKey& key, uint64_t generation, MeshInput input, std::shared_ptr<const BlockAssets> assets, IdMapping ids);
    std::vector<MeshResult> takeResults();
    size_t pending() const;
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
    std::deque<Job> jobs;
    std::vector<MeshResult> results;
    size_t running = 0;
    bool stopping = false;
};

}
