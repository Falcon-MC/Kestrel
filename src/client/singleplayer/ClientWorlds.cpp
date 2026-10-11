#include "client/Client.h"
#include "client/DebugLog.h"
#include "client/LocalServer.h"
#include "client/WorldStore.h"
#include "platform/Paths.h"
#include "platform/Shell.h"
#include "render/Renderer.h"
#include "ui/Image.h"
#include "ui/Localization.h"
#include "util/Text.h"

#include <chrono>
#include <future>

namespace kestrel {

namespace {

constexpr double WorldIconInterval = 30.0;
constexpr int WorldIconCaptureFrames = 45;
constexpr uint32_t WorldIconWidth = 512;
constexpr int WorldIconQuality = 85;
const char* const WorldIconPrefix = "dynamic/world_icon/";

}

void Client::startLocalWorlds()
{
    localServer = std::make_unique<LocalServer>(platform::dataDirectory() / "singleplayer");
    worldStore = std::make_unique<WorldStore>(localServer->worldsDirectory());
    menu.enableLocalWorlds();
    refreshLocalWorlds();
    if (!launch.world) {
        return;
    }
    for (const menu::LocalWorld& world : worldStore->list()) {
        if (world.id == *launch.world || world.settings.name == *launch.world) {
            playLocalWorld(world.id);
            return;
        }
    }
    menu::WorldSettings settings;
    settings.name = *launch.world;
    if (std::optional<std::string> id = worldStore->create(settings)) {
        refreshLocalWorlds();
        playLocalWorld(*id);
    }
}

void Client::refreshLocalWorlds()
{
    for (const std::string& sprite : worldIconSprites) {
        skin.clearDynamic(sprite);
    }
    worldIconSprites.clear();
    std::vector<menu::LocalWorld> worlds = worldStore->list();
    for (menu::LocalWorld& world : worlds) {
        std::optional<std::string> jpeg = worldStore->readIcon(world.id);
        ui::Bitmap icon;
        if (!jpeg || !ui::decodeBitmap(*jpeg, icon)) {
            continue;
        }
        world.icon = WorldIconPrefix + world.id;
        skin.setDynamic(world.icon, ui::shrinkBitmap(icon, WorldIconWidth));
        worldIconSprites.insert(world.icon);
    }
    menu.setLocalWorlds(std::move(worlds));
}

void Client::playLocalWorld(const std::string& id)
{
    std::optional<menu::LocalWorld> world = worldStore->find(id);
    if (!world) {
        return;
    }
    std::optional<std::string> address = localServer->open(id, world->settings);
    if (!address) {
        debugLog("local server: could not start world " + id);
        menu.notify(ui::tr("hbui.CreateNewWorld.editWorldErrorModal.unexpectedError.title", "Something went wrong"));
        return;
    }
    localSessionStarted = false;
    nextWorldIconCapture = secondsNow() + 5.0;
    worldIconFrame = {};
    pendingConnect = menu::ConnectRequest { world->settings.name, *address, true };
}

void Client::handleWorldRequest(const menu::WorldRequest& request)
{
    using Kind = menu::WorldRequest::Kind;
    switch (request.kind) {
    case Kind::Play:
        playLocalWorld(request.id);
        break;
    case Kind::Create:
        if (std::optional<std::string> id = worldStore->create(request.settings)) {
            refreshLocalWorlds();
            playLocalWorld(*id);
        } else {
            menu.notify(ui::tr("hbui.CreateNewWorld.editWorldErrorModal.unexpectedError.title", "Something went wrong"));
        }
        break;
    case Kind::Save:
        if (worldStore->update(request.id, request.settings)) {
            menu.notify(ui::trf("hbui.EditWorld.notificationToasts.saved", "%1$s saved", { request.settings.name }));
        }
        refreshLocalWorlds();
        break;
    case Kind::Delete: {
        std::optional<menu::LocalWorld> world = worldStore->find(request.id);
        if (world && worldStore->remove(request.id)) {
            menu.notify(ui::trf("hbui.EditWorld.notificationToasts.deleted", "%1$s deleted", { world->settings.name }));
        }
        refreshLocalWorlds();
        break;
    }
    case Kind::Duplicate: {
        std::optional<menu::LocalWorld> world = worldStore->find(request.id);
        if (!world) {
            break;
        }
        menu.showWorldProgress(ui::trf("hbui.CreateNewWorld.duplicateWorld.header", "Duplicating \"%1$s\"", { world->settings.name }));
        WorldStore* store = worldStore.get();
        std::string id = request.id;
        std::string name = world->settings.name;
        worldTask = std::async(std::launch::async, [this, store, id, name]() -> std::function<void()> {
            bool copied = store->duplicate(id).has_value();
            return [this, copied, name] {
                if (copied) {
                    menu.notify(ui::trf("hbui.EditWorld.notificationToasts.duplicated", "%1$s duplicated", { name }));
                } else {
                    menu.notify(ui::tr("hbui.CreateNewWorld.duplicateWorldErrorModal.title", "Storage full"));
                }
            };
        });
        break;
    }
    case Kind::Export: {
        std::optional<menu::LocalWorld> world = worldStore->find(request.id);
        if (!world) {
            menu.notify(ui::tr("hbui.ExportWorld.error.incorrectWorldID.title", "You can not export a world that does not exist"));
            break;
        }
        std::string path = platform::pickWorldSaveFile(world->settings.name + ".mcworld");
        if (path.empty()) {
            break;
        }
        menu.showWorldProgress(ui::tr("hbui.CreateNewWorld.general.fileManagementExportWorldLabel", "Export world"));
        WorldStore* store = worldStore.get();
        std::string id = request.id;
        worldTask = std::async(std::launch::async, [this, store, id, path]() -> std::function<void()> {
            std::string error;
            bool exported = store->exportWorld(id, util::utf8Path(path), error);
            return [this, exported, error] {
                if (exported) {
                    menu.notify(ui::tr("hbui.EditWorld.notificationToasts.exportedWorld", "World exported successfully"));
                    return;
                }
                debugLog("world export failed: " + error);
                menu.notify(ui::tr("hbui.ExportWorld.error.unknownError.title", "Something went wrong. Please try again later."));
            };
        });
        break;
    }
    case Kind::Import: {
        std::string path = platform::pickWorldFile();
        if (path.empty()) {
            break;
        }
        menu.showWorldProgress(ui::tr("hbui.PlayScreen.importWorldProgressModal.title", "Importing world"));
        WorldStore* store = worldStore.get();
        worldTask = std::async(std::launch::async, [this, store, path]() -> std::function<void()> {
            std::string error;
            bool imported = store->importWorld(util::utf8Path(path), error).has_value();
            return [this, imported, error] {
                if (imported) {
                    return;
                }
                debugLog("world import failed: " + error);
                menu.notify(ui::tr("hbui.CreateNewWorld.editWorldErrorModal.unexpectedError.title", "Something went wrong"));
            };
        });
        break;
    }
    }
}

/**
 * Runs once a frame: carries out what the Worlds tab asked for, freezes the
 * world while the pause screen is up, and once the player leaves a world
 * saves it in the background behind the saving screen, then writes the
 * picture taken while they played as the world's icon.
 */
void Client::updateLocalWorlds(const SessionSnapshot& snapshot)
{
    if (worldTask.valid()) {
        menu.takeWorldRequests();
        if (worldTask.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        std::function<void()> report = worldTask.get();
        menu.hideWorldProgress();
        refreshLocalWorlds();
        report();
        return;
    }
    bool closing = localServer->closing();
    if (!closing && savingLocalWorld) {
        savingLocalWorld = false;
        menu.hideWorldProgress();
        refreshLocalWorlds();
    }
    bool open = !localServer->world().empty();
    for (const menu::WorldRequest& request : menu.takeWorldRequests()) {
        if (!closing && !open && !worldTask.valid()) {
            handleWorldRequest(request);
        }
    }
    if (closing || !open) {
        return;
    }
    bool active = snapshot.state == SessionState::Resolving || snapshot.state == SessionState::Connecting || snapshot.state == SessionState::Joined;
    if (active && localServer->running()) {
        localSessionStarted = true;
        localServer->setPaused(menu.pauseShown());
        captureWorldIcon(snapshot);
        return;
    }
    if (!localSessionStarted && localServer->running()) {
        return;
    }
    localSessionStarted = false;
    std::string world = localServer->world();
    if (!worldIconFrame.rgba.empty()) {
        std::string jpeg = ui::encodeJpeg(worldIconFrame.rgba, worldIconFrame.width, worldIconFrame.height, WorldIconQuality);
        if (!jpeg.empty()) {
            worldStore->saveIcon(world, jpeg);
        }
        worldIconFrame = {};
    }
    worldIconCaptureFrames = 0;
    savingLocalWorld = true;
    menu.showWorldProgress(ui::tr("hbui.CreateNewWorld.savingWorldModal.header", "Saving..."));
    localServer->closeInBackground();
}

void Client::captureWorldIcon(const SessionSnapshot& snapshot)
{
    if (snapshot.state != SessionState::Joined || !terrainReady(snapshot) || worldIconCaptureFrames > 0 || !agentCaptures.empty()) {
        return;
    }
    if (menu.currentDialog() != menu::Dialog::None || menu.currentScreen() != menu::Screen::Title || secondsNow() < nextWorldIconCapture) {
        return;
    }
    if (renderer->requestCapture()) {
        worldIconCaptureFrames = WorldIconCaptureFrames;
    }
    nextWorldIconCapture = secondsNow() + WorldIconInterval;
}

void Client::finishWorldIconCapture()
{
    ui::Bitmap frame;
    if (!renderer->takeCapture(frame.rgba, frame.width, frame.height)) {
        if (--worldIconCaptureFrames > 0) {
            renderer->requestCapture();
        }
        return;
    }
    worldIconCaptureFrames = 0;
    ui::shrinkToWidth(frame.rgba, frame.width, frame.height, WorldIconWidth);
    worldIconFrame = std::move(frame);
}

}
