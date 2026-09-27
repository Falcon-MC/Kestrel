#pragma once

#include "Protocol/PacketCodecContext.h"
#include "Protocol/Types/ItemStack.h"
#include "client/PlayerMotion.h"
#include "world/BlockAssets.h"
#include "world/MeshScheduler.h"
#include "world/WorldStream.h"

#include <array>
#include <atomic>
#include <map>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

class BedrockConnection;
class MinecraftAuthentication;
class Packet;
class SerializedSkin;

namespace kestrel {

enum class SessionState {
    Idle,
    Resolving,
    Connecting,
    Joined,
    Disconnected,
    Failed,
};

inline constexpr uint32_t NoSkin = 0xFFFFFFFFu;

/**
 * One entity the server has shown the client: its identifier, feet position,
 * body yaw in degrees, and for players the skin slot their skin sits in.
 */
struct ActorView {
    uint64_t runtimeId = 0;
    std::string identifier;
    std::string name;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float yaw = 0.0f;
    float headYaw = 0.0f;
    float pitch = 0.0f;
    float scale = 1.0f;
    std::array<uint64_t, 3> flags{};
    int variant = 0;
    int markVariant = 0;
    int color = 0;
    int skinId = 0;
    uint32_t skinSlot = NoSkin;
    bool slim = false;
    bool onGround = true;
    uint64_t moves = 0;
    uint64_t teleports = 0;
};

/**
 * A player skin to place in its entity texture slot, already scaled to the
 * entity texture size.
 */
struct SkinUpload {
    uint32_t slot = 0;
    std::vector<uint8_t> pixels;
    std::shared_ptr<const world::EntityRig> rig;
};

/**
 * One inventory stack as the HUD shows it: the item identifier, its count, its
 * aux value (the variant for legacy items), the damage of tools and armor and
 * any custom name.
 */
struct HudItem {
    std::string identifier;
    int32_t count = 0;
    int32_t aux = 0;
    int32_t damage = 0;
    std::string customName;
    std::string icon;

    bool empty() const
    {
        return identifier.empty() || count <= 0;
    }

    bool operator==(const HudItem&) const = default;
};

/**
 * A status effect on the local player; expires is in secondsNow time, or
 * negative when it lasts forever.
 */
struct HudEffect {
    int32_t id = 0;
    int32_t amplifier = 0;
    double expires = -1.0;
    bool ambient = false;
};

/**
 * Everything the gameplay HUD shows about the local player, as the server
 * reports it.
 */
struct HudState {
    int32_t gameType = 0;
    std::array<HudItem, 36> inventory {};
    std::array<HudItem, 4> armor {};
    HudItem offhand;
    int32_t selectedSlot = 0;
    double selectedChanged = 0.0;
    bool statsKnown = false;
    float health = 20.0f;
    float maxHealth = 20.0f;
    float absorption = 0.0f;
    float hunger = 20.0f;
    float saturation = 5.0f;
    float experience = 0.0f;
    int32_t level = 0;
    int32_t air = 300;
    int32_t maxAir = 300;
    double lastHealthDrop = 0.0;
    std::vector<HudEffect> effects;
};

/**
 * Where the local player's feet were at the last two ticks, and when the last
 * tick ran, so the camera can glide between them.
 */
struct PlayerView {
    bool active = false;
    std::array<double, 3> previous {};
    std::array<double, 3> current {};
    double tickTime = 0.0;
    bool sneaking = false;
    bool sprinting = false;
    bool flying = false;
    float movementSpeed = 0.1f;
    uint64_t teleports = 0;
};

/**
 * A sound the world asks for: a level event resolved through the block,
 * entity and individual event tables, a named sound definition, or a stop.
 */
struct SoundRequest {
    enum class Kind {
        Event,
        Named,
        Stop,
        StopAll,
    };

    Kind kind = Kind::Event;
    std::string name;
    std::array<double, 3> position {};
    std::string actor;
    std::string block;
    bool baby = false;
    bool global = false;
    float volume = 1.0f;
    float pitch = 1.0f;
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
    double boomFraction = 0.0;
    uint32_t localSkinSlot = NoSkin;
    bool localSlim = false;
    uint32_t airSequential = 0;
    uint32_t airHash = 0;
    uint64_t unresolvedLookups = 0;
    uint32_t lastUnresolved = 0;
    std::string targetBlock;
    std::shared_ptr<const world::BlockAssets> assets;
    std::vector<std::shared_ptr<const world::PackFiles>> packs;
    std::shared_ptr<const std::vector<uint8_t>> titleImage;
    bool packPrompt = false;
    size_t packCount = 0;
    bool packSkippable = true;
    uint64_t packBytes = 0;
    bool packDownloading = false;
    uint64_t packReceived = 0;
    uint64_t packTotal = 0;
    int64_t worldTime = 6000;
    double worldTimeStamp = 0.0;
    bool daylightCycle = true;
    float rainLevel = 0.0f;
    float thunderLevel = 0.0f;
    uint8_t cameraMedium = 0;
    bool cohortComplete = false;
    bool updatesPending = false;
    std::vector<ActorView> actors;
    HudState hud;
    PlayerView player;
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
    std::vector<SkinUpload> takeSkinUploads();
    std::vector<SoundRequest> takeSounds();
    void setLookRay(const std::array<double, 3>& origin, const std::array<float, 3>& direction);
    void setCameraBoom(const std::array<double, 3>& origin, const std::array<double, 3>& delta);
    void answerResourcePacks(bool download);
    void setRenderDistance(int chunks);
    void selectHotbarSlot(int slot);
    void setMotionInput(const MotionInput& input);

private:
    void handleMotionPacket(const std::shared_ptr<Packet>& packet);
    void handleSoundPacket(const std::shared_ptr<Packet>& packet);
    void queueSound(SoundRequest request);
    std::string blockNameAt(int32_t x, int32_t y, int32_t z);
    void playMotionSounds(const MotionTick& tick, const MotionVector& before);
    void tickMotion();
    MotionCell motionCell(int32_t x, int32_t y, int32_t z);
    bool motionAreaLoaded(const MotionVector& feet);
    void handleHudPacket(const std::shared_ptr<Packet>& packet);
    void sendSelectedSlot(int slot);
    void run(std::string target, MinecraftAuthentication* authentication, std::string offlineName);
    void fail(const std::string& error);
    void handleWorldPacket(const std::string& payload);
    void scheduleMeshes();
    void collectMeshes();
    void initializeLocalPlayer(BedrockConnection& target, uint64_t runtimeId);
    void moveActor(uint64_t runtimeId, double x, double y, double z, float yaw, float headYaw, float pitch, bool teleport, bool onGround);
    void storeSkin(const std::string& uuid, const SerializedSkin& skin);
    void releaseSkin(const std::string& uuid);
    std::string traceTarget();
    double boomFraction();
    uint8_t mediumAt(const std::array<double, 3>& position);

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
    uint64_t localRuntimeId = 0;
    int64_t localUniqueId = 0;
    std::string localUuid;
    std::array<double, 3> lookOrigin {};
    std::array<float, 3> lookDirection { 0.0f, 0.0f, -1.0f };
    std::array<double, 3> boomOrigin {};
    std::array<double, 3> boomDelta {};
    std::atomic<int> packDecision { 0 };
    std::atomic<int> requestedRadius { 16 };
    int sentRadius = 0;
    bool spawnInitialized = false;
    std::map<uint64_t, ActorView> actors;
    std::map<int64_t, uint64_t> runtimeByUnique;
    std::map<uint64_t, std::string> uuidByRuntime;
    std::map<std::string, std::pair<uint32_t, bool>> skinByUuid;
    std::array<std::string, world::SkinSlots> slotOwners;
    std::vector<SkinUpload> pendingSkins;
    std::set<int> seenPackets;
    double lastReadinessLog = 0.0;
    BlockDefinitionRegistry blockDefinitions;
    ItemDefinitionRegistry itemDefinitions;
    std::unique_ptr<PacketCodecContext> codecContext;
    std::array<ItemStack, 36> inventoryStacks {};
    std::atomic<int> requestedSlot { -1 };
    PlayerMotion motion;
    MotionInput motionInput;
    MotionInput lastMotionInput;
    bool motionStarted = false;
    bool teleportHandled = false;
    uint64_t clientTick = 0;
    double nextMotionTick = 0.0;
    int32_t motionDimension = 0;
    std::vector<SoundRequest> pendingSounds;
    float walkedDistance = 0.0f;
    float nextStepDistance = 1.0f;
    float fallStartY = 0.0f;
    bool wasOnGround = true;
};

}
