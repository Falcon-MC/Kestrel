#include "world/MeshScheduler.h"
#include "client/DebugLog.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace kestrel {
void debugLog(const std::string&) {}
StartupTimer::StartupTimer() = default;
void StartupTimer::mark(const std::string&) {}
}

using namespace kestrel::world;
using namespace std::chrono_literals;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
}

template<class Condition, class Poll>
void waitUntil(Condition condition, Poll poll)
{
    auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!condition()) {
        check(std::chrono::steady_clock::now() < deadline, "mesh workers timed out");
        poll();
        std::this_thread::sleep_for(1ms);
    }
}

MeshInput input(bool diagnostic = false)
{
    MeshInput result;
    auto center = std::make_shared<SubChunk>();
    if (diagnostic) {
        const uint8_t bytes[] = { 8, 1, 1, 0 };
        size_t consumed = 0;
        std::string error;
        check(SubChunk::decode(bytes, sizeof(bytes), *center, consumed, error), "synthetic palette could not be decoded");
    }
    result.center = std::move(center);
    return result;
}

int main()
{
    auto assets = std::make_shared<const BlockAssets>();
    for (unsigned count : { 1u, 2u, 4u }) {
        MeshScheduler scheduler(count);
        check(scheduler.workerCount() == count, "configured worker budget was ignored");
        std::vector<MeshResult> held;
        for (int index = 0; index < 24; ++index) {
            check(scheduler.submit({0, index, 0, 0}, 1, input(index % 3 == 0), assets, {}, index == 23), "initial job rejected");
        }
        size_t completed = 0;
        waitUntil([&] { return completed == 24; }, [&] {
            auto results = scheduler.takeResults();
            for (auto& result : results) {
                check(scheduler.isCurrent(result), "fresh result considered stale");
                check(result.mesh.visibility != nullptr, "worker lost its visibility result");
                if (result.key.x % 3 == 0) {
                    check(!result.mesh.empty(), "lit diagnostic mesh disappeared");
                    check(result.mesh.light.size() == 4096, "prepared lighting was lost before meshing");
                }
                ++completed;
                held.push_back(std::move(result));
            }
        });
        waitUntil([&] { return scheduler.pending() == 0; }, [] {});
        scheduler.clear();
        for (const auto& result : held) check(!scheduler.isCurrent(result), "old epoch survived clear");
        held.clear();

        // Reset while results are full and both phases still own pending jobs.
        for (int index = 0; index < 64; ++index) {
            check(scheduler.submit({0, index, 1, 0}, 2, input(), assets, {}), "bounded batch rejected");
        }
        std::this_thread::sleep_for(50ms);
        scheduler.clear();
        waitUntil([&] { return scheduler.pending() == 0; }, [&] { scheduler.takeResults(); });
        check(scheduler.takeResults().empty(), "reset published cancelled results");

        SubChunkKey key {0, 0, 0, 0};
        check(scheduler.submit(key, 3, input(true), assets, {}), "post-reset job rejected");
        scheduler.invalidate(key, 4);
        waitUntil([&] { return scheduler.pending() == 0; }, [&] { scheduler.takeResults(); });
        check(scheduler.submit(key, 4, input(true), assets, {}, true), "replacement job rejected");
        bool fresh = false;
        waitUntil([&] { return fresh; }, [&] {
            for (const auto& result : scheduler.takeResults()) {
                check(result.generation == 4 && scheduler.isCurrent(result), "invalidated generation was published");
                fresh = true;
            }
        });

        for (int index = 0; index < 24; ++index) {
            check(scheduler.submit({0, index, 2, 0}, 5, input(true), assets, {}), "cancellation batch rejected");
        }
        for (int index = 0; index < 24; ++index) scheduler.cancel({0, index, 2, 0});
        waitUntil([&] { return scheduler.pending() == 0; }, [] {});
        check(scheduler.takeResults().empty(), "cancelled phase published a result");
    }
    {
        MeshScheduler scheduler(2);
        for (int index = 0; index < 64; ++index) scheduler.submit({0, index, 0, 0}, 1, input(), assets, {});
        std::this_thread::sleep_for(50ms);
    }
}
