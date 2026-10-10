#include "render/OpaqueTerrain.h"

#include <cstdlib>
#include <cstdio>
#include <limits>

void check(bool passed, const char* message)
{
    if (!passed) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

int main()
{
    using namespace kestrel;
    std::array<uint8_t, 3> opaque { 1, 1, 0 };
    check(solidCubeMaterial(0, 0, opaque), "opaque material rejected");
    check(!solidCubeMaterial(2, 0, opaque), "cutout material accepted");
    check(!solidCubeMaterial(1 | (1u << 15), 0, opaque), "a transparent animation frame was ignored");
    check(!solidCubeMaterial(2 | (1u << 15), 0xc0000000u, opaque), "invalid animation range accepted");
    check(solidCubeMaterial(2, 0xc0000000u, opaque), "tint overlay mask treated as coverage");
    check(!solidCubeMaterial(2, 0x40000000u, opaque), "inactive tint overlay accepted");
    check(!solidCubeMaterial(0x1fffu, 0xc0000000u, opaque), "procedural material accepted");

    std::array<std::array<uint32_t, 5>, 8> quads {};
    for (uint32_t face = 0; face < 6; ++face) quads[face] = { face << 15, 0, face, 0x12345678u, 0x55u };
    quads[6] = { 0, 2, 6, 0, 0 };
    quads[7] = { 7u << 15, 0, 7, 0, 0 };
    auto partition = partitionCubeQuads(quads.data(), uint32_t(quads.size()), opaque);
    check(partition.offsets == std::array<uint32_t, 8>{ 0, 1, 2, 3, 4, 5, 6, 8 }, "quad partition ranges wrong");
    check(std::memcmp(quads.data(), partition.words.data(), sizeof(quads)) == 0, "quad attributes changed");
    uint32_t submitted = 0, draws = 0;
    drawSolidCubeRuns(partition.offsets, 63, [&](uint32_t first, uint32_t count) {
        check(first == 0 && count == 6, "adjacent solid ranges were not merged");
        submitted += count;
        ++draws;
    });
    check(draws == 1 && submitted == 6, "solid draws duplicated or omitted quads");
    check(facingCubeDirections({ 0, 0, 0 }, { 8, 8, 8 }) == 63, "camera inside chunk lost a direction");
    check(facingCubeDirections({ 0, 0, 0 }, { 32, 32, 32 }) == 42, "positive octant mask wrong");
    check(facingCubeDirections({ -16, -16, -16 }, { -32, -32, -32 }) == 21, "negative octant mask wrong");
    check(facingCubeDirections({ 0, 0, 0 }, { std::numeric_limits<double>::quiet_NaN(), 32, 32 }) == 63, "invalid camera must retain all directions");
    check(partitionCubeQuads(nullptr, 0, opaque).words.empty(), "empty mesh partition failed");

    std::array<uint8_t, 32> pixels;
    pixels.fill(255);
    pixels[7] = 128;
    const uint8_t* mips[] = { pixels.data() };
    auto layers = opaqueTextureLayers({ mips, 2, 2, 1 });
    check(layers == std::vector<uint8_t>{ 0, 1 }, "semi-transparent texture accepted as opaque");
    std::array<uint8_t, 8> smaller;
    smaller.fill(255);
    smaller[7] = 0;
    const uint8_t* mipChain[] = { pixels.data(), smaller.data() };
    check(opaqueTextureLayers({ mipChain, 2, 2, 2 }) == std::vector<uint8_t>{ 0, 0 }, "transparent mip ignored");

    std::array<std::array<uint32_t, ModelQuadBytes / sizeof(uint32_t)>, 4> models {};
    models[0][10] = 2;
    models[1][10] = 0;
    models[1][11] = 0x20;
    models[2][10] = 0;
    models[2][11] = 0x123400;
    models[3][10] = 0x1fff;
    auto modelRuns = partitionModelQuads(models.data(), uint32_t(models.size()), opaque);
    check(modelRuns.solidCount == 1, "transparent, procedural or entity model accepted as solid terrain");
    check(std::memcmp(modelRuns.words.data(), models[2].data(), ModelQuadBytes) == 0, "model attributes changed during partition");
    check(std::memcmp(modelRuns.words.data() + ModelQuadBytes / sizeof(uint32_t), models[0].data(), ModelQuadBytes) == 0, "model cutout order changed");
    check(partitionModelQuads(nullptr, 0, opaque).words.empty(), "empty model partition failed");
}
