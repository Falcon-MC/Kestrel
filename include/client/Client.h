#pragma once

#include "agent/AgentServer.h"
#include "agent/EventLog.h"
#include "agent/SessionControl.h"
#include "audio/SoundEngine.h"
#include "audio/SoundLibrary.h"

#include <future>
#include "client/Account.h"
#include "client/ActorMotion.h"
#include "client/BlockParticles.h"
#include "client/Camera.h"
#include "client/ServerCamera.h"
#include "client/AimAssist.h"
#include "client/DressingRoom.h"
#include "client/FeaturedServers.h"
#include "client/FirstPersonMotion.h"
#include "client/LaunchOptions.h"
#include "client/ParticleRenderer.h"
#include "client/Profiler.h"
#include "client/ServerPinger.h"
#include "client/Session.h"
#include "client/HandEquip.h"
#include "client/HotbarSelection.h"
#include "client/Social.h"
#include "world/EntityAnimation.h"
#include "world/BookAnimation.h"
#include "world/GlobalResources.h"
#include "render/Renderer.h"
#include "world/Mesher.h"
#include "menu/Menu.h"
#include "menu/ServerStore.h"
#include "modding/ModManager.h"
#include "platform/System.h"
#include "ui/Context.h"
#include "ui/DrawList.h"
#include "ui/Font.h"
#include "ui/GameAssets.h"
#include "ui/JsonUi.h"
#include "ui/Skin.h"

#include <array>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace kestrel {

class Window;
class Renderer;

/**
 * Server text the HUD fades out: when it arrived and how long it holds before
 * fading, and for popups whether a jukebox sent it.
 */
struct HudMessage {
    std::string text;
    double shown = -1.0;
    float hold = 0.0f;
    bool jukebox = false;
};

class Client {
public:
    explicit Client(LaunchOptions options = {});
    ~Client();

    int run();

private:
    /**
     * One frame of synthetic input for the agent: what to change in the
     * window's input, and the request to answer once it has been applied.
     */
    struct AgentInputStep {
        std::function<void(InputState&)> apply;
        std::shared_ptr<agent::Request> done;
    };

    struct AgentCapture {
        std::shared_ptr<agent::Request> request;
        uint32_t maxWidth = 0;
        std::optional<ui::Rect> crop;
        int framesLeft = 0;
    };

    void startMods();
    void startAgent();
    float serverFovDegrees(float settingDegrees, float deltaSeconds);
    void serveAgent();
    bool handleAgentRequest(agent::Request& request);
    void finishAgentCaptures();
    void queueAgentInput(std::vector<std::function<void(InputState&)>> steps, agent::Request& request);
    std::string agentState();
    std::string agentWidgetList(const agent::Request& request) const;
    std::string agentSettings() const;
    bool applyAgentSettings(const agent::Request& request, std::string& error);
    std::string agentServers();
    std::string agentForms();
    std::string agentDebug();
    const std::string& offlineName() const;

    void syncAccount();
    void syncSocial();
    void syncDressingRoom();
    void syncSession();
    void prepareSessionRender();
    void updateAimAssist();
    void drawAimAssist(ui::Context& context, float scale);
    void syncFeatured();
    void collectFeaturedImages();
    void syncForms();
    std::string formImage(const menu::FormImage& image);
    void syncChat();
    void showHudText(const ChatMessage& message, std::string body);
    void applyTitle(TitleRequest request);
    void applyServerPacks(const std::vector<std::shared_ptr<const world::PackFiles>>& packs);
    void loadPackGlyphs(const std::vector<std::shared_ptr<const world::PackFiles>>& packs);
    void cutGlyphs(size_t index, const ui::Bitmap& sheet);
    void loadJsonUi(const std::vector<std::shared_ptr<const world::PackFiles>>& packs);
    bool loadPackTexture(const std::string& texture);
    ui::UiData sidebarData() const;
    float nightVisionStrength() const;
    void updateAudio(const SessionSnapshot& snapshot);
    void playSoundRequest(const SoundRequest& request);
    void updateMusic(const SessionSnapshot& snapshot);
    void applyMeshUpdates();
    size_t visibleTerrain() const;
    uint8_t lightAt(double x, double y, double z) const;
    uint32_t lightCorners(double x, double y, double z) const;
    void lightQuads(std::vector<world::ModelQuadGpu>& quads, size_t first, uint32_t corners) const;
    void buildActorQuads(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out, std::vector<world::ModelQuadGpu>& blended);
    void interpolateActors(double now);
    ActorView localActorView(float deltaSeconds);
    std::optional<std::array<float, 2>> glideRotation(const ActorView& actor, double now, float alpha) const;
    void appendFirstPerson(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out);
    uint32_t heldItemLayer() const;

    /**
     * Where a skin image sits in the skin layers: the first layer of its grid,
     * counted from the first skin layer, and how it is cut over the grid.
     */
    struct SkinTexture {
        uint32_t layer = 0;
        world::EntityTileGrid grid;
        bool present = false;
    };

    /**
     * An animation sheet of a skin as drawn: its layers, the model it goes on
     * and how it steps through its frames.
     */
    struct SkinAnimationView {
        SkinTexture texture;
        std::shared_ptr<const world::EntityRig> rig;
        SkinAnimationKind kind = SkinAnimationKind::Face;
        uint32_t frames = 1;
        bool blinking = false;
    };

    /**
     * The textures of the skin in one skin slot: the skin, its cape and its
     * animation sheets.
     */
    struct SkinView {
        SkinTexture base;
        SkinTexture cape;
        std::vector<SkinAnimationView> animations;
    };

    /**
     * Copies an image texel for texel onto a grid of free skin layers, at most
     * maxTiles layers across and down, owned by the skin slot; an image that
     * finds no room for its grid is squeezed into a single layer.
     */
    SkinTexture placeSkinTexture(uint32_t slot, const SkinImage& image, uint32_t maxTiles);
    void releaseSkinLayers(uint32_t slot);
    const SkinView* skinViewOf(uint32_t slot) const;

    /**
     * How the texture starting at an entity layer is cut over the layers,
     * whether it is a pack texture or a skin image.
     */
    world::EntityTileGrid tileGridOf(uint32_t layer) const;

    /**
     * Pushes one posed model quad, corners in 1/256 block around the draw
     * origin and UVs over the whole texture, cut over the layers its texture
     * is spread on.
     */
    void appendEntityQuad(const std::array<std::array<float, 3>, 4>& corners, const std::array<std::array<float, 2>, 4>& uvs, uint32_t layer, uint32_t shadeWord, std::vector<world::ModelQuadGpu>& out) const;
    const world::EntityModel* localPlayerModel(const world::EntityRig*& rig, uint32_t& skinLayer) const;
    float swingProgress();
    float swingProgressSince(double start, double now) const;
    void startSwing(double now);
    void appendHeldItem(const HudItem& held, const std::function<std::array<float, 3>(const std::array<float, 3>&, bool)>& place, std::vector<world::ModelQuadGpu>& out, bool mirroredSprite = false);
    struct HeldAttachable {
        std::string identifier;
        world::EntityAnimator animator;
        std::vector<world::EntityBone> bones;
    };
    void appendThirdPersonItem(const HudItem& held, double itemUseTicks, HeldAttachable& attachable, const world::EntityRig& rig, const std::vector<world::BoneMatrix>& matrices, const std::function<std::array<float, 3>(const std::array<float, 3>&)>& toWorld, std::vector<world::ModelQuadGpu>& out, bool leftHand = false);
    bool appendAttachable(const HudItem& held, double itemUseTicks, const world::EntityRig& holder, const std::vector<world::BoneMatrix>& holderMatrices, bool firstPerson, HeldAttachable& state, const std::function<std::array<float, 3>(const std::array<float, 3>&)>& toWorld, std::vector<world::ModelQuadGpu>& out, bool leftHand = false);
    double localItemUseTicks() const;
    double actorItemUseTicks(const ActorView& actor, double now);
    void appendArmor(const std::array<std::string, 4>& armor, const world::EntityRig& rig, const std::vector<world::BoneMatrix>& matrices, const std::function<std::array<float, 3>(const std::array<float, 3>&)>& toWorld, uint32_t shadeFlags, std::vector<world::ModelQuadGpu>& out, const std::vector<uint8_t>* shownBones = nullptr, const std::array<HudItem, 4>* items = nullptr);
    bool paperDollVisible();
    void appendPaperDoll(const ActorView& self, const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out);
    void appendBlockOverlays(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out);
    void appendChestLids(const std::array<int32_t, 3>& origin, double deltaSeconds, std::vector<world::ModelQuadGpu>& out);
    uint32_t loadParticles(const std::vector<std::shared_ptr<const world::PackFiles>>& packs, uint32_t firstLayer, std::vector<uint8_t>& entityPixels);
    void takeSessionParticles(const SessionSnapshot& snapshot);
    void clearParticles();
    void refreshParticleBlocks(double now);
    void appendParticles(double deltaSeconds, const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& opaque, std::vector<world::ModelQuadGpu>& blended);
    menu::HudSlot inventoryIcon(const HudItem& item);
    void drawInventoryEntity(ui::Context& ui, const std::string& identifier, const ui::Rect& rect, float alpha);
    menu::HudView buildHudView();
    uint32_t modHiddenElements() const;
    std::vector<menu::NameTag> buildNameTags() const;
    void countFrame(std::chrono::steady_clock::time_point now);
    menu::DebugView buildDebugView(const SessionSnapshot& snapshot);
    void handleHotbarInput();

    /**
     * An emote playing on the local player: the clip on its body, when it
     * started and how long a clip that plays once lasts.
     */
    struct ActiveEmote {
        std::string id;
        std::string clip;
        double started = 0.0;
        double length = 0.0;
        bool once = false;
        int restartFrames = 0;
    };
    void updateEmotes(double now);
    void startEmote(const std::string& id, double now);
    menu::ModelPose emotePose(const std::string& clipName);
    std::optional<ActiveEmote> activeEmote;
    world::AnimationLibrary emoteAnimations;
    uint64_t emoteRevision = ~uint64_t(0);
    void driveGamepad();
    std::array<Key, PadButtonCount> padKeys {};
    bool padAttack = false;
    bool padUse = false;
    bool padClick = false;
    bool padCursorShown = false;
    double padClock = 0.0;
    float padScroll = 0.0f;
    std::array<float, 2> padMove {};
    bool sneakFromPad = false;
    bool sneakToggled = false;
    bool sneakActive = false;
    void loadShownTips();
    void markTipShown(const std::string& id);
    void showGameTip(const std::string& id, const std::string& text, const std::string& animation, double now);
    void updateGameTips();
    bool terrainReady(const SessionSnapshot& snapshot);
    float guiScale() const;
    void uploadAtlas(bool fontChanged);
    void loadSettings();
    void saveSettings();

    std::unique_ptr<Window> window;
    std::unique_ptr<Renderer> renderer;
    ui::GameAssets assets;
    ui::Skin skin { assets };
    ui::Font font;
    ui::DrawList drawList;
    ui::WidgetState widgets;
    menu::ServerStore store;
    menu::Menu menu;
    Account account;
    Social social;
    std::string socialAccount;
    uint64_t socialRevision = 0;
    uint64_t realmsChangedSeen = 0;
    std::vector<std::string> socialSprites;
    Session session;
    std::filesystem::path settingsFile;
    float savedScale = 1.0f;
    KeyBindings savedBindings;
    int savedRenderDistance = menu::DefaultRenderDistance;
    int savedMaxFps = menu::DefaultMaxFps;
    int savedFov = menu::DefaultFov;
    bool savedPaperDollHidden = false;
    bool savedVsync = false;
    bool savedGameplayFov = true;
    float savedSafeArea = menu::MaxSafeArea;
    int savedBrightness = menu::DefaultBrightness;
    int savedGlintStrength = 100;
    int savedGlintSpeed = 100;
    std::map<std::string, int> savedExtraOptions;
    std::string savedOfflineName;
    menu::ChatSettings savedChat;
    bool savedFullscreen = false;
    std::string savedLanguage = "en_US";
    std::vector<uint8_t> atlasPixels;
    uint64_t avatarRevision = 0;
    uint64_t profileRevision = 0;
    menu::ProfileInfo profileInfo;
    std::vector<std::string> profileSprites;
    DressingRoomCatalog dressingCatalog;
    std::map<std::string, uint64_t> dressingRevisions;
    std::map<std::string, std::vector<std::string>> dressingSprites;
    std::shared_ptr<const std::vector<uint8_t>> shownTitle;
    FreeCamera camera;
    uint64_t seenJoin = 0;
    uint64_t seenTeleport = 0;
    std::shared_ptr<const SessionSnapshot> seenSessionSnapshot;
    std::shared_ptr<const SessionSnapshot> renderedSessionSnapshot;
    bool resetChunkMeshes = false;
    PlayerView playerView;
    std::unique_ptr<audio::SoundEngine> soundEngine;
    std::future<std::unique_ptr<audio::SoundEngine>> pendingSoundEngine;
    std::future<std::shared_ptr<audio::SoundLibrary>> pendingSoundLibrary;
    std::shared_ptr<world::PackSource> vanillaSounds;
    std::shared_ptr<world::PackSource> musicSounds;
    std::vector<std::shared_ptr<const world::PackFiles>> soundPacks;
    bool soundLibraryBuilt = false;
    uint64_t heardClicks = 0;
    std::string musicSituation;
    double nextMusicAt = 0.0;
    bool musicWasPlaying = false;
    double nextRainSoundAt = 0.0;
    bool underwaterAudio = false;
    float submergedSeconds = 0.0f;
    std::array<int, menu::VolumeChannelCount> savedVolumes {};
    bool worldShown = false;
    std::shared_ptr<const world::BlockAssets> blockAssets;
    SessionSnapshot timeState;
    world::GlobalResources globalResources;
    uint64_t globalResourcesRevision = 0;
    std::vector<std::shared_ptr<const world::PackFiles>> selectedGlobalPacks;
    std::set<std::string> globalPackIcons;
    void updateGlobalResources();
    void applyMeshUpdate(const MeshUpdate& update);
    double startSeconds = 0.0;
    std::unordered_map<uint64_t, std::array<int32_t, 3>> opaqueChunks;
    std::unordered_map<uint64_t, std::shared_ptr<const world::ChunkMesh>> litChunks;
    std::optional<uint64_t> readinessFrame;
    bool terrainReleased = false;
    std::vector<ActorView> actorViews;
    std::map<uint32_t, std::vector<uint8_t>> skinPixels;
    std::map<uint32_t, std::shared_ptr<const world::EntityRig>> skinRigs;
    std::map<uint32_t, SkinView> skinViews;
    std::vector<uint32_t> skinLayerOwners = std::vector<uint32_t>(world::SkinPoolLayers, 0);
    std::unordered_map<uint32_t, world::EntityTileGrid> skinTileGrids;
    std::unordered_map<uint64_t, world::EntityAnimator> animators;

    /**
     * An entity's bones as posed on the last two game ticks, drawn blended by
     * how far the frame is into the current tick.
     */
    struct ActorPose {
        const world::EntityRig* source = nullptr;
        std::vector<world::EntityBone> bones;
        std::vector<world::BoneMatrix> previous;
        std::vector<world::BoneMatrix> current;
        std::vector<world::BoneMatrix> interpolated;
        std::vector<std::array<float, 24>> gpuTransforms;
        std::vector<uint8_t> gpuTransformValid;
        double tick = -1.0;
    };
    std::unordered_map<uint64_t, ActorPose> actorPoses;
    std::unordered_map<uint64_t, ActorMotion> motions;
    HudState hudState;
    std::map<std::string, bool> itemIcons;
    std::vector<std::shared_ptr<const world::PackFiles>> artPacks;
    std::vector<std::string> packSprites;
    bool jsonUiLoaded = false;
    SidebarView sidebarView;
    HudMessage popupMessage;
    HudMessage tipMessage;
    HudMessage actionbarMessage;
    menu::HudTitle titleView;
    std::vector<menu::HudTitle> titleUiUpdates;
    uint64_t titleSerial = 0;

    /**
     * The game tip on screen: which tip it is, what the HUD shows, when it
     * came up and when the action it teaches was done, 0 until then.
     */
    struct GameTipState {
        std::string id;
        menu::HudGameTip view;
        double shownAt = 0.0;
        double doneAt = 0.0;
    };
    GameTipState gameTip;
    double gameTipEndedAt = 0.0;
    std::set<std::string> shownTips;
    bool shownTipsLoaded = false;
    uint64_t gameTipSerial = 0;
    std::string ridingView;
    std::string targetBlockName;
    float tipLookTravel = 0.0f;
    std::optional<BlockSelection> selectionView;
    std::vector<BlockCrack> crackViews;
    std::vector<ChestLidView> chestLidViews;
    std::vector<FrameItemView> frameItemViews;
    std::vector<ShelfItemView> shelfItemViews;
    std::vector<EnchantingBookView> enchantingBookViews;
    std::vector<BeaconBeamView> beaconBeamViews;
    std::vector<ConduitView> conduitViews;
    std::vector<BannerView> bannerViews;
    std::vector<SignTextView> signTextViews;
    std::vector<SpawnerView> spawnerViews;
    std::vector<VaultItemView> vaultItemViews;
    std::vector<PotView> potViews;
    std::vector<PistonView> pistonViews;
    std::vector<MovingBlockView> movingBlockViews;
    struct SpawnerPose {
        uint64_t id = 0;
        float spin = 0.0f;
        double time = 0.0;
        std::string identifier;
    };
    std::map<std::array<int32_t, 3>, SpawnerPose> spawnerPoses;
    std::map<std::array<int32_t, 3>, world::BookAnimation> enchantingBookShown;
    std::vector<uint8_t> mapIcons;
    uint32_t mapIconsWidth = 0;
    uint32_t mapIconsHeight = 0;
    std::map<std::array<int32_t, 3>, float> chestLidShown;
    BlockParticles blockParticles;
    world::ParticleLibrary particleLibrary;
    world::ParticleSystem particleSystem { particleLibrary };
    ParticleRenderer particleRenderer;
    ClientParticleEmitters particleEmitters;
    std::vector<world::ParticleSpawn> particleSpawns;
    std::vector<uint64_t> particleAttacks;
    std::shared_ptr<const NearbyBlocks> nearbyBlocks;
    std::vector<ClientParticleEmitters::BlockState> particleBlocks;
    std::unordered_map<uint32_t, bool> particleBlockKinds;
    std::pair<const void*, const void*> particleBlockKindsKey {};
    double particleBlocksAt = 0.0;
    int particleDimension = 0;
    Profiler profiler;
    ServerPinger pinger;
    std::unique_ptr<FeaturedServers> featured;
    std::vector<FeaturedServer> featuredList;
    std::set<std::string> featuredImages;
    std::map<std::string, ui::Bitmap> featuredShowcases;
    std::set<std::string> shownShowcases;
    std::optional<std::string> featuredFocus;
    bool featuredListed = false;
    bool featuredDirty = false;
    world::EntityAnimator handAnimator;
    FirstPersonMotion firstPersonMotion;
    uint64_t handMotionTickSerial = 0;
    uint64_t handMotionTeleports = 0;
    world::EntityAnimator paperDollAnimator;
    HeldAttachable handAttachable;
    HeldAttachable handOffhandAttachable;
    HeldAttachable bodyAttachable;
    HeldAttachable bodyOffhandAttachable;
    double paperDollShownAt = 0.0;
    std::unordered_map<uint64_t, float> swimAmounts;
    float localSwimAmount = 0.0f;
    std::unordered_map<uint64_t, HeldAttachable> actorAttachables;
    std::unordered_map<uint64_t, HeldAttachable> actorOffhandAttachables;
    std::unordered_map<uint64_t, HeldAttachable> actorElytras;
    // When each other player's using item flag came on, since servers only send the flag.
    std::unordered_map<uint64_t, double> actorItemUseSince;
    double lastActorTime = 0.0;
    static constexpr int PerspectiveFirst = 0;
    static constexpr int PerspectiveBack = 1;
    static constexpr int PerspectiveFront = 2;
    static constexpr double ThirdPersonRadius = 4.0;
    static constexpr uint64_t LocalActorId = ~0ull;
    int perspective = PerspectiveFirst;
    bool cameraDetached = false;
    ServerCamera serverCamera;
    AimAssist aimAssist;
    std::optional<AimAssistTarget> aimTarget;
    std::unordered_map<uint32_t, std::string> aimBlockNames;
    const void* aimBlockAssets = nullptr;
    const void* aimBlockIds = nullptr;
    double serverBoomFraction = 0.0;
    int64_t cameraLocalUnique = 0;
    uint64_t cameraSessionJoin = 0;
    int32_t cameraDimension = 0;
    std::array<double, 3> eyePosition {};
    double boomFraction = 0.0;
    float localBodyYaw = 0.0f;
    double localGlideSince = 0.0;
    // What the server was last told the player faces, which a mod may have turned away from the camera.
    float localLookYaw = 0.0f;
    float localLookPitch = 0.0f;
    // What the mods changed through Visuals, merged once a frame.
    modding::VisualRequest visuals;
    uint32_t localSkinSlot = NoSkin;
    struct ServerFovBlend {
        bool active = false;
        float from = 0.0f;
        float to = 0.0f;
        bool returning = false;
        float elapsed = 0.0f;
        float duration = 0.0f;
        int easeType = 0;
    };
    CameraFovRequest cameraFovRequest;
    uint64_t serverFovSerial = 0;
    ServerFovBlend serverFov;
    bool localSlim = false;
    double swingStart = -1.0;
    HotbarSelection hotbarSelection;
    HandEquip handTransition;
    HandEquip offhandTransition;
    float handEquip = 0.0f;
    float offhandEquip = 0.0f;
    double consumeStarted = 0.0;
    std::string consumeIdentity;
    uint64_t heldItemFrame = 0;
    struct HeldItemFace {
        std::array<std::array<float, 3>, 4> corners;
        std::array<std::array<uint16_t, 2>, 4> uvs;
        uint32_t material;
        uint32_t shade;
    };
    struct HeldItemMesh {
        std::vector<HeldItemFace> faces;
        bool block = false;
        bool map = false;
        uint32_t slot = 0;
        uint64_t used = 0;
    };
    HeldItemMesh* heldMesh(const HudItem& held);
    std::unordered_map<std::string, HeldItemMesh> heldMeshes;
    std::vector<ActorDraw> actorDraws;
    struct ActorGeometry {
        uint64_t id = 0;
        uint64_t used = 0;
        std::vector<world::ModelQuadGpu> quads;
        std::vector<std::array<float, 6>> bounds;
    };
    uint64_t nextActorGeometry = 1;
    std::unordered_map<const world::EntityRig*, ActorGeometry> actorGeometry;
    std::unordered_set<uint64_t> actorPresent;
    std::vector<uint8_t> actorPartVisible;
    std::vector<uint8_t> actorPartOwn;
    std::vector<uint8_t> actorPartHidden;
    std::vector<uint8_t> actorPartState;
    std::vector<size_t> actorPartPath;
    std::vector<HeldItemFace> buildItemMesh(const HudItem& held, uint32_t layer, bool& block);
    struct DroppedItemMesh {
        std::vector<HeldItemFace> faces;
        bool block = false;
        uint32_t iconSlot = 0;
    };
    std::unordered_map<std::string, DroppedItemMesh> droppedMeshes;
    uint64_t localRuntime = 0;
    std::array<std::string, world::DroppedIconSlots> droppedIconKeys {};
    uint32_t nextDroppedIcon = 0;
    const DroppedItemMesh* droppedItemMesh(const HudItem& item);
    void appendDroppedItem(const ActorView& actor, const std::array<int32_t, 3>& origin, double now, std::vector<world::ModelQuadGpu>& out);
    void appendFallingBlock(const ActorView& actor, const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out, std::vector<world::ModelQuadGpu>& blended);
    void appendActorBlock(const ActorView& actor, const world::BlockVisual& visual, const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out, std::vector<world::ModelQuadGpu>& blended);
    uint32_t mapBackgroundLayer() const;
    void loadMapArt(const std::vector<std::shared_ptr<const world::PackFiles>>& packs, uint8_t* backgroundLayer);
    std::vector<uint8_t> composeMap(const MapView& map) const;
    std::string mapMeshKey(const HudItem& item);
    bool buildMapMesh(const HudItem& item, uint32_t layer, std::vector<HeldItemFace>& faces);
    void appendFrameItems(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out);
    void appendShelfItems(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out);
    void appendVaultItems(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out);
    void appendPots(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out);
    void appendPistons(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out, std::vector<world::ModelQuadGpu>& blended);
    void appendBeaconBeams(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out, std::vector<world::ModelQuadGpu>& blended);
    void appendEnchantingBooks(const std::array<int32_t, 3>& origin, double deltaSeconds, std::vector<world::ModelQuadGpu>& out);
    void appendConduits(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out);
    void appendBanners(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out, std::vector<world::ModelQuadGpu>& blended);
    void drawSignTexts(modding::WorldCanvas& painter);
    bool appendFirstPersonMap(const HudItem& held, float attackTime, const std::array<std::array<float, 3>, 3>& axes, const std::array<float, 3>& eyePoint, float handZoom, const std::array<float, 16>& viewMotion, std::vector<world::ModelQuadGpu>& out);
    std::map<std::pair<const void*, const void*>, std::vector<int32_t>> partMatches;
    std::map<std::pair<const void*, const void*>, std::vector<int32_t>> armorBoneMatches;
    std::chrono::steady_clock::time_point fpsWindowStart;
    int framesCounted = 0;
    int framesPerSecond = 0;
    platform::MemoryUsage memory;
    std::string processor = platform::processorName();
    LaunchOptions launch;
    std::optional<menu::ConnectRequest> pendingConnect;
    agent::EventLog agentEvents;
    std::unique_ptr<agent::AgentServer> agentServer;
    std::unique_ptr<agent::SessionControl> agentSession;
    std::deque<AgentInputStep> agentInput;
    std::vector<AgentCapture> agentCaptures;
    std::vector<ui::Widget> agentWidgets;
    bool agentQuit = false;
    // Last, so the mods go before anything they hold on to.
    std::unique_ptr<modding::ModManager> mods;
};

}
