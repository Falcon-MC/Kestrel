#pragma once

#include "world/ChunkStore.h"
#include "world/Mesher.h"

#include <condition_variable>
#include <deque>
#include <cstdint>
#include <array>
#include <set>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace kestrel::world {

struct MeshBudgetState;
struct MeshMemoryCredit;

struct MeshResult {
    SubChunkKey key;
    uint64_t generation = 0;
    uint64_t epoch = 0;
    ChunkMesh mesh;
    std::shared_ptr<MeshMemoryCredit> credit;
};

class MeshScheduler {
public:
    MeshScheduler();
    ~MeshScheduler();

    MeshScheduler(const MeshScheduler&) = delete;
    MeshScheduler& operator=(const MeshScheduler&) = delete;

    bool submit(const SubChunkKey& key, uint64_t generation, MeshInput input, std::shared_ptr<const BlockAssets> assets, IdMapping ids, bool urgent = false);
    std::vector<MeshResult> takeResults(size_t maximum = 32);
    size_t pending() const;
    double averageMilliseconds() const;
    size_t workerCount() const
    {
        return workers.size();
    }
    void clear();
    void cancel(const SubChunkKey& key);
    void invalidate(const SubChunkKey& key, uint64_t generation);
    void setView(std::array<double, 3> position);
    bool isCurrent(const MeshResult& result) const;
    bool canSubmit(const SubChunkKey& key) const;
    static constexpr size_t QueueCapacity = 64;
    static constexpr size_t ResultCapacity = 32;
    static constexpr size_t ResultByteCapacity = 64 * 1024 * 1024;
    static constexpr size_t LightCacheCapacity = 32;

private:
    struct Job {
        SubChunkKey key;
        uint64_t generation = 0;
        uint64_t epoch = 0;
        bool urgent = false;
        MeshInput input;
        std::shared_ptr<const BlockAssets> assets;
        IdMapping ids;
        size_t maxTemplateQuads = 0;
    };

    struct LightCacheEntry {
        MeshInput sources;
        std::shared_ptr<const BlockAssets> assets;
        IdMapping ids;
        std::shared_ptr<const ChunkLighting> lighting;
        uint64_t used = 0;
    };

    void work();
    bool hasReadyJob() const;

    std::vector<std::thread> workers;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::map<SubChunkKey, Job> queued;
    std::set<SubChunkKey> active;
    std::map<SubChunkKey, uint64_t> newest;
    std::map<SubChunkKey, std::shared_ptr<std::atomic_bool>> cancellations;
    std::shared_ptr<MeshBudgetState> memory;
    std::shared_ptr<const BlockAssets> boundAssets;
    size_t maxTemplateQuads = 0;
    std::map<SubChunkKey, LightCacheEntry> lightCache;
    std::array<double, 3> view {};
    uint64_t currentEpoch = 1;
    uint64_t cacheClock = 0;
    size_t resultBytes = 0;
    std::deque<MeshResult> results;
    size_t running = 0;
    double meshMilliseconds = 0.0;
    uint64_t meshCount = 0;
    bool stopping = false;
};

}
