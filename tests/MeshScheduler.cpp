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

void checkLighting()
{
    BlockAssets assets;
    MeshInput region;
    auto center = std::make_shared<SubChunk>();
    center->apply({ { 8, 8, 8, 0, 0 } });
    region.center = center;
    auto light = std::make_shared<ChunkLighting>();
    constexpr size_t volume = 48 * 48 * 48;
    light->block.resize(volume); light->sky.resize(volume);
    light->occluder.resize(volume); light->shade.resize(volume);
    auto cell = [](int x, int y, int z) { return (size_t(x + 16) * 48 + size_t(y + 16)) * 48 + size_t(z + 16); };
    light->occluder[cell(8, 8, 8)] = 1;
    light->block[cell(8, 9, 8)] = 3;
    light->sky[cell(8, 9, 8)] = 2;
    light->block[cell(7, 9, 8)] = 14;
    light->sky[cell(8, 9, 7)] = 15;
    for (auto sample : { cell(8, 9, 8), cell(7, 9, 8), cell(8, 9, 7), cell(7, 9, 7) }) light->shade[sample] = 1;
    light->occluder[cell(7, 9, 8)] = light->occluder[cell(8, 9, 7)] = 1;
    region.lighting = light;
    auto mesh = meshSubChunk(assets, {}, region);
    bool seen = false;
    for (const auto& quad : mesh.cubes) {
        if (((quad.geometry >> 15) & 7) != uint32_t(Face::PositiveY)) continue;
        seen = true;
        check((quad.light & 255) == 0xfe, "corner light must retain the independent maximum of each channel");
        check((quad.ao & 15) == 4, "AO must account for the outward face and three tangent samples");
    }
    check(seen, "synthetic block must expose its top face");
    region.center = std::make_shared<SubChunk>();
    region.lighting.reset();
    region.unknownAbove.fill(true);
    region.unknownAround.fill(true);
    region.unknownAround[13] = false;
    auto dark = meshSubChunk(assets, {}, region);
    check(dark.empty() && dark.light.size() == 4096, "empty chunks must retain solved light for entity sampling");
    check(dark.light[linearIndex(8, 8, 8)] == 0, "unknown chunks must not admit sky light");
    region.unknownAround.fill(false); region.unknownAbove.fill(false);
    auto sky = meshSubChunk(assets, {}, region);
    check(sky.light[linearIndex(8, 8, 8)] == 0xf0, "known empty chunks must admit direct sky light");
    region.skyLight = false;
    check(meshSubChunk(assets, {}, region).light[linearIndex(8, 8, 8)] == 0, "dimensions without a sky must not invent sky light");
}

void checkFilteredSky()
{
    std::string error;
    auto assets = BlockAssets::shared(error);
    check(bool(assets), error.c_str());
    BlockRegistry registry;
    check(registry.load(error), error.c_str());
    uint32_t leaves = ImplicitAir, stone = ImplicitAir;
    for (const auto& record : registry.records()) {
        if (record.name == "minecraft:oak_leaves") leaves = record.networkHash;
        if (record.name == "minecraft:stone") stone = record.networkHash;
    }
    check(leaves != ImplicitAir && stone != ImplicitAir, "lighting fixture blocks missing");
    check(assets->visual(leaves, true).lightFilter == 1, "leaves must transmit filtered sky light");
    IdMapping ids;
    ids.hashed = true;
    auto roof = [](uint32_t value) {
        auto chunk = std::make_shared<SubChunk>();
        for (uint8_t x = 0; x < 16; ++x)
            for (uint8_t z = 0; z < 16; ++z) chunk->apply({ { x, 0, z, 0, value } });
        return chunk;
    };
    MeshInput region;
    auto center = std::make_shared<SubChunk>();
    center->apply({ { 0, 0, 0, 0, stone } });
    region.center = center;
    region.unknownAround.fill(true);
    region.unknownAround[13] = region.unknownAround[16] = false;
    region.around[16] = std::make_shared<SubChunk>();
    region.unknownAbove.fill(true);
    region.unknownAbove[4] = false;
    region.above[4] = { roof(leaves) };
    auto cell = [](int x, int y, int z) { return (size_t(x + 16) * 48 + size_t(y + 16)) * 48 + size_t(z + 16); };
    auto filtered = updateChunkLighting(*assets, ids, region);
    check(bool(filtered), "filtered sky fixture must produce a light field");
    check(filtered->sky[cell(8, 31, 8)] == 13, "leaves above the light halo must filter sky rather than extinguish it");
    region.above[4] = { roof(stone) };
    auto opaque = updateChunkLighting(*assets, ids, region, filtered);
    check(opaque->sky[cell(8, 31, 8)] == 0, "an opaque roof must still block the sky");
    region.above[4] = { roof(leaves) };
    auto repaired = updateChunkLighting(*assets, ids, region, opaque);
    check(repaired->sky == filtered->sky, "incremental roof updates must match a fresh solve");
    region.above[4] = { nullptr, roof(leaves) };
    check(updateChunkLighting(*assets, ids, region)->sky[cell(8, 31, 8)] == 0,
        "known empty sub-chunks must retain attenuation distance beneath filtered sky");
    region = {};
    auto overhead = std::make_shared<SubChunk>();
    for (uint8_t x = 7; x <= 9; ++x)
        for (uint8_t z = 7; z <= 9; ++z) overhead->apply({ { x, 14, z, 0, stone } });
    region.center = overhead;
    check(updateChunkLighting(*assets, ids, region)->sky[cell(8, 4, 8)] >= 13,
        "an open-sided canopy ten blocks above must admit bright lateral sky light");
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--lighting-pack") checkFilteredSky();
    checkLighting();
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
        for (bool completed : { false, true }) {
            check(scheduler.submit(key, 3, input(true), assets, {}), "post-reset job rejected");
            if (completed) waitUntil([&] { return scheduler.pending() == 0; }, [] {});
            scheduler.invalidate(key, 4);
            auto discardInvalidated = [&] {
                for (const auto& result : scheduler.takeResults()) {
                    check(result.generation == 3 && !scheduler.isCurrent(result), "invalidated result remained current");
                }
            };
            waitUntil([&] { return scheduler.pending() == 0; }, discardInvalidated);
            // Completed results can remain queued after pending reaches zero.
            discardInvalidated();
            check(scheduler.submit(key, 4, input(true), assets, {}, true), "replacement job rejected");
            bool fresh = false;
            waitUntil([&] { return fresh; }, [&] {
                for (const auto& result : scheduler.takeResults()) {
                    check(result.generation == 4 && scheduler.isCurrent(result), "invalidated generation was published");
                    fresh = true;
                }
            });
        }

        for (int index = 0; index < 24; ++index) {
            check(scheduler.submit({0, index, 2, 0}, 5, input(true), assets, {}), "cancellation batch rejected");
        }
        for (int index = 0; index < 24; ++index) scheduler.cancel({0, index, 2, 0});
        waitUntil([&] { return scheduler.pending() == 0; }, [] {});
        check(scheduler.takeResults().empty(), "cancelled phase published a result");
    }
    {
        MeshScheduler scheduler(8);
        // Block changes must still finish when urgent work arrives during staged lighting.
        for (int batch = 0; batch < 16; ++batch) {
            size_t submitted = 0;
            for (int index = 0; index < 64; ++index) {
                submitted += scheduler.submit({0, index, batch + 10, 0}, 1, input(true), assets, {});
            }
            std::this_thread::sleep_for(10ms);
            for (int index = 0; index < 16; ++index) {
                MeshDeferred displaced;
                if (scheduler.submit({0, index, batch + 10, 1}, 1, input(true), assets, {}, true, &displaced)) {
                    ++submitted;
                    if (displaced.key.y == batch + 10) --submitted;
                }
            }
            size_t completed = 0;
            waitUntil([&] { return completed == submitted; }, [&] {
                for (const auto& result : scheduler.takeResults()) {
                    check(scheduler.isCurrent(result), "mixed-priority batch returned a stale mesh");
                    check(!result.mesh.empty(), "mixed-priority batch lost its geometry");
                    ++completed;
                }
            });
            waitUntil([&] { return scheduler.pending() == 0; }, [] {});
        }
    }
    {
        MeshScheduler scheduler(2);
        for (int index = 0; index < 64; ++index) scheduler.submit({0, index, 0, 0}, 1, input(), assets, {});
        std::this_thread::sleep_for(50ms);
    }
}
