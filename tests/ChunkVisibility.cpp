#include "world/ChunkVisibility.h"

#include <cstdio>
#include <cstdlib>

using kestrel::world::ChunkVisibility;
using kestrel::world::ChunkVisibilityGraph;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
}

int main()
{
    std::bitset<4096> solid;
    auto air = ChunkVisibility::build(solid);
    check(air->cells.empty() && air->exits == std::vector<uint8_t>{63}, "uniform air must use the compact representation");
    solid.set();
    auto filled = ChunkVisibility::build(solid);
    check(filled->region(0) == ChunkVisibility::Solid, "uniform solid must block traversal");
    for (int z = 2; z < 14; ++z) {
        for (int y = 2; y < 14; ++y) {
            for (int x = 2; x < 14; ++x) solid.reset(ChunkVisibility::index(x, y, z));
        }
    }
    auto sealed = ChunkVisibility::build(solid);
    auto roomRegion = sealed->region(ChunkVisibility::index(8, 8, 8));
    check(roomRegion != ChunkVisibility::Solid && sealed->exits[roomRegion] == 0, "sealed air region acquired an exit");
    ChunkVisibilityGraph graph;
    graph.set({ 0, 0, 0 }, sealed);
    graph.set({ 4, 0, 0 }, air);
    graph.update({ 8, 8, 8 });
    check(graph.contains({0, 0, 0}) && !graph.contains({4, 0, 0}), "sealed room leaked to remote terrain");
    for (int x = 14; x < 16; ++x) solid.reset(ChunkVisibility::index(x, 8, 8));
    graph.set({0, 0, 0}, ChunkVisibility::build(solid));
    graph.update({8, 8, 8});
    check(graph.contains({4, 0, 0}), "opening a wall did not expose terrain");
    graph.set({0, 0, 0}, sealed);
    graph.update({8, 8, 8});
    check(!graph.contains({4, 0, 0}), "closing a wall did not invalidate cached visibility");
    graph.set({0, 0, 0}, nullptr);
    graph.update({8, 8, 8});
    check(graph.contains({4, 0, 0}), "unknown camera chunk must disable occlusion");
    graph.set({0, 0, 0}, filled);
    graph.update({8, 8, 8});
    check(graph.contains({4, 0, 0}), "camera inside solid terrain must disable occlusion");

    auto halfRoom = [](bool upper) {
        std::bitset<4096> blocks;
        for (int z = 1; z < 15; ++z) {
            for (int y = 0; y < 16; ++y) {
                for (int x = 1; x < 15; ++x) {
                    if (x == 1 || x == 14 || z == 1 || z == 14 || y == (upper ? 14 : 1))
                        blocks.set(ChunkVisibility::index(x, y, z));
                }
            }
        }
        return ChunkVisibility::build(blocks);
    };
    graph.clear();
    graph.set({0, -1, 0}, halfRoom(false));
    graph.set({0, 0, 0}, halfRoom(true));
    graph.set({4, 0, 0}, air);
    graph.update({8, -8, 8});
    check(graph.contains({0, 0, 0}), "connected half of a room disappeared");
    check(!graph.contains({4, 0, 0}), "disconnected air regions sharing a boundary face were joined");
    graph.update({8, 8, 8});
    check(!graph.contains({4, 0, 0}), "moving into another sub-chunk leaked the room");
    graph.update({0.5, 8, 0.5});
    check(graph.contains({4, 0, 0}), "outside air region must expose terrain");
    graph.update({std::numeric_limits<double>::quiet_NaN(), 8, 8});
    check(graph.contains({4, 0, 0}), "invalid camera must disable occlusion");
    graph.clear();
    graph.update({8, 8, 8});
    check(graph.contains({4, 0, 0}), "dimension reset retained visibility");
    for (int z = -2; z <= 2; ++z) {
        for (int y = -2; y <= 2; ++y) {
            for (int x = -2; x <= 2; ++x) graph.set({x, y, z}, filled);
        }
    }
    graph.set({0, 0, 0}, air);
    graph.update({8, 8, 8});
    check(graph.contains({1, 1, 1}), "model overhang at a sub-chunk corner disappeared");
    check(!graph.contains({2, 2, 2}), "model margin exposed unrelated remote terrain");
    graph.clear();
    graph.set({0, 0, 0}, sealed);
    graph.set({1000000, 0, 0}, air);
    graph.update({8, 8, 8});
    check(graph.contains({1000000, 0, 0}), "large coordinate range must use the bounded fallback");
    graph.update({8, 8, 8});
    check(graph.contains({1000000, 0, 0}), "cached fallback accidentally enabled occlusion");
}
