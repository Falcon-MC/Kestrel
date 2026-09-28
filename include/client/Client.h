#pragma once

#include "audio/SoundEngine.h"
#include "client/Account.h"
#include "client/BlockParticles.h"
#include "client/Camera.h"
#include "client/FeaturedServers.h"
#include "client/Profiler.h"
#include "client/ServerPinger.h"
#include "client/Session.h"
#include "world/EntityAnimation.h"
#include "world/Mesher.h"
#include "menu/Menu.h"
#include "menu/ServerStore.h"
#include "platform/System.h"
#include "ui/Context.h"
#include "ui/DrawList.h"
#include "ui/Font.h"
#include "ui/GameAssets.h"
#include "ui/JsonUi.h"
#include "ui/Skin.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>

namespace kestrel {

class Window;
class Renderer;

/**
 * How one entity glides between its network samples: the displayed position
 * and rotation (yaw, head yaw, pitch) move from where they were when the last
 * sample arrived toward that sample over the time samples usually take.
 */
struct ActorMotion {
    uint64_t moves = 0;
    uint64_t teleports = 0;
    std::array<double, 3> from {};
    std::array<double, 3> to {};
    std::array<double, 3> shown {};
    std::array<float, 3> turnFrom {};
    std::array<float, 3> turnTo {};
    std::array<float, 3> turnShown {};
    double start = 0.0;
    double duration = 0.0;
    double lastSample = 0.0;
};

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
    Client();
    ~Client();

    int run();

private:
    void syncAccount();
    void syncSession();
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
    void buildActorQuads(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out, std::vector<world::ModelQuadGpu>& blended);
    void interpolateActors(double now);
    ActorView localActorView(float deltaSeconds);
    void appendFirstPerson(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out);
    uint32_t heldItemLayer() const;
    const world::EntityModel* localPlayerModel(const world::EntityRig*& rig, uint32_t& skinLayer) const;
    float swingProgress();
    void startSwing(double now);
    void appendHeldItem(const HudItem& held, const std::function<std::array<float, 3>(const std::array<float, 3>&, bool)>& place, std::vector<world::ModelQuadGpu>& out);
    void appendThirdPersonItem(const world::EntityRig& rig, const std::vector<world::BoneMatrix>& matrices, const std::function<std::array<float, 3>(const std::array<float, 3>&)>& toWorld, std::vector<world::ModelQuadGpu>& out);
    void appendArmor(const std::array<std::string, 4>& armor, const world::EntityRig& rig, const std::vector<world::BoneMatrix>& matrices, const std::function<std::array<float, 3>(const std::array<float, 3>&)>& toWorld, uint32_t shadeFlags, std::vector<world::ModelQuadGpu>& out);
    bool paperDollVisible();
    void appendPaperDoll(const ActorView& self, const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out);
    void appendBlockOverlays(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out);
    void appendChestLids(const std::array<int32_t, 3>& origin, double deltaSeconds, std::vector<world::ModelQuadGpu>& out);
    menu::HudSlot inventoryIcon(const HudItem& item);
    menu::HudView buildHudView();
    std::vector<menu::NameTag> buildNameTags() const;
    void countFrame(std::chrono::steady_clock::time_point now);
    menu::DebugView buildDebugView(const SessionSnapshot& snapshot);
    void handleHotbarInput();
    bool terrainReady(const SessionSnapshot& snapshot);
    float guiScale() const;
    void uploadAtlas();
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
    Session session;
    std::filesystem::path settingsFile;
    float savedScale = 1.0f;
    KeyBindings savedBindings;
    int savedRenderDistance = menu::DefaultRenderDistance;
    int savedMaxFps = menu::DefaultMaxFps;
    int savedFov = menu::DefaultFov;
    bool savedPaperDollHidden = false;
    bool savedFullscreen = false;
    std::string savedLanguage = "en_US";
    std::vector<uint8_t> atlasPixels;
    uint64_t avatarRevision = 0;
    uint64_t profileRevision = 0;
    menu::ProfileInfo profileInfo;
    std::vector<std::string> profileSprites;
    std::shared_ptr<const std::vector<uint8_t>> shownTitle;
    FreeCamera camera;
    uint64_t seenJoin = 0;
    uint64_t seenTeleport = 0;
    PlayerView playerView;
    std::unique_ptr<audio::SoundEngine> soundEngine;
    std::shared_ptr<world::PackSource> vanillaSounds;
    std::shared_ptr<world::PackSource> musicSounds;
    std::vector<std::shared_ptr<const world::PackFiles>> soundPacks;
    bool soundLibraryBuilt = false;
    uint64_t heardClicks = 0;
    std::string musicSituation;
    double nextMusicAt = 0.0;
    bool musicWasPlaying = false;
    double nextRainSoundAt = 0.0;
    std::array<int, menu::VolumeChannelCount> savedVolumes {};
    bool worldShown = false;
    std::shared_ptr<const world::BlockAssets> blockAssets;
    SessionSnapshot timeState;
    double startSeconds = 0.0;
    std::unordered_map<uint64_t, std::array<int32_t, 3>> opaqueChunks;
    std::optional<uint64_t> readinessFrame;
    bool terrainReleased = false;
    std::vector<ActorView> actorViews;
    std::map<uint32_t, std::vector<uint8_t>> skinPixels;
    std::map<uint32_t, std::shared_ptr<const world::EntityRig>> skinRigs;
    std::unordered_map<uint64_t, world::EntityAnimator> animators;
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
    uint64_t titleSerial = 0;
    std::optional<BlockSelection> selectionView;
    std::vector<BlockCrack> crackViews;
    std::vector<ChestLidView> chestLidViews;
    std::map<std::array<int32_t, 3>, float> chestLidShown;
    BlockParticles blockParticles;
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
    uint64_t actorFrame = 0;
    world::EntityAnimator handAnimator;
    world::EntityAnimator handRestAnimator;
    world::EntityAnimator paperDollAnimator;
    double paperDollShownAt = 0.0;
    std::unordered_map<uint64_t, float> swimAmounts;
    double lastActorTime = 0.0;
    static constexpr int PerspectiveFirst = 0;
    static constexpr int PerspectiveBack = 1;
    static constexpr int PerspectiveFront = 2;
    static constexpr double ThirdPersonRadius = 4.0;
    static constexpr uint64_t LocalActorId = ~0ull;
    int perspective = PerspectiveFirst;
    std::array<double, 3> eyePosition {};
    double boomFraction = 0.0;
    float localBodyYaw = 0.0f;
    uint32_t localSkinSlot = NoSkin;
    bool localSlim = false;
    double swingStart = -1.0;
    std::string lastHeldIdentity;
    double handUpdatedAt = 0.0;
    float handEquip = 0.0f;
    HudItem handItem;
    std::string heldItemKey;
    struct HeldItemFace {
        std::array<std::array<float, 3>, 4> corners;
        std::array<std::array<uint16_t, 2>, 4> uvs;
        uint32_t material;
        uint32_t shade;
    };
    std::vector<HeldItemFace> heldItemMesh;
    bool heldItemBlock = false;
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
    std::map<std::pair<const void*, const void*>, std::vector<int32_t>> partMatches;
    std::map<std::pair<const void*, const void*>, std::vector<int32_t>> armorBoneMatches;
    std::chrono::steady_clock::time_point fpsWindowStart;
    int framesCounted = 0;
    int framesPerSecond = 0;
    platform::MemoryUsage memory;
    std::string processor = platform::processorName();
};

}
