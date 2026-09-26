#include "world/MeshScheduler.h"

#include <algorithm>

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
        jobs.clear();
    }
    wake.notify_all();
    for (std::thread& worker : workers) {
        worker.join();
    }
}

void MeshScheduler::submit(const SubChunkKey& key, uint64_t generation, MeshInput input, std::shared_ptr<const BlockAssets> assets, IdMapping ids)
{
    {
        std::lock_guard<std::mutex> guard(mutex);
        jobs.push_back({ key, generation, std::move(input), std::move(assets), std::move(ids) });
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

size_t MeshScheduler::pending() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return jobs.size() + running;
}

void MeshScheduler::clear()
{
    std::lock_guard<std::mutex> guard(mutex);
    jobs.clear();
    results.clear();
}

void MeshScheduler::work()
{
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] {
                return stopping || !jobs.empty();
            });
            if (stopping) {
                return;
            }
            job = std::move(jobs.front());
            jobs.pop_front();
            ++running;
        }

        MeshResult result { job.key, job.generation, meshSubChunk(*job.assets, job.ids, job.input) };

        std::lock_guard<std::mutex> guard(mutex);
        --running;
        results.push_back(std::move(result));
    }
}

}
