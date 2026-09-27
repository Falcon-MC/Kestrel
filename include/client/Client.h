#pragma once

#include "client/Account.h"
#include "client/Camera.h"
#include "client/Profiler.h"
#include "client/ServerPinger.h"
#include "client/Session.h"
#include "world/EntityAnimation.h"
#include "world/Mesher.h"
#include "menu/Menu.h"
#include "menu/ServerStore.h"
#include "ui/Context.h"
#include "ui/DrawList.h"
#include "ui/Font.h"
#include "ui/GameAssets.h"
#include "ui/Skin.h"

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
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

class Client {
public:
    Client();
    ~Client();

    int run();

private:
    void syncAccount();
    void syncSession();
    float nightVisionStrength() const;
    void applyMeshUpdates();
    size_t visibleTerrain() const;
    std::vector<world::ModelQuadGpu> buildActorQuads(const std::array<int32_t, 3>& origin);
    void interpolateActors(double now);
    menu::HudView buildHudView();
    void handleHotbarInput();
    bool terrainReady(const SessionSnapshot& snapshot);
    float guiScale() const;
    void uploadAtlas();
    void loadWorlds();
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
    std::vector<uint8_t> atlasPixels;
    uint64_t avatarRevision = 0;
    std::shared_ptr<const std::vector<uint8_t>> shownTitle;
    FreeCamera camera;
    uint64_t seenJoin = 0;
    uint64_t seenTeleport = 0;
    PlayerView playerView;
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
    Profiler profiler;
    ServerPinger pinger;
    uint64_t actorFrame = 0;
    std::map<std::pair<const void*, const void*>, std::vector<int32_t>> partMatches;
};

}
