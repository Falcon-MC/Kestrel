#pragma once

#include "world/BlockAssets.h"
#include "world/MeshScheduler.h"
#include "world/WorldStream.h"

#include <array>
#include <atomic>
#include <map>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class BedrockConnection;
class MinecraftAuthentication;

namespace kestrel {

enum class SessionState {
    Idle,
    Resolving,
    Connecting,
    Joined,
    Disconnected,
    Failed,
};

struct MeshUpdate {
    world::SubChunkKey key;
    std::shared_ptr<const world::ChunkMesh> mesh;
};

struct SessionSnapshot {
    SessionState state = SessionState::Idle;
    std::string name;
    std::string target;
    std::string displayName;
    std::string levelName;
    std::string gameMode;
    std::string position;
    int dimension = 0;
    int chunkRadius = 0;
    uint64_t packetsReceived = 0;
    world::WorldStats world;
    size_t meshes = 0;
    size_t meshQuads = 0;
    size_t meshJobs = 0;
    size_t materials = 0;
    size_t textureLayers = 0;
    size_t diagnosticVisuals = 0;
    std::string assetsError;
    std::string error;
    uint64_t joinCount = 0;
    bool worldReady = false;
    double spawnX = 0.0;
    double spawnY = 0.0;
    double spawnZ = 0.0;
    float spawnYaw = 0.0f;
    float spawnPitch = 0.0f;
    uint64_t teleportCount = 0;
    bool hashedIds = false;
    size_t customBlocks = 0;
    size_t customPermutations = 0;
    std::string blockAtPlayer;
    uint32_t airSequential = 0;
    uint32_t airHash = 0;
    uint64_t unresolvedLookups = 0;
    uint32_t lastUnresolved = 0;
    std::string targetBlock;
    std::shared_ptr<const world::BlockAssets> assets;
    std::shared_ptr<const std::vector<uint8_t>> titleImage;
    bool packPrompt = false;
    size_t packCount = 0;
    uint64_t packBytes = 0;
    bool packDownloading = false;
    uint64_t packReceived = 0;
    uint64_t packTotal = 0;
    int64_t worldTime = 6000;
    double worldTimeStamp = 0.0;
    bool daylightCycle = true;
};

double secondsNow();
double currentWorldTime(const SessionSnapshot& snapshot);

class Session {
public:
    Session();
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void connect(std::string name, std::string target, MinecraftAuthentication* authentication, std::string offlineName);
    void disconnect();

    SessionSnapshot snapshot() const;
    std::vector<MeshUpdate> takeMeshUpdates();
    void setLookRay(const std::array<double, 3>& origin, const std::array<float, 3>& direction);
    void answerResourcePacks(bool download);

private:
    void run(std::string target, MinecraftAuthentication* authentication, std::string offlineName);
    void fail(const std::string& error);
    void handleWorldPacket(const std::string& payload);
    void scheduleMeshes();
    void collectMeshes();
    bool spawnAreaReady();
    std::string traceTarget();

    std::thread worker;
    std::atomic<bool> cancelled { false };
    mutable std::mutex mutex;
    SessionSnapshot current;
    std::unique_ptr<BedrockConnection> connection;
    world::WorldStream world;
    std::shared_ptr<const world::BlockAssets> assets;
    bool hashedNetworkIds = false;
    world::IdMapping ids;
    std::unique_ptr<world::MeshScheduler> mesher;
    std::map<world::SubChunkKey, uint64_t> meshGenerations;
    std::map<world::SubChunkKey, std::shared_ptr<const world::ChunkMesh>> meshes;
    size_t meshQuads = 0;
    std::vector<MeshUpdate> pendingUpdates;
    uint64_t joins = 0;
    double joinedAt = 0.0;
    uint64_t localRuntimeId = 0;
    std::array<double, 3> lookOrigin {};
    std::array<float, 3> lookDirection { 0.0f, 0.0f, -1.0f };
    std::atomic<int> packDecision { 0 };
};

}
