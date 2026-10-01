#pragma once

#include "Protocol/PacketCodecContext.h"
#include "Protocol/Types/ItemStack.h"
#include "client/PlayerMotion.h"
#include "client/Inventory.h"
#include "client/PacketHook.h"
#include "client/PacketJournal.h"
#include "menu/ChatCommands.h"
#include "world/BlockAssets.h"
#include "world/BlockCollisions.h"
#include "world/MeshScheduler.h"
#include "world/Particles.h"
#include "world/WorldStream.h"

#include <array>
#include <atomic>
#include <map>
#include <optional>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

class BedrockConnection;
class BossEventPacket;
class LevelEventPacket;
class MinecraftAuthentication;
class Packet;
class PacketViolationWarningPacket;
class PlayerAuthInputPacket;
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

/**
 * The box outlined around the block under the crosshair, in world
 * coordinates.
 */
struct BlockSelection {
    std::array<int32_t, 3> cell {};
    std::array<double, 3> min {};
    std::array<double, 3> max {};
};

/**
 * A block someone is breaking: its look, the boxes of its shape relative to
 * its cell, which the cracks cover when it has no model, and how far along
 * the breaking is, from 0 to 1.
 */
struct BlockCrack {
    std::array<int32_t, 3> cell {};
    world::BlockVisual visual;
    std::vector<world::CollisionBox> boxes;
    float progress = 0.0f;
};

/**
 * A chest lid on the move: the chest's cell, its lid model and turns, whether
 * the chest is open, and how far the lid has opened from 0 to 1 at the last
 * tick.
 */
struct ChestLidView {
    std::array<int32_t, 3> cell {};
    world::ChestLid lid;
    bool open = false;
    float openness = 0.0f;
};

/**
 * Particles a block throws off: the burst of a broken block or the chip
 * knocked off the face being mined. Material is a block texture word and
 * tint 0xRRGGBB (0 for none); the shape and the obstacles the particles
 * bounce on are relative to the cell.
 */
struct ParticleBurst {
    enum class Kind {
        Destroy,
        Crack,
    };

    Kind kind = Kind::Destroy;
    std::array<int32_t, 3> cell {};
    int32_t face = 0;
    uint32_t material = 0;
    uint32_t tint = 0;
    world::CollisionBox shape;
    std::shared_ptr<const std::vector<world::CollisionBox>> obstacles;
};

/**
 * The block the crosshair rests on as the debug screen lists it: where it
 * is, its identifier and each state as "name: value".
 */
struct TargetBlock {
    std::array<int32_t, 3> cell {};
    std::string name;
    std::vector<std::string> states;
};

struct ActorView {
    int64_t uniqueId = 0;
    double lastHurt = 0.0;
    double lastSwing = 0.0;
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
    std::string scoreTag;
    float nameplateDistance = 64.0f;
    std::array<uint64_t, 3> flags{};
    int variant = 0;
    int markVariant = 0;
    int color = 0;
    int skinId = 0;
    int poseIndex = 0;
    uint32_t skinSlot = NoSkin;
    bool slim = false;
    bool onGround = true;
    // Helmet, chestplate, leggings and boots item identifiers, empty when bare.
    std::array<std::string, 4> armor {};
    HudItem held;
    uint32_t effectColor = 0;
    uint64_t moves = 0;
    uint64_t teleports = 0;
    // A dropped item: the stack it shows, and once picked up who took it and when.
    HudItem item;
    uint64_t pickedUpBy = 0;
    double pickedUpAt = 0.0;
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
/**
 * A boss bar the server shows: the boss it follows, its title, how full it
 * is from 0 to 1 and its color (pink, blue, red, green, yellow, purple,
 * rebecca purple, white).
 */
struct BossBarView {
    int64_t bossId = 0;
    std::string title;
    float progress = 1.0f;
    int32_t color = 0;
};

struct HudState {
    int32_t gameType = 0;
    std::vector<BossBarView> bossBars;
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
    double lastHurt = 0.0;
    double lastSwing = 0.0;
    // When the selected item started being held in use, like a drawn bow, 0 while it is not.
    double itemUseStarted = 0.0;
    std::vector<HudEffect> effects;
    // HUD elements the server hid with SetHud, one bit per HudElement.
    uint32_t hiddenElements = 0;
};

/**
 * Where the local player's feet were at the last two ticks, and when the last
 * tick ran, so the camera can glide between them.
 */
struct PlayerView {
    bool active = false;
    bool onGround = true;
    std::array<double, 3> previous {};
    std::array<double, 3> current {};
    double tickTime = 0.0;
    bool sneaking = false;
    bool sprinting = false;
    bool swimming = false;
    bool flying = false;
    bool mayFly = false;
    bool operatorCommands = false;
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
 * The action bar text a title packet sets, as raw JSON text when json is set.
 */
struct ActionbarText {
    std::string text;
    bool json = false;
};

/**
 * What a title packet asks of the title and subtitle: clear or reset them,
 * set either text (raw JSON text when json is set) or set the fade times in
 * ticks.
 */
struct TitleRequest {
    enum class Kind {
        Clear,
        Reset,
        Title,
        Subtitle,
        Times,
    };

    Kind kind = Kind::Clear;
    std::string text;
    bool json = false;
    int32_t fadeIn = 0;
    int32_t stay = 0;
    int32_t fadeOut = 0;
};

/**
 * A toast the server asks for: the title and the line shown under it.
 */
struct ToastRequest {
    std::string title;
    std::string content;
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

/**
 * The loaded sub-chunks around the local player as they were at one tick,
 * with what it takes to read their blocks. Sub-chunks never change once
 * shared, so the client reads them without a lock: particles collide with
 * them and the lit blocks among them give off flames and smoke.
 */
struct NearbyBlocks {
    static constexpr int32_t Radius = 2;
    static constexpr int32_t Span = Radius * 2 + 1;

    int32_t dimension = 0;
    std::array<int32_t, 3> base {};
    std::vector<std::shared_ptr<const world::SubChunk>> subChunks;
    std::vector<std::shared_ptr<const world::PalettedStorage>> biomes;
    std::shared_ptr<const world::BlockAssets> assets;
    world::IdMapping ids;

    /**
     * The network block value of the first layer at a cell, or ImplicitAir
     * outside the area or in a sub-chunk that is not loaded.
     */
    uint32_t value(int32_t x, int32_t y, int32_t z) const;
    std::string name(int32_t x, int32_t y, int32_t z) const;

    /**
     * Appends the collision boxes, in world coordinates, of every block
     * touching the box from low to high.
     */
    void collisionBoxes(const std::array<double, 3>& low, const std::array<double, 3>& high, std::vector<std::array<double, 6>>& out) const;

    /**
     * Appends every cell within radius blocks of center whose block value
     * wanted accepts, skipping the sub-chunks whose palette has none.
     */
    void find(const std::array<double, 3>& center, int32_t radius, const std::function<bool(uint32_t)>& wanted, std::vector<std::pair<std::array<int32_t, 3>, uint32_t>>& out) const;
};

/**
 * Every loaded sub-chunk of the current dimension as it was at one tick,
 * with what it takes to read their blocks, shared with the main thread for
 * mods that look anywhere in the world.
 */
struct LoadedBlocks {
    int32_t dimension = 0;
    std::map<world::SubChunkKey, std::shared_ptr<const world::SubChunk>> subChunks;
    std::shared_ptr<const world::BlockAssets> assets;
    world::IdMapping ids;

    bool loaded(int32_t x, int32_t y, int32_t z) const;

    /**
     * The network block value of the first layer, or ImplicitAir where no
     * sub-chunk is loaded.
     */
    uint32_t value(int32_t x, int32_t y, int32_t z) const;
    std::string name(int32_t x, int32_t y, int32_t z) const;

    /**
     * The block's states, each as "name: value".
     */
    std::vector<std::string> states(int32_t x, int32_t y, int32_t z) const;

    /**
     * Whether the look ray stops at the block: anything but air, liquids and
     * the blocks a mod hid.
     */
    bool selectable(int32_t x, int32_t y, int32_t z) const;
};

struct MeshUpdate {
    world::SubChunkKey key;
    std::shared_ptr<const world::ChunkMesh> mesh;
    std::shared_ptr<void> credit;
};

/**
 * The last field of view the server set through a camera instruction: its
 * degrees, how long the change eases and with which curve, or a release back
 * to the player's own setting. serial counts the instructions received.
 */
struct CameraFovRequest {
    uint64_t serial = 0;
    float degrees = 0.0f;
    float easeSeconds = 0.0f;
    int easeType = 0;
    bool clear = false;
};

struct SessionSnapshot {
    uint64_t globalResourcesRevision = 0;
    uint64_t resourceReloadSerial = 0;
    bool resourceReloading = false;
    std::string resourceReloadError;
    std::shared_ptr<const std::vector<MeshUpdate>> reloadedMeshes;
    SessionState state = SessionState::Idle;
    CameraFovRequest cameraFov;
    int64_t localUniqueActorId = 0;
    uint64_t localRuntimeId = 0;
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
    std::string packetError;
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
    double serverBoomFraction = 0.0;
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
    std::optional<TargetBlock> targetBlock;
    std::optional<BlockSelection> selection;
    // The identifier of the entity the local player rides, empty on foot.
    std::string riding;
    std::vector<BlockCrack> cracks;
    std::vector<ChestLidView> chestLids;
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
    std::shared_ptr<const NearbyBlocks> nearby;
    std::shared_ptr<const NearbyBlocks> cameraBlocks;
    std::shared_ptr<const LoadedBlocks> loaded;
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

    static std::string_view gameVersion();
    static int protocolVersion();

    SessionSnapshot snapshot() const;
    std::shared_ptr<const SessionSnapshot> sharedSnapshot() const;
    std::vector<MeshUpdate> takeMeshUpdates(size_t maximum = 32, const world::BlockAssets* expectedAssets = nullptr);
    std::vector<std::shared_ptr<const Packet>> takeCameraEvents();
    std::vector<SkinUpload> takeSkinUploads();
    std::vector<SoundRequest> takeSounds();
    std::vector<ChatMessage> takeChatMessages();
    std::optional<ActionbarText> takeActionbar();
    std::vector<TitleRequest> takeTitles();
    std::vector<ToastRequest> takeToasts();
    std::vector<FormRequest> takeForms();

    /**
     * Queues a line typed into chat: commands (starting with a slash) go out
     * as command requests, anything else as a chat message.
     */
    void sendChat(std::string text);
    void setLookRay(const std::array<double, 3>& origin, const std::array<float, 3>& direction);
    void setRenderedCamera(const std::array<double, 3>& origin);
    std::pair<uint8_t, uint32_t> cameraEnvironment(const SessionSnapshot& snapshot, const std::array<double, 3>& position, bool renderedSurface = true);
    void setCameraBoom(const std::array<double, 3>& origin, const std::array<double, 3>& delta);
    void setServerCameraBoom(const std::array<double, 3>& origin, const std::array<double, 3>& delta);
    void answerResourcePacks(bool download);
    void setRenderDistance(int chunks);
    void setGlobalPacks(std::vector<std::shared_ptr<const world::PackFiles>> packs, uint64_t revision);
    void selectHotbarSlot(int slot);
    void requestRespawn();
    void setMotionInput(const MotionInput& input);

    /**
     * Queues a click for the network thread: a right click uses the held
     * item, a left click hits the entity under the crosshair.
     */
    void requestInteraction(bool use);

    /**
     * Whether the attack button is held down in game, which keeps mining the
     * block under the crosshair.
     */
    void setAttackHeld(bool held);

    /**
     * Whether the use button is held down in game, which keeps placing along
     * the line the first placement started.
     */
    void setUseHeld(bool held);

    /**
     * Whether the use button is held down in game.
     */
    bool useIsHeld() const
    {
        return useHeld.load();
    }

    /**
     * Queues a pick of the block under the crosshair: the server selects it
     * in the hotbar, or gives it in creative. With data, the block entity's
     * contents come along.
     */
    void requestPickBlock(bool withData);
    std::vector<ParticleBurst> takeParticleBursts();

    /**
     * The particle effects the server started since the last call: level
     * event particles and SpawnParticleEffect packets of the current
     * dimension.
     */
    std::vector<world::ParticleSpawn> takeParticles();

    /**
     * Draws every block of the given names as air, or with visibleOnly every
     * block but them, redrawing the loaded terrain when that changes.
     */
    void setHiddenBlocks(std::set<std::string> names, bool visibleOnly = false);

    /**
     * The runtime ids of the entities the local player hit since the last
     * call.
     */
    std::vector<uint64_t> takeAttacks();
    void requestInventory(InventoryCommand command);

    /**
     * Answers a form: with its response JSON, or without one when the player
     * closed it or was busy with another screen.
     */
    void answerForm(uint32_t id, std::optional<std::string> data, bool busy);

    PacketJournal& packets()
    {
        return journal;
    }

    /**
     * Queues a raw game packet payload, header included, to go out as it is.
     * Meant for protocol debugging; the server sees whatever was written.
     */
    void sendRawPacket(std::string payload);

    /**
     * Routes every game packet through hook from the next connection on. Set
     * it before connecting; the network thread reads it without a lock.
     */
    void setPacketHook(std::shared_ptr<PacketHook> hook)
    {
        packetHook = std::move(hook);
    }

private:
    void transmit(const Packet& packet);
    void handleMotionPacket(const std::shared_ptr<Packet>& packet);
    void handleSoundPacket(const std::shared_ptr<Packet>& packet);
    void handleChatPacket(const std::shared_ptr<Packet>& packet);
    void handleScorePacket(const std::shared_ptr<Packet>& packet);
    void rebuildSidebar();
    void flushChat();
    void handleFormPacket(const std::shared_ptr<Packet>& packet);
    void flushForms();
    void queueSound(SoundRequest request);
    void queueParticle(world::ParticleSpawn spawn);
    void publishNearby();
    void publishCameraBlocks();
    void publishLoaded();
    void applyHiddenBlocks();
    void hideNewValues(const world::SubChunk* subChunk);
    std::string blockNameAt(int32_t x, int32_t y, int32_t z);
    void playMotionSounds(const MotionTick& tick, const MotionVector& before);
    void tickMotion();
    void runMotionTick(double now);
    void replayCorrection(uint64_t tick, const MotionVector& position, const MotionVector* velocity, bool onGround);
    MotionCell motionCell(int32_t x, int32_t y, int32_t z);
    bool motionAreaLoaded(const MotionVector& feet);
    void handleHudPacket(const std::shared_ptr<Packet>& packet);
    void handleBossEvent(const BossEventPacket& event);
    void handleInventoryPacket(const std::shared_ptr<Packet>& packet);
    void flushInventory();
    void publishInventory();
    void sendSelectedSlot(int slot);
    void sendRespawnRequest();
    void run(std::string target, MinecraftAuthentication* authentication, std::string offlineName);
    std::optional<std::string> join(const std::string& target, MinecraftAuthentication* authentication, const std::string& offlineName);
    void resetSnapshot(std::string name, std::string target);
    std::shared_ptr<const world::PackFiles> cachedPack(const std::string& path);
    void publishSnapshotLocked(std::shared_ptr<const SessionSnapshot> snapshot = {});
    void collectViewInput();
    void cachePackLocked(const std::string& path, std::shared_ptr<const world::PackFiles> pack);
    void fail(const std::string& error);
    void handleWorldPacket(std::string& payload);
    void handleViolation(const PacketViolationWarningPacket& violation);
    void scheduleMeshes();
    void finishDimensionChange();
    void collectMeshes();
    void initializeLocalPlayer(BedrockConnection& target, uint64_t runtimeId);
    void moveActor(uint64_t runtimeId, double x, double y, double z, float yaw, float headYaw, float pitch, bool teleport, bool onGround, bool feetPosition = false);
    void storeSkin(const std::string& uuid, const SerializedSkin& skin);
    void releaseSkin(const std::string& uuid);
    void assignSkin(const std::string& uuid);
    bool skinWorn(const std::string& uuid) const;
    std::optional<TargetBlock> traceTarget();
    std::optional<BlockHit> traceBlock(double reach);
    std::optional<BlockHit> traceBlock(const std::array<double, 3>& origin, const std::array<float, 3>& direction, double reach);
    const ActorView* traceActor(const std::array<double, 3>& origin, const std::array<double, 3>& direction, double reach, double& distance) const;
    void interact(bool use);
    bool openBook(int slot);
    bool holdsBlock(const ItemStack& item) const;
    static std::array<int32_t, 3> placedCell(const BlockHit& hit);
    bool replaceableAt(const std::array<int32_t, 3>& cell);
    bool placeableAt(const std::array<int32_t, 3>& cell);
    static bool usableBlock(std::string_view name);
    bool trySwing(std::string_view source);
    uint64_t lastSwingTick = 0;
    bool swingStarted = false;

    /**
     * What a click on a block does on this side, which decides whether the
     * arm swings and what the transaction predicts.
     */
    enum class BlockUse {
        Nothing,
        Interact,
        Place,
    };
    BlockUse localUse(const BlockHit& block, const ItemStack& item);
    void useOnBlock(const BlockHit& block, bool repeat, BlockUse outcome);
    bool useSelectionVerified();
    bool withinPickRange(const BlockHit& hit) const;
    std::optional<BlockHit> useTarget();
    std::optional<BlockHit> bridgeHit();
    void recordUse(bool repeat, double due, BlockUse outcome);
    static double repeatInterval(bool sneaking, bool slow, double speed, bool survival);
    bool unselectable(uint32_t value) const;
    void pickBlock(bool withData);
    void tickHeldUse();
    bool startItemUse(int32_t slot, const ItemStack& item);
    void tickItemUse(PlayerAuthInputPacket& packet);
    void stopItemUse();
    uint32_t blockAt(int32_t x, int32_t y, int32_t z, uint32_t layer = 0);
    std::vector<world::CollisionBox> shapeBoxes(uint32_t value, int32_t x, int32_t y, int32_t z);
    world::CollisionBox selectionBox(uint32_t value, int32_t x, int32_t y, int32_t z);
    void tickBreaking(PlayerAuthInputPacket& packet, const MotionTick& tick);
    void destroyPredicted(PlayerAuthInputPacket& packet, int32_t face, const std::array<double, 3>& point);
    void emitBurst(ParticleBurst::Kind kind, const std::array<int32_t, 3>& cell, uint32_t value, int32_t face);
    void handleBreakingEvent(const LevelEventPacket& event);
    void answerPredictedBreak(const std::array<int32_t, 3>& cell, uint32_t value);
    bool locallyBroken(const std::array<int32_t, 3>& cell) const;
    void tickCracks();
    void tickChestLids();
    void markChestLid(const std::array<int32_t, 3>& cell, bool moving);
    void publishBreaking();
    double boomFraction();
    double boomFraction(const std::array<double, 3>& origin, const std::array<double, 3>& delta);
    uint8_t mediumAt(const std::array<double, 3>& position);

    std::thread worker;
    std::atomic<bool> cancelled { false };
    mutable std::mutex mutex;
    SessionSnapshot current;
    std::shared_ptr<const SessionSnapshot> publishedSnapshot;
    std::vector<std::shared_ptr<const SessionSnapshot>> retiredSnapshots;
    std::thread::id snapshotProducerThread;
    std::unique_ptr<BedrockConnection> connection;
    world::WorldStream world;
    std::shared_ptr<const world::BlockAssets> assets;
    std::string assetsKey;
    void pollGlobalPacks();
    void configurePaletteResolver();
    struct ResourceReload {
        std::shared_ptr<const world::BlockAssets> assets;
        std::vector<std::shared_ptr<const world::PackFiles>> packs;
        std::vector<MeshUpdate> meshes;
        uint64_t revision = 0, generation = 0;
        int32_t dimension = 0;
        std::string error;
    };
    std::future<ResourceReload> resourceReloadJob;
    std::shared_ptr<std::atomic_bool> resourceReloadCancelled;
    std::vector<std::shared_ptr<const world::PackFiles>> requestedGlobalPacks, sessionServerPacks;
    std::vector<world::CustomBlock> sessionCustomBlocks;
    uint64_t requestedGlobalRevision = 0, appliedGlobalRevision = 0, resourceGeneration = 0;
    std::map<std::string, std::shared_ptr<const world::PackFiles>> packCache;
    std::optional<std::string> transferTarget;
    bool hashedNetworkIds = false;
    world::IdMapping ids;
    std::unique_ptr<world::MeshScheduler> mesher;
    std::map<world::SubChunkKey, uint64_t> meshGenerations;
    std::map<world::SubChunkKey, std::shared_ptr<const world::ChunkMesh>> meshes;
    size_t meshQuads = 0;
    std::deque<MeshUpdate> pendingUpdates;
    std::vector<std::shared_ptr<const Packet>> pendingCameraEvents;
    uint64_t joins = 0;
    uint64_t localRuntimeId = 0;
    int64_t localUniqueId = 0;
    std::string localUuid;
    std::mutex viewInputMutex;
    std::array<double, 3> requestedLookOrigin {};
    std::array<double, 3> requestedRenderedCamera {};
    std::array<double, 3> renderedCamera {};
    std::array<float, 3> requestedLookDirection { 0.0f, 0.0f, -1.0f };
    std::array<double, 3> requestedBoomOrigin {};
    std::array<double, 3> requestedBoomDelta {};
    std::array<double, 3> requestedServerBoomOrigin {}, requestedServerBoomDelta {};
    std::array<double, 3> lookOrigin {};
    std::array<float, 3> lookDirection { 0.0f, 0.0f, -1.0f };
    std::array<double, 3> boomOrigin {};
    std::array<double, 3> boomDelta {};
    std::array<double, 3> serverBoomOrigin {}, serverBoomDelta {};
    std::atomic<int> packDecision { 0 };
    std::atomic<int> requestedRadius { 16 };
    int sentRadius = 0;
    bool spawnInitialized = false;
    std::map<uint64_t, ActorView> actors;
    std::map<int64_t, uint64_t> runtimeByUnique;
    int64_t ridingUnique = 0;
    std::map<uint64_t, std::string> uuidByRuntime;
    std::map<std::string, std::pair<uint32_t, bool>> skinByUuid;
    std::map<std::string, SerializedSkin> knownSkins;
    std::map<std::string, uint64_t> uploadedSkinPrints;
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
    PacketJournal journal;
    std::shared_ptr<PacketHook> packetHook;
    std::vector<std::string> rawOutgoing;
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
    bool dimensionSpawnReceived = false;
    std::atomic<bool> useRequested { false };
    std::atomic<bool> attackRequested { false };
    std::atomic<bool> attackHeld { false };
    std::atomic<bool> useHeld { false };
    std::atomic<int> pickRequested { 0 };
    std::array<double, 3> tickEye {};
    std::array<float, 3> tickDirection { 0.0f, 0.0f, -1.0f };
    bool tickSneaking = false;
    double tickSpeed = 0.0;
    std::optional<double> lastUseTime;
    bool slowRepeat = false;
    uint64_t lastUseAttemptTick = 0;
    uint64_t lastItemRepeatTick = 0;

    /**
     * The item held in use: the hotbar slot it sits in, what it is, and
     * whether the tick that announces the start has gone out yet.
     */
    struct ItemInUse {
        int32_t slot = 0;
        std::string identifier;
        bool announced = false;
    };
    std::optional<ItemInUse> itemInUse;

    struct LocalBreak {
        bool active = false;
        std::array<int32_t, 3> cell {};
        int32_t face = 0;
        uint32_t value = 0;
        float progress = 0.0f;
        uint32_t ticks = 0;
    };

    struct PredictedBreak {
        std::array<int32_t, 3> cell {};
        uint32_t value = 0;
        int32_t face = 0;
        double time = 0.0;
    };

    struct RemoteCrack {
        uint32_t value = 0;
        float progress = 0.0f;
        float speed = 0.0f;
    };

    LocalBreak breaking;
    bool attackOnEntity = false;
    bool attackHeldBefore = false;
    bool breakSwitched = false;
    int32_t destroyDelay = 0;
    std::map<std::array<int32_t, 3>, RemoteCrack> remoteCracks;
    struct ChestLidState {
        bool open = false;
        float openness = 0.0f;
    };
    std::map<std::array<int32_t, 3>, ChestLidState> chestLidStates;
    std::map<std::array<int32_t, 3>, double> recentBreaks;
    std::vector<PredictedBreak> predictedBreaks;
    std::vector<ParticleBurst> pendingBursts;
    std::vector<world::ParticleSpawn> pendingParticles;
    std::optional<std::pair<std::set<std::string>, bool>> pendingHidden;
    std::set<std::string> hiddenNames;
    bool hiddenInverted = false;
    std::unordered_map<uint32_t, bool> hiddenChecked;
    std::vector<uint64_t> pendingAttacks;
    /**
     * One sent movement tick: its number, the input it ran with, the server
     * knockback it took and the motion state it ended in, kept so a server
     * correction of that tick can be replayed forward to the present.
     */
    struct SentMotionTick {
        uint64_t tick = 0;
        MotionInput input;
        PlayerMotion after;
        bool knockedBack = false;
        MotionVector knockback;
    };
    PlayerMotion motion;
    std::deque<SentMotionTick> motionHistory;

    /**
     * A velocity the server set on the player, kept for the input tick it
     * applies at so a replay over that tick sets it again.
     */
    struct ServerMotion {
        uint64_t tick = 0;
        MotionVector velocity;
    };
    std::deque<ServerMotion> serverMotions;
    bool enderChestOpen = false;
    std::mutex motionInputMutex;
    MotionInput motionInput;
    MotionInput lastMotionInput;
    bool motionStarted = false;
    bool teleportHandled = false;
    std::atomic<bool> missedSwing { false };
    std::atomic<int32_t> pendingRiptide { 0 };
    uint64_t clientTick = 0;
    double nextMotionTick = 0.0;
    int32_t motionDimension = 0;
    std::vector<SoundRequest> pendingSounds;
    std::vector<ChatMessage> pendingChat;
    std::optional<ActionbarText> pendingActionbar;
    std::vector<TitleRequest> pendingTitles;
    std::vector<ToastRequest> pendingToasts;
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
