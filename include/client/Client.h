#pragma once

#include "client/Account.h"
#include "client/Camera.h"
#include "client/Session.h"
#include "menu/Menu.h"
#include "menu/ServerStore.h"
#include "ui/Context.h"
#include "ui/DrawList.h"
#include "ui/Font.h"

#include <filesystem>
#include <memory>

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
    std::vector<uint8_t> atlasPixels;
    std::vector<uint8_t> avatarPixels;
    uint64_t avatarRevision = 0;
    uint64_t uploadedAvatarRevision = 0;
    FreeCamera camera;
    uint64_t seenJoin = 0;
    uint64_t seenTeleport = 0;
    bool worldShown = false;
    bool blockTexturesUploaded = false;
    std::shared_ptr<const world::BlockAssets> blockAssets;
    SessionSnapshot timeState;
    double startSeconds = 0.0;
};

}
