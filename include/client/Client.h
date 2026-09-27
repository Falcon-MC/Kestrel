#pragma once

#include "client/Account.h"
#include "client/Camera.h"
#include "client/Session.h"
#include "menu/Menu.h"
#include "menu/ServerStore.h"
#include "ui/Context.h"
#include "ui/DrawList.h"
#include "ui/Font.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>

namespace kestrel {

class Window;
class Renderer;

class Client {
public:
    Client();
    ~Client();

    int run();

private:
    void syncAccount();
    void syncSession();
    void applyMeshUpdates();
    size_t visibleTerrain() const;
    bool terrainReady(const SessionSnapshot& snapshot);
    void uploadAtlas();
    void loadWorlds();
    void loadSettings();
    void saveSettings();

    std::unique_ptr<Window> window;
    std::unique_ptr<Renderer> renderer;
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
    std::vector<uint8_t> atlasPixels;
    std::vector<uint8_t> avatarPixels;
    uint64_t avatarRevision = 0;
    uint64_t uploadedAvatarRevision = 0;
    std::shared_ptr<const std::vector<uint8_t>> shownTitle;
    std::vector<uint8_t> titlePixels;
    uint64_t titleRevision = 0;
    uint64_t uploadedTitleRevision = 0;
    FreeCamera camera;
    uint64_t seenJoin = 0;
    uint64_t seenTeleport = 0;
    bool worldShown = false;
    std::shared_ptr<const world::BlockAssets> blockAssets;
    SessionSnapshot timeState;
    double startSeconds = 0.0;
    std::unordered_map<uint64_t, std::array<int32_t, 3>> opaqueChunks;
    std::optional<uint64_t> readinessFrame;
    bool terrainReleased = false;
};

}
