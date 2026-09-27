#pragma once

#include "Protocol/PacketCodecContext.h"
#include "Protocol/Types/ItemStack.h"
#include "client/PlayerMotion.h"
#include "client/Inventory.h"
#include "menu/ChatCommands.h"
#include "world/BlockAssets.h"
#include "world/MeshScheduler.h"
#include "world/WorldStream.h"

#include <array>
#include <atomic>
#include <map>
#include <optional>
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
/**
 * A block the look ray runs into: its cell, network block value and name, the
 * face it enters through, the point it hits and how far along the ray.
 */
struct BlockHit {
    std::array<int32_t, 3> cell {};
    uint32_t value = 0;
    std::string name;
    int32_t face = 0;
    std::array<double, 3> point {};
    double distance = 0.0;
};

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
    float height = 0.0f;
    float width = 0.0f;
    bool alwaysShowName = false;
    std::array<uint64_t, 3> flags{};
    int variant = 0;
    int markVariant = 0;
    int color = 0;
    int skinId = 0;
    int poseIndex = 0;
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
    InventoryState container;
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
    bool swimming = false;
    bool flying = false;
    float movementSpeed = 0.1f;

    double eyeHeight() const
    {
        return swimming ? 0.4 : sneaking ? 1.54 : 1.62;
    }
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

/**
 * A text packet as the server sent it. The client translates and formats it
 * with the language the menus use, so it stays raw until then.
 */
struct ChatMessage {
    enum class Kind {
        Raw,
        Chat,
        Translation,
        Popup,
        JukeboxPopup,
        Tip,
        System,
        Whisper,
        Announcement,
        WhisperJson,
        Json,
        AnnouncementJson,
    };

    Kind kind = Kind::Raw;
    std::string source;
    std::string message;
    std::vector<std::string> parameters;
    bool translate = false;
};

/**
 * A form from the server: its id and JSON, or with close set, the server
 * asking every open form to go away.
 */
struct FormRequest {
    uint32_t id = 0;
    std::string data;
    bool close = false;
};

/**
 * The objective shown in the sidebar slot: its display name and the lines in
 * the order the objective sorts them, at most the fifteen the game shows.
 */
struct SidebarView {
    bool visible = false;
    std::string title;
    std::vector<std::pair<std::string, int32_t>> lines;
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
    bool dead = false;
    bool changingDimension = false;
    std::string deathCause;
    std::vector<std::string> deathParameters;
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
    bool packsResolved = false;
    int64_t worldTime = 6000;
    double worldTimeStamp = 0.0;
    bool daylightCycle = true;
    float rainLevel = 0.0f;
    float thunderLevel = 0.0f;
    uint8_t cameraMedium = 0;
    bool cohortComplete = false;
    bool updatesPending = false;
    std::vector<ActorView> actors;
    std::shared_ptr<const std::vector<menu::ChatCommand>> commands;
    std::vector<std::string> players;
    SidebarView sidebar;
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
    std::vector<ChatMessage> takeChatMessages();
    std::vector<FormRequest> takeForms();

    /**
     * Queues a line typed into chat: commands (starting with a slash) go out
     * as command requests, anything else as a chat message.
     */
    void sendChat(std::string text);
    void setLookRay(const std::array<double, 3>& origin, const std::array<float, 3>& direction);
    void setCameraBoom(const std::array<double, 3>& origin, const std::array<double, 3>& delta);
    void answerResourcePacks(bool download);
    void setRenderDistance(int chunks);
    void selectHotbarSlot(int slot);
    void requestRespawn();
    void setMotionInput(const MotionInput& input);

    /**
     * Queues a click for the network thread: a right click uses the held
     * item, a left click hits the entity under the crosshair.
     */
    void requestInteraction(bool use);
    void requestInventory(InventoryCommand command);

    /**
     * Answers a form: with its response JSON, or without one when the player
     * closed it or was busy with another screen.
     */
    void answerForm(uint32_t id, std::optional<std::string> data, bool busy);

private:
    void handleMotionPacket(const std::shared_ptr<Packet>& packet);
    void handleSoundPacket(const std::shared_ptr<Packet>& packet);
    void handleChatPacket(const std::shared_ptr<Packet>& packet);
    void handleScorePacket(const std::shared_ptr<Packet>& packet);
    void rebuildSidebar();
    void flushChat();
    void handleFormPacket(const std::shared_ptr<Packet>& packet);
    void flushForms();
    void queueSound(SoundRequest request);
    std::string blockNameAt(int32_t x, int32_t y, int32_t z);
    void playMotionSounds(const MotionTick& tick, const MotionVector& before);
    void tickMotion();
    MotionCell motionCell(int32_t x, int32_t y, int32_t z);
    bool motionAreaLoaded(const MotionVector& feet);
    void handleHudPacket(const std::shared_ptr<Packet>& packet);
    void handleInventoryPacket(const std::shared_ptr<Packet>& packet);
    void flushInventory();
    void publishInventory();
    void sendSelectedSlot(int slot);
    void sendRespawnRequest();
    void run(std::string target, MinecraftAuthentication* authentication, std::string offlineName);
    void fail(const std::string& error);
    void handleWorldPacket(const std::string& payload);
    void scheduleMeshes();
    void finishDimensionChange();
    void collectMeshes();
    void initializeLocalPlayer(BedrockConnection& target, uint64_t runtimeId);
    void moveActor(uint64_t runtimeId, double x, double y, double z, float yaw, float headYaw, float pitch, bool teleport, bool onGround);
    void storeSkin(const std::string& uuid, const SerializedSkin& skin);
    void releaseSkin(const std::string& uuid);
    void assignSkin(const std::string& uuid);
    bool skinWorn(const std::string& uuid) const;
    std::string traceTarget();
    std::optional<BlockHit> traceBlock(double reach);
    void interact(bool use);
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
    std::map<std::string, SerializedSkin> knownSkins;
    std::map<std::string, std::string> playerNames;
    std::map<int64_t, std::string> playerNamesByActor;

    struct ScoreLine {
        std::string objective;
        int32_t score = 0;
        std::string name;
        int64_t actorId = -1;
        bool player = false;
    };

    std::map<std::string, std::string> objectives;
    std::map<std::string, std::pair<std::string, int32_t>> displaySlots;
    std::map<int64_t, ScoreLine> scores;
    std::array<std::string, world::SkinSlots> slotOwners;
    std::vector<SkinUpload> pendingSkins;
    std::set<int> seenPackets;
    double lastReadinessLog = 0.0;
    BlockDefinitionRegistry blockDefinitions;
    ItemDefinitionRegistry itemDefinitions;
    std::unique_ptr<PacketCodecContext> codecContext;
    InventoryModel inventoryModel;
    std::vector<InventoryCommand> inventoryCommands;
    std::optional<std::array<ItemStack, inventory::SlotCount>> inventoryBefore;
    std::set<int> inventoryChangedSlots;
    int32_t inventoryRequestId = -1;
    int32_t pendingInventoryRequest = 0;
    double inventoryRequestTime = 0.0;
    bool inventoryClosing = false;
    std::atomic<int> requestedSlot { -1 };
    std::atomic<bool> respawnRequested { false };
    bool respawnPending = false;
    bool dimensionAckReceived = false;
    std::atomic<bool> useRequested { false };
    std::atomic<bool> attackRequested { false };
    PlayerMotion motion;
    MotionInput motionInput;
    MotionInput lastMotionInput;
    bool motionStarted = false;
    bool teleportHandled = false;
    uint64_t clientTick = 0;
    double nextMotionTick = 0.0;
    int32_t motionDimension = 0;
    std::vector<SoundRequest> pendingSounds;
    std::vector<ChatMessage> pendingChat;
    std::vector<std::string> outgoingChat;

    struct FormAnswer {
        uint32_t id = 0;
        std::optional<std::string> data;
        bool busy = false;
    };

    std::vector<FormRequest> pendingForms;
    std::vector<FormAnswer> outgoingForms;
    std::string localXuid;
    float walkedDistance = 0.0f;
    float nextStepDistance = 1.0f;
    float fallStartY = 0.0f;
    bool wasOnGround = true;
};

}
