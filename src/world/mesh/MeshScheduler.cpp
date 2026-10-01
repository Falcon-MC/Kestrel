#include "world/MeshScheduler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace kestrel::world {

struct MeshBudgetState {
    std::mutex mutex;
    std::condition_variable wake;
    size_t bytes = 0;
    bool stopping = false;
};

struct MeshMemoryCredit {
    std::shared_ptr<MeshBudgetState> state;
    size_t bytes;
    MeshMemoryCredit(std::shared_ptr<MeshBudgetState> state, size_t bytes) : state(std::move(state)), bytes(bytes) {}
    ~MeshMemoryCredit() {
        { std::lock_guard lock(state->mutex); state->bytes -= bytes; }
        state->wake.notify_all();
    }
    void reconcile(size_t actual) {
        { std::lock_guard lock(state->mutex); state->bytes = state->bytes - bytes + actual; bytes = actual; }
        state->wake.notify_all();
    }
};
namespace {
size_t meshBytes(const ChunkMesh& mesh)
{
    return (mesh.cubes.capacity() + mesh.translucentCubes.capacity()) * sizeof(PackedQuad)
        + (mesh.models.capacity() + mesh.translucentModels.capacity()) * sizeof(ModelQuadGpu)
        + mesh.light.capacity();
}
}

MeshScheduler::MeshScheduler()
    : memory(std::make_shared<MeshBudgetState>())
{
    unsigned int cores = std::thread::hardware_concurrency();
    unsigned int count = std::min(8u, std::max(1u, cores > 2 ? cores - 2 : 1u));
    for (unsigned int i = 0; i < count; ++i) workers.emplace_back([this] { work(); });
}

MeshScheduler::~MeshScheduler()
{
    { std::lock_guard lock(memory->mutex); memory->stopping = true; }
    memory->wake.notify_all();
    {
        std::lock_guard<std::mutex> guard(mutex);
        stopping = true;
        for (auto& [key, token] : cancellations) token->store(true);
        queued.clear();
    }
    wake.notify_all();
    for (std::thread& worker : workers) worker.join();
}

bool MeshScheduler::submit(const SubChunkKey& key, uint64_t generation, MeshInput input, std::shared_ptr<const BlockAssets> assets, IdMapping ids, bool urgent)
{
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto existing = queued.find(key);
        if (existing == queued.end() && queued.size() >= QueueCapacity) return false;
        if (existing != queued.end()) urgent |= existing->second.urgent;
        newest[key] = generation;
        if (auto old = cancellations.find(key); old != cancellations.end()) old->second->store(true);
        auto token = std::make_shared<std::atomic_bool>(false);
        cancellations[key] = token;
        input.cancelled = std::move(token);
        if (boundAssets != assets) {
            boundAssets = assets;
            maxTemplateQuads = 0;
            for (const auto& model : assets->modelTemplates()) maxTemplateQuads = std::max(maxTemplateQuads, size_t(model.quadCount));
        }
        queued.insert_or_assign(key, Job { key, generation, currentEpoch, urgent, std::move(input), std::move(assets), std::move(ids), maxTemplateQuads });
    }
    wake.notify_all();
    memory->wake.notify_all();
    return true;
}

void MeshScheduler::invalidate(const SubChunkKey& key, uint64_t generation)
{
    {
        std::lock_guard lock(mutex);
        if (auto old = cancellations.find(key); old != cancellations.end()) old->second->store(true);
        queued.erase(key);
        newest[key] = generation;
    }
    memory->wake.notify_all();
    wake.notify_all();
}

void MeshScheduler::cancel(const SubChunkKey& key)
{
    {
        std::lock_guard<std::mutex> guard(mutex);
        queued.erase(key);
        if (auto old = cancellations.find(key); old != cancellations.end()) {
            old->second->store(true);
            cancellations.erase(old);
        }
        newest.erase(key);
        lightCache.erase(key);
        for (auto it = results.begin(); it != results.end();) {
            if (it->key == key) { resultBytes -= meshBytes(it->mesh); it = results.erase(it); }
            else ++it;
        }
    }
    wake.notify_all();
    memory->wake.notify_all();
}

void MeshScheduler::setView(std::array<double, 3> position)
{
    std::lock_guard<std::mutex> guard(mutex);
    view = position;
}

bool MeshScheduler::isCurrent(const MeshResult& result) const
{
    std::lock_guard<std::mutex> guard(mutex);
    auto entry = newest.find(result.key);
    return result.epoch == currentEpoch && entry != newest.end() && entry->second == result.generation;
}

bool MeshScheduler::canSubmit(const SubChunkKey& key) const
{
    std::lock_guard<std::mutex> guard(mutex);
    return queued.contains(key) || queued.size() < QueueCapacity;
}

std::vector<MeshResult> MeshScheduler::takeResults(size_t maximum)
{
    std::vector<MeshResult> ready;
    {
        std::lock_guard<std::mutex> guard(mutex);
        ready.reserve(std::min(maximum, results.size()));
        while (!results.empty() && ready.size() < maximum) {
            resultBytes -= meshBytes(results.front().mesh);
            ready.push_back(std::move(results.front()));
            results.pop_front();
        }
    }
    wake.notify_all();
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
    {
        std::lock_guard<std::mutex> guard(mutex);
        ++currentEpoch;
        for (auto& [key, token] : cancellations) token->store(true);
        cancellations.clear();
        queued.clear();
        newest.clear();
        results.clear();
        lightCache.clear();
        resultBytes = 0;
        meshMilliseconds = 0;
        meshCount = 0;
    }
    wake.notify_all();
    memory->wake.notify_all();
}

bool MeshScheduler::hasReadyJob() const
{
    return std::any_of(queued.begin(), queued.end(), [&](const auto& entry) { return !active.contains(entry.first); });
}

void MeshScheduler::work()
{
    for (;;) {
        Job job;
        std::shared_ptr<const ChunkLighting> previous;
        bool reusable = false;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return stopping || (results.size() < ResultCapacity && resultBytes < ResultByteCapacity && hasReadyJob()); });
            if (stopping) return;
            auto best = queued.end();
            double bestDistance = std::numeric_limits<double>::infinity();
            for (auto entry = queued.begin(); entry != queued.end(); ++entry) {
                if (active.contains(entry->first)) continue;
                double dx = double(entry->first.x) * 16 + 8 - view[0];
                double dy = double(entry->first.y) * 16 + 8 - view[1];
                double dz = double(entry->first.z) * 16 + 8 - view[2];
                double distance = dx * dx + dy * dy + dz * dz;
                if (best == queued.end() || (entry->second.urgent && !best->second.urgent)
                    || (entry->second.urgent == best->second.urgent && distance < bestDistance)) {
                    best = entry; bestDistance = distance;
                }
            }
            job = std::move(best->second);
            queued.erase(best);
            active.insert(job.key);
            ++running;
            auto cached = lightCache.find(job.key);
            if (cached != lightCache.end()) {
                previous = cached->second.lighting;
                const auto& sources = cached->second.sources;
                reusable = cached->second.assets == job.assets
                    && cached->second.ids.hashed == job.ids.hashed
                    && cached->second.ids.sequential == job.ids.sequential
                    && cached->second.ids.hidden == job.ids.hidden
                    && sources.skyLight == job.input.skyLight
                    && sources.center == job.input.center
                    && sources.around == job.input.around && sources.above == job.input.above;
            }
        }
        auto started = std::chrono::steady_clock::now();
        std::shared_ptr<MeshMemoryCredit> credit;
        auto cancelled = [&] { return job.input.cancelled->load(std::memory_order_relaxed); };
        if (!cancelled()) {
            size_t bound = meshOutputBound(*job.assets, job.ids, job.input, job.maxTemplateQuads);
            std::unique_lock lock(memory->mutex);
            memory->wake.wait(lock, [&] { return memory->stopping || cancelled() || memory->bytes == 0
                || (bound <= ResultByteCapacity && memory->bytes <= ResultByteCapacity - bound); });
            if (!memory->stopping && !cancelled()) {
                credit = std::make_shared<MeshMemoryCredit>(memory, bound);
                memory->bytes += bound;
            }
        }
        ChunkMesh output;
        if (credit && !cancelled()) {
            job.input.lighting = reusable ? previous : updateChunkLighting(*job.assets, job.ids, job.input, previous);
            if (!cancelled()) output = meshSubChunk(*job.assets, job.ids, job.input);
            credit->reconcile(meshBytes(output));
        }
        MeshResult result { job.key, job.generation, job.epoch, std::move(output), std::move(credit) };
        double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        size_t bytes = meshBytes(result.mesh);
        {
            std::unique_lock<std::mutex> lock(mutex);
            auto current = [&] {
                auto latest = newest.find(job.key);
                return !cancelled() && result.credit && job.epoch == currentEpoch && latest != newest.end() && latest->second == job.generation;
            };
            wake.wait(lock, [&] { return stopping || !current() || (results.size() < ResultCapacity
                && (results.empty() || bytes <= ResultByteCapacity - std::min(resultBytes, ResultByteCapacity))); });
            --running;
            active.erase(job.key);
            if (stopping) return;
            if (current()) {
                if (lightCache.size() >= LightCacheCapacity && !lightCache.contains(job.key)) {
                    auto oldest = std::min_element(lightCache.begin(), lightCache.end(), [](const auto& left, const auto& right) { return left.second.used < right.second.used; });
                    lightCache.erase(oldest);
                }
                MeshInput sources;
                sources.center = job.input.center;
                sources.around = job.input.around;
                sources.above = job.input.above;
                sources.skyLight = job.input.skyLight;
                lightCache.insert_or_assign(job.key, LightCacheEntry { std::move(sources), job.assets, job.ids, job.input.lighting, ++cacheClock });
                meshMilliseconds += elapsed;
                ++meshCount;
                resultBytes += bytes;
                results.push_back(std::move(result));
            }
        }
        wake.notify_all();
    }
}

}
