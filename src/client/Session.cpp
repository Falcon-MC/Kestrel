#include "client/Session.h"

#include "Network/BedrockConnection.h"
#include "Network/Client/ClientNetworkSystem.h"
#include "Network/Session/RealmsService.h"
#include "Protocol/Packets/ChangeDimensionPacket.h"
#include "Protocol/Packets/ChunkRadiusUpdatedPacket.h"
#include "Protocol/Packets/GameRulesChangedPacket.h"
#include "Protocol/Packets/SetTimePacket.h"
#include "Protocol/Packets/LevelChunkPacket.h"
#include "Protocol/Packets/MovePlayerPacket.h"
#include "Protocol/Packets/NetworkChunkPublisherUpdatePacket.h"
#include "Protocol/Packets/StartGamePacket.h"
#include "Protocol/Packets/SubChunkPacket.h"
#include "Protocol/Packets/SubChunkRequestPacket.h"
#include "Protocol/Packets/UpdateBlockPacket.h"
#include "Protocol/Packets/UpdateSubChunkBlocksPacket.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace kestrel {

double secondsNow()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double currentWorldTime(const SessionSnapshot& snapshot)
{
    if (!snapshot.daylightCycle) {
        return static_cast<double>(snapshot.worldTime);
    }
    return static_cast<double>(snapshot.worldTime) + (secondsNow() - snapshot.worldTimeStamp) * 20.0;
}

namespace {

constexpr int ProtocolVersion = 2193;
constexpr const char* GameVersion = "1.26.51";
constexpr const char* RealmPrefix = "realm_id/";
constexpr unsigned int TimeoutMs = 30000;
constexpr int ChunkRadius = 16;

const char* gameModeName(GameType type)
{
    switch (type) {
    case GameType::Survival:
        return "Survival";
    case GameType::Creative:
        return "Creative";
    case GameType::Adventure:
        return "Adventure";
    case GameType::Spectator:
        return "Spectator";
    case GameType::SurvivalViewer:
    case GameType::CreativeViewer:
        return "Viewer";
    case GameType::Default:
        break;
    }
    return "Default";
}

int32_t floorChunk(float blockCoordinate)
{
    return static_cast<int32_t>(std::floor(blockCoordinate / 16.0f));
}

bool parseHostPort(const std::string& address, std::string& host, unsigned short& port)
{
    size_t colon = address.rfind(':');
    host = colon == std::string::npos ? address : address.substr(0, colon);
    long parsed = colon == std::string::npos ? 19132 : std::strtol(address.c_str() + colon + 1, nullptr, 10);
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }
    if (host.empty() || parsed <= 0 || parsed > 65535) {
        return false;
    }
    port = static_cast<unsigned short>(parsed);
    return true;
}

}

Session::Session() = default;

Session::~Session()
{
    disconnect();
}

void Session::connect(std::string name, std::string target, MinecraftAuthentication* authentication, std::string offlineName)
{
    disconnect();
    cancelled = false;
    {
        std::lock_guard<std::mutex> guard(mutex);
        current = SessionSnapshot {};
        current.state = target.rfind(RealmPrefix, 0) == 0 ? SessionState::Resolving : SessionState::Connecting;
        current.name = std::move(name);
        current.target = target;
    }
    worker = std::thread([this, target = std::move(target), authentication, offlineName = std::move(offlineName)]() mutable {
        run(std::move(target), authentication, std::move(offlineName));
    });
}

void Session::disconnect()
{
    cancelled = true;
    if (worker.joinable()) {
        worker.join();
    }
    std::lock_guard<std::mutex> guard(mutex);
    if (current.state == SessionState::Joined) {
        current.state = SessionState::Idle;
    }
}

SessionSnapshot Session::snapshot() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return current;
}

std::vector<MeshUpdate> Session::takeMeshUpdates()
{
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<MeshUpdate> updates = std::move(pendingUpdates);
    pendingUpdates.clear();
    return updates;
}

void Session::handleWorldPacket(const std::string& payload)
{
    MinecraftPacketIds id;
    if (!BedrockConnection::peekPacketId(payload, id)) {
        return;
    }

    switch (id) {
    case MinecraftPacketIds::LevelChunk:
    case MinecraftPacketIds::SubChunk:
    case MinecraftPacketIds::UpdateBlock:
    case MinecraftPacketIds::UpdateSubChunkBlocks:
    case MinecraftPacketIds::NetworkChunkPublisherUpdate:
    case MinecraftPacketIds::ChunkRadiusUpdated:
    case MinecraftPacketIds::ChangeDimension:
    case MinecraftPacketIds::MovePlayer:
    case MinecraftPacketIds::SetTime:
    case MinecraftPacketIds::GameRulesChanged:
        break;
    default:
        return;
    }

    std::shared_ptr<Packet> packet = connection->decode(payload);
    if (!packet) {
        return;
    }

    if (auto levelChunk = std::dynamic_pointer_cast<LevelChunkPacket>(packet)) {
        world.handle(*levelChunk);
    } else if (auto subChunk = std::dynamic_pointer_cast<SubChunkPacket>(packet)) {
        world.handle(*subChunk);
    } else if (auto updateBlock = std::dynamic_pointer_cast<UpdateBlockPacket>(packet)) {
        world.handle(*updateBlock);
    } else if (auto updateSubChunk = std::dynamic_pointer_cast<UpdateSubChunkBlocksPacket>(packet)) {
        world.handle(*updateSubChunk);
    } else if (auto publisher = std::dynamic_pointer_cast<NetworkChunkPublisherUpdatePacket>(packet)) {
        world.handle(*publisher);
    } else if (auto radius = std::dynamic_pointer_cast<ChunkRadiusUpdatedPacket>(packet)) {
        world.setChunkRadius(radius->mRadius);
        std::lock_guard<std::mutex> guard(mutex);
        current.chunkRadius = radius->mRadius;
    } else if (auto move = std::dynamic_pointer_cast<MovePlayerPacket>(packet)) {
        if (static_cast<uint64_t>(move->mRuntimeActorId) == localRuntimeId) {
            std::lock_guard<std::mutex> guard(mutex);
            current.spawnX = move->mPosition.x;
            current.spawnY = move->mPosition.y;
            current.spawnZ = move->mPosition.z;
            current.spawnPitch = move->mRotation.x;
            current.spawnYaw = move->mRotation.y;
            ++current.teleportCount;
        }
    } else if (auto time = std::dynamic_pointer_cast<SetTimePacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        current.worldTime = time->mTime;
        current.worldTimeStamp = secondsNow();
    } else if (auto rules = std::dynamic_pointer_cast<GameRulesChangedPacket>(packet)) {
        for (const ChangedGameRuleData& rule : rules->mGameRules) {
            if (rule.mName == "dodaylightcycle" && rule.mType == ChangedGameRuleType::Bool) {
                std::lock_guard<std::mutex> guard(mutex);
                current.worldTime = currentWorldTime(current);
                current.worldTimeStamp = secondsNow();
                current.daylightCycle = rule.mBoolValue;
            }
        }
    } else if (auto dimension = std::dynamic_pointer_cast<ChangeDimensionPacket>(packet)) {
        world.changeDimension(dimension->mDimension, floorChunk(dimension->mPosition.x), floorChunk(dimension->mPosition.z));
        std::lock_guard<std::mutex> guard(mutex);
        current.dimension = dimension->mDimension;
    }
}

void Session::scheduleMeshes()
{
    std::vector<world::SubChunkKey> dirty = world.store().takeDirty();
    if (!assets) {
        return;
    }

    static constexpr int32_t Offsets[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    for (const world::SubChunkKey& key : dirty) {
        uint64_t generation = ++meshGenerations[key];
        std::shared_ptr<const world::SubChunk> center = world.store().subChunk(key);
        if (!center) {
            auto existing = meshes.find(key);
            if (existing != meshes.end()) {
                meshQuads -= existing->second->quadCount();
                meshes.erase(existing);
                std::lock_guard<std::mutex> guard(mutex);
                pendingUpdates.push_back({ key, nullptr });
            }
            continue;
        }

        world::MeshInput input;
        input.center = std::move(center);
        for (size_t face = 0; face < 6; ++face) {
            input.neighbours[face] = world.store().subChunk({ key.dimension, key.x + Offsets[face][0], key.y + Offsets[face][1], key.z + Offsets[face][2] });
        }
        for (int32_t dz = -1; dz <= 1; ++dz) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                input.biomes[size_t((dz + 1) * 3 + (dx + 1))] = world.store().biomes({ key.dimension, key.x + dx, key.y, key.z + dz });
            }
        }
        mesher->submit(key, generation, std::move(input), assets, ids);
    }
}

void Session::collectMeshes()
{
    for (world::MeshResult& result : mesher->takeResults()) {
        auto generation = meshGenerations.find(result.key);
        if (generation == meshGenerations.end() || generation->second != result.generation) {
            continue;
        }
        auto existing = meshes.find(result.key);
        bool hadMesh = existing != meshes.end();
        if (hadMesh) {
            meshQuads -= existing->second->quadCount();
            meshes.erase(existing);
        }

        const std::vector<world::Material>& materials = assets->materials();
        auto word = [&](uint32_t id) {
            return (id < materials.size() ? materials[id] : materials.front()).gpuWord();
        };
        for (auto* cubes : { &result.mesh.cubes, &result.mesh.translucentCubes }) {
            for (world::PackedQuad& quad : *cubes) {
                quad.material = word(quad.material);
            }
        }
        for (auto* models : { &result.mesh.models, &result.mesh.translucentModels }) {
            for (world::ModelQuadGpu& quad : *models) {
                quad.words[10] = word(quad.words[10]);
            }
        }

        std::shared_ptr<const world::ChunkMesh> mesh;
        if (!result.mesh.empty()) {
            meshQuads += result.mesh.quadCount();
            mesh = std::make_shared<const world::ChunkMesh>(std::move(result.mesh));
            meshes.emplace(result.key, mesh);
        }
        if (mesh || hadMesh) {
            std::lock_guard<std::mutex> guard(mutex);
            pendingUpdates.push_back({ result.key, std::move(mesh) });
        }
    }
}

void Session::fail(const std::string& error)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (cancelled) {
        current.state = SessionState::Idle;
        return;
    }
    current.state = SessionState::Failed;
    current.error = error;
}

void Session::run(std::string target, MinecraftAuthentication* authentication, std::string offlineName)
{
    ClientConnectionSettings settings;
    settings.mProtocolVersion = ProtocolVersion;
    settings.mGameVersion = GameVersion;
    settings.mAuthentication = authentication;
    settings.mIdentity.mDisplayName = offlineName;
    settings.mChunkRadius = ChunkRadius;
    settings.mTimeoutMs = TimeoutMs;
    settings.mCancel = &cancelled;

    if (target.rfind(RealmPrefix, 0) == 0) {
        if (!authentication) {
            fail("Sign in with Microsoft to join Realms");
            return;
        }
        long long realmId = std::strtoll(target.c_str() + std::char_traits<char>::length(RealmPrefix), nullptr, 10);
        RealmsService realms(*authentication);
        RealmAddress address;
        SessionConnectionTarget resolved;
        std::string error;
        if (!realms.requestAddress(realmId, TimeoutMs, &cancelled, address, error) || !address.toTarget(resolved, error)) {
            fail(error);
            return;
        }
        resolved.applyTo(settings);
        std::lock_guard<std::mutex> guard(mutex);
        current.state = SessionState::Connecting;
    } else if (!parseHostPort(target, settings.mHost, settings.mPort)) {
        fail("Invalid server address: " + target);
        return;
    }

    ClientConnectionResult result = ClientNetworkSystem::dial(settings);
    if (!result.mConnection) {
        fail(result.mError.empty() ? "Could not connect" : result.mError);
        return;
    }

    {
        std::lock_guard<std::mutex> guard(mutex);
        connection = std::move(result.mConnection);
        current.state = SessionState::Joined;
        current.displayName = result.mIdentity.mDisplayName;
        current.chunkRadius = connection->getChunkRadius();
        current.joinCount = ++joins;
        pendingUpdates.clear();
        if (const std::shared_ptr<StartGamePacket>& startGame = connection->getStartGame()) {
            current.spawnX = startGame->mPlayerPosition.x;
            current.spawnY = startGame->mPlayerPosition.y;
            current.spawnZ = startGame->mPlayerPosition.z;
            current.spawnPitch = startGame->mRotation.x;
            current.spawnYaw = startGame->mRotation.y;
            current.hashedIds = startGame->mBlockNetworkIdsHashed;
            localRuntimeId = startGame->mRuntimeActorId;
            current.levelName = startGame->mLevelName;
            current.gameMode = gameModeName(startGame->mPlayerGameType);
            current.dimension = startGame->mDimensionId;
            current.worldTimeStamp = secondsNow();
            for (const GameRuleData& rule : startGame->mGamerules) {
                if (rule.mName == "dodaylightcycle" && rule.mType == GameRuleData::Type::Bool) {
                    current.daylightCycle = rule.mBoolValue;
                }
            }
            char position[96];
            std::snprintf(position, sizeof(position), "%.1f, %.1f, %.1f", startGame->mPlayerPosition.x, startGame->mPlayerPosition.y, startGame->mPlayerPosition.z);
            current.position = position;
        }
    }

    if (const std::shared_ptr<StartGamePacket>& startGame = connection->getStartGame()) {
        world.reset(startGame->mDimensionId, floorChunk(startGame->mPlayerPosition.x), floorChunk(startGame->mPlayerPosition.z));
        hashedNetworkIds = startGame->mBlockNetworkIdsHashed;
    }
    world.setChunkRadius(connection->getChunkRadius());

    std::string assetsError;
    assets = world::BlockAssets::shared(assetsError);
    ids = world::IdMapping {};
    ids.hashed = hashedNetworkIds;
    size_t customCount = 0;
    size_t customPermutationCount = 0;
    if (assets) {
        if (const std::shared_ptr<StartGamePacket>& startGame = connection->getStartGame()) {
            std::vector<world::CustomBlock> customBlocks;
            for (const BlockPropertyData& block : startGame->mBlockProperties) {
                world::CustomBlock custom;
                custom.name = block.mName;
                if (const Tag* properties = block.mProperties.get("properties")) {
                    for (const Tag& property : properties->getList()) {
                        if (const Tag* values = property.get("enum")) {
                            custom.permutations *= std::max<uint32_t>(1, static_cast<uint32_t>(values->getList().size()));
                        }
                    }
                }
                customBlocks.push_back(std::move(custom));
            }
            customCount = customBlocks.size();
            for (const world::CustomBlock& custom : customBlocks) {
                customPermutationCount += custom.permutations;
            }
            ids.sequential = assets->sequentialMap(customBlocks);
        }
    }
    if (!mesher) {
        mesher = std::make_unique<world::MeshScheduler>();
    }
    mesher->clear();
    meshGenerations.clear();
    meshes.clear();
    meshQuads = 0;
    {
        std::lock_guard<std::mutex> guard(mutex);
        current.assetsError = assetsError;
        current.customBlocks = customCount;
        current.customPermutations = customPermutationCount;
        if (assets) {
            current.materials = assets->materials().size();
            current.textureLayers = assets->textures().layers;
            current.diagnosticVisuals = assets->diagnosticVisuals();
        }
    }

    std::string payload;
    while (!cancelled) {
        bool received = connection->readRaw(payload, 50, &cancelled);
        if (received) {
            handleWorldPacket(payload);
        } else if (connection->isClosed()) {
            break;
        }

        for (const std::unique_ptr<SubChunkRequestPacket>& request : world.takeRequests(world::WorldStream::Clock::now())) {
            connection->send(*request);
        }
        scheduleMeshes();
        collectMeshes();

        std::lock_guard<std::mutex> guard(mutex);
        if (received) {
            ++current.packetsReceived;
        }
        current.world = world.stats();
        current.meshes = meshes.size();
        current.meshQuads = meshQuads;
        current.meshJobs = mesher->pending();
        if (assets) {
            int32_t bx = static_cast<int32_t>(std::floor(current.spawnX));
            int32_t by = static_cast<int32_t>(std::floor(current.spawnY));
            int32_t bz = static_cast<int32_t>(std::floor(current.spawnZ));
            world::SubChunkKey probe { current.dimension, bx >> 4, by >> 4, bz >> 4 };
            if (std::shared_ptr<const world::SubChunk> sub = world.store().subChunk(probe)) {
                uint32_t value = sub->runtimeId(0, uint32_t(bx & 15), uint32_t(by & 15), uint32_t(bz & 15));
                current.blockAtPlayer = assets->describe(value, ids.hashed, ids.sequential.get());
            } else {
                current.blockAtPlayer = "no sub-chunk (air)";
            }
            current.airSequential = assets->airSequentialId();
            current.airHash = assets->airNetworkHash();
            current.unresolvedLookups = assets->unresolvedLookups();
            current.lastUnresolved = assets->lastUnresolvedValue();
        }
    }

    std::lock_guard<std::mutex> guard(mutex);
    if (cancelled) {
        connection->disconnect("Disconnected");
        current.state = SessionState::Idle;
    } else {
        current.state = SessionState::Disconnected;
        current.error = connection->getDisconnectReason();
    }
    connection.reset();
}

}
