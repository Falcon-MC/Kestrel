#include "world/MeshScheduler.h"

#include <algorithm>
#include <chrono>

namespace kestrel::world {

MeshScheduler::MeshScheduler()
{
    unsigned int count = std::max(1u, std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 2 : 1u);
    for (unsigned int i = 0; i < count; ++i) {
        workers.emplace_back([this] {
            work();
        });
    }
}

MeshScheduler::~MeshScheduler()
{
    {
        std::lock_guard<std::mutex> guard(mutex);
        stopping = true;
        queued.clear();
        order.clear();
    }
    wake.notify_all();
    for (std::thread& worker : workers) {
        worker.join();
    }
}

/**
 * Queues a mesh job. A sub-chunk already waiting keeps its place in the queue
 * and only its newest input is meshed, so a burst of neighbour updates costs
 * one job instead of one per update.
 */
void MeshScheduler::submit(const SubChunkKey& key, uint64_t generation, MeshInput input, std::shared_ptr<const BlockAssets> assets, IdMapping ids, bool urgent)
{
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto [entry, inserted] = queued.insert_or_assign(key, Job { key, generation, std::move(input), std::move(assets), std::move(ids) });
        if (urgent) {
            if (!inserted) {
                order.erase(std::remove(order.begin(), order.end(), key), order.end());
            }
            order.push_front(key);
        } else if (inserted) {
            order.push_back(key);
        }
    }
    wake.notify_one();
}

std::vector<MeshResult> MeshScheduler::takeResults()
{
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<MeshResult> ready = std::move(results);
    results.clear();
    return ready;
}

double MeshScheduler::averageMilliseconds() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return meshCount ? meshMilliseconds / double(meshCount) : 0.0;
}

size_t MeshScheduler::pending() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return queued.size() + running;
}

void MeshScheduler::clear()
{
    std::lock_guard<std::mutex> guard(mutex);
    queued.clear();
    order.clear();
    results.clear();
}

void MeshScheduler::work()
{
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] {
                return stopping || !order.empty();
            });
            if (stopping) {
                return;
            }
            auto entry = queued.find(order.front());
            order.pop_front();
            job = std::move(entry->second);
            queued.erase(entry);
            ++running;
        }

        auto started = std::chrono::steady_clock::now();
        MeshResult result { job.key, job.generation, meshSubChunk(*job.assets, job.ids, job.input) };
        double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

        std::lock_guard<std::mutex> guard(mutex);
        --running;
        if (!result.mesh.empty()) {
            meshMilliseconds += elapsed;
            ++meshCount;
        }
        results.push_back(std::move(result));
    }
}

}
