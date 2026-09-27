#include "client/Client.h"

#include "platform/Window.h"
#include "render/Renderer.h"

#include <cmath>
#include <cstdio>
#include <thread>

namespace kestrel {

namespace {

template <typename... Args>
std::string format(const char* pattern, Args... args)
{
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), pattern, args...);
    return buffer;
}

std::string dimensionName(int dimension)
{
    switch (dimension) {
    case 0:
        return "minecraft:overworld";
    case 1:
        return "minecraft:nether";
    case 2:
        return "minecraft:the_end";
    default:
        return "dimension " + std::to_string(dimension);
    }
}

std::string facing(float yaw, float pitch)
{
    static constexpr const char* Names[4][2] = {
        { "south", "Towards positive Z" },
        { "west", "Towards negative X" },
        { "north", "Towards negative Z" },
        { "east", "Towards positive X" },
    };
    float wrapped = std::remainder(yaw, 360.0f);
    int index = static_cast<int>(std::floor(wrapped / 90.0f + 0.5f)) & 3;
    return format("Facing: %s (%s) (%.1f / %.1f)", Names[index][0], Names[index][1], wrapped, pitch);
}

}

void Client::countFrame(std::chrono::steady_clock::time_point now)
{
    ++framesCounted;
    if (now - fpsWindowStart < std::chrono::seconds(1)) {
        return;
    }
    framesPerSecond = framesCounted;
    framesCounted = 0;
    fpsWindowStart = now;
    if (menu.debugVisible()) {
        memory = platform::memoryUsage();
    }
}

menu::DebugView Client::buildDebugView(const SessionSnapshot& snapshot)
{
    menu::DebugView view;
    std::vector<std::string>& left = view.left;
    std::vector<std::string>& right = view.right;

    int limit = menu.maxFps();
    left.push_back(format("Kestrel %.*s (Bedrock)", static_cast<int>(Session::gameVersion().size()), Session::gameVersion().data()));
    left.push_back(std::to_string(framesPerSecond) + " fps T: " + (limit == menu::UnlimitedFps ? std::string("inf") : std::to_string(limit)));
    left.push_back("Server: " + (snapshot.levelName.empty() ? snapshot.name : snapshot.levelName));
    left.push_back(format("C: %zu/%zu D: %d, pC: %zu, pU: %zu", visibleTerrain(), snapshot.meshes, snapshot.chunkRadius, snapshot.world.pendingSubChunks, snapshot.meshJobs));
    left.push_back(format("E: %zu, Q: %zu, T: %zu", snapshot.actors.size(), snapshot.meshQuads, snapshot.textureLayers));
    left.push_back(dimensionName(snapshot.dimension));
    left.push_back(format("IDs: %s, custom %zu (%zu states), unknown %llu", snapshot.hashedIds ? "hashed" : "sequential", snapshot.customBlocks, snapshot.customPermutations,
        static_cast<unsigned long long>(snapshot.unresolvedLookups)));
    left.emplace_back();

    std::array<double, 3> feet = playerView.active ? playerView.current : std::array<double, 3> { camera.x(), camera.y(), camera.z() };
    std::array<int32_t, 3> block {};
    for (int axis = 0; axis < 3; ++axis) {
        block[axis] = static_cast<int32_t>(std::floor(feet[axis]));
    }
    left.push_back(format("XYZ: %.3f / %.5f / %.3f", feet[0], feet[1], feet[2]));
    left.push_back(format("Block: %d %d %d [%d %d %d]", block[0], block[1], block[2], block[0] & 15, block[1] & 15, block[2] & 15));
    left.push_back(format("Chunk: %d %d %d", block[0] >> 4, block[1] >> 4, block[2] >> 4));
    left.push_back(facing(camera.minecraftYaw(), camera.minecraftPitch()));
    if (!snapshot.gameMode.empty()) {
        left.push_back("Game Mode: " + snapshot.gameMode);
    }
    if (snapshot.world.decodeErrors > 0 || !snapshot.assetsError.empty()) {
        left.emplace_back();
        left.push_back("Decode errors: " + std::to_string(snapshot.world.decodeErrors));
        if (!snapshot.world.lastError.empty()) {
            left.push_back(snapshot.world.lastError);
        }
        if (!snapshot.assetsError.empty()) {
            left.push_back(snapshot.assetsError);
        }
    }

    constexpr uint64_t Megabyte = 1024 * 1024;
    int percent = memory.physical ? static_cast<int>(memory.resident * 100 / memory.physical) : 0;
    right.push_back(format("Mem: %2d%% %03llu/%03lluMB", percent, static_cast<unsigned long long>(memory.resident / Megabyte),
        static_cast<unsigned long long>(memory.physical / Megabyte)));
    right.push_back("CPU: " + std::to_string(std::thread::hardware_concurrency()) + "x " + processor);
    right.emplace_back();
    right.push_back(format("Display: %ux%u (%.*s)", window->width(), window->height(), static_cast<int>(renderer->backendName().size()), renderer->backendName().data()));
    right.push_back(renderer->deviceName());

    if (snapshot.targetBlock) {
        const TargetBlock& target = *snapshot.targetBlock;
        right.emplace_back();
        right.push_back(format("Targeted Block: %d, %d, %d", target.cell[0], target.cell[1], target.cell[2]));
        right.push_back(target.name);
        right.insert(right.end(), target.states.begin(), target.states.end());
    }
    return view;
}

}
