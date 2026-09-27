#include "client/Client.h"
#include "client/DebugLog.h"

#include "platform/Paths.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/Theme.h"

#include "client/LocalWorlds.h"
#include "client/Sky.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace kestrel {

namespace {

constexpr size_t MinVisibleTerrain = 1024;

}

Client::Client()
    : store(platform::dataDirectory() / "servers.txt")
    , menu(store)
    , account(platform::dataDirectory() / "microsoft-token.json")
    , settingsFile(platform::dataDirectory() / "settings.txt")
{
    store.load();
    loadSettings();
    loadWorlds();
    account.restore();
    if (!font.load()) {
        throw std::runtime_error("No UI font found");
    }
    window = Window::create("Kestrel", 1280, 760);
    renderer = Renderer::create(*window);
    startSeconds = secondsNow();
}

Client::~Client()
{
    renderer.reset();
    window.reset();
}

int Client::run()
{
    float bakedScale = 0.0f;
    constexpr ui::Color canvas = ui::theme::Canvas;
    auto lastFrame = std::chrono::steady_clock::now();

    while (window->pump()) {
        if (window->consumeResize()) {
            renderer->resize(window->width(), window->height());
        }

        if (int limit = menu.maxFps(); limit != menu::UnlimitedFps) {
            std::this_thread::sleep_until(lastFrame + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / limit)));
        }
        auto now = std::chrono::steady_clock::now();
        float deltaSeconds = std::min(std::chrono::duration<float>(now - lastFrame).count(), 0.1f);
        lastFrame = now;

        syncAccount();
        session.setRenderDistance(menu.renderDistance());
        syncSession();
        applyMeshUpdates();

        bool captured = menu.capturesMouse();
        window->setMouseCaptured(captured);
        camera.update(window->input(), menu.keyBindings(), deltaSeconds, captured);
        char cameraText[96];
        std::snprintf(cameraText, sizeof(cameraText), "Camera %.1f, %.1f, %.1f", camera.x(), camera.y(), camera.z());
        menu.setCameraInfo(cameraText);
        session.setLookRay({ camera.x(), camera.y(), camera.z() }, camera.forward());

        float scale = window->contentScale() * menu.interfaceScale();
        bool rebaked = scale != bakedScale;
        if (rebaked) {
            font.bake(scale);
            bakedScale = scale;
        }
        if (rebaked || avatarRevision != uploadedAvatarRevision || titleRevision != uploadedTitleRevision) {
            uploadAtlas();
            uploadedAvatarRevision = avatarRevision;
            uploadedTitleRevision = titleRevision;
        }

        menu.setChrome({ window->drawsCaptionButtons(), window->captionInsetLeft() / menu.interfaceScale(), window->maximized(), window->fullscreen() });

        drawList.reset(scale, font.whiteU(), font.whiteV());
        ui::Context context(drawList, font, window->input(), widgets, scale);
        menu.frame(context, window->width() / scale, window->height() / scale);
        context.endFrame();

        WindowChrome chrome;
        chrome.captionHeight = menu.headerVisible() ? ui::theme::HeaderHeight * scale : 0.0f;
        for (const ui::Rect& rect : context.interactiveRects()) {
            if (rect.y < ui::theme::HeaderHeight) {
                chrome.interactive.push_back({ rect.x * scale, rect.y * scale, rect.w * scale, rect.h * scale });
            }
        }
        window->setChrome(std::move(chrome));
        window->setCursor(context.cursor());

        switch (menu.takeChromeAction()) {
        case menu::ChromeAction::Minimize:
            window->minimize();
            break;
        case menu::ChromeAction::Maximize:
            window->toggleMaximize();
            break;
        case menu::ChromeAction::Fullscreen:
            window->toggleFullscreen();
            break;
        case menu::ChromeAction::Close:
            window->close();
            break;
        case menu::ChromeAction::None:
            break;
        }

        switch (menu.takeAccountRequest()) {
        case menu::AccountRequest::SignIn:
            account.signIn();
            break;
        case menu::AccountRequest::Cancel:
            account.cancel();
            break;
        case menu::AccountRequest::SignOut:
            session.disconnect();
            account.signOut();
            break;
        case menu::AccountRequest::None:
            break;
        }

        if (std::optional<menu::ConnectRequest> request = menu.takeConnectRequest()) {
            session.connect(request->name, request->address, account.signedInAuthentication(), menu.playerName());
        }
        if (menu.takeDisconnectRequest()) {
            session.disconnect();
        }
        if (std::optional<bool> answer = menu.takePackAnswer()) {
            session.answerResourcePacks(*answer);
        }
        if (menu.interfaceScale() != savedScale || !(menu.keyBindings() == savedBindings) || menu.renderDistance() != savedRenderDistance || menu.maxFps() != savedMaxFps) {
            saveSettings();
        }
        if (menu.quitRequested()) {
            break;
        }

        if (menu.worldVisible() || (worldShown && !terrainReleased)) {
            float renderDistance = static_cast<float>(std::max(timeState.chunkRadius, 4) * 16);
            SkyFrame sky = atmosphereAt(currentWorldTime(timeState), renderDistance, timeState.rainLevel, timeState.thunderLevel);
            std::vector<SkyVertex> background;
            if (blockAssets) {
                background = buildSkyBackground(sky, blockAssets->sunLayer(), blockAssets->moonLayer(sky.moonPhase));
            }

            renderer->beginFrame(sky.fogColor[0], sky.fogColor[1], sky.fogColor[2]);
            WorldView view;
            float aspect = static_cast<float>(window->width()) / static_cast<float>(std::max<uint32_t>(window->height(), 1));
            view.viewProjection = camera.viewProjection(aspect);
            view.cameraX = camera.x();
            view.cameraY = camera.y();
            view.cameraZ = camera.z();
            view.animationTicks = static_cast<float>(std::fmod((secondsNow() - startSeconds) * 20.0, 1048576.0));
            view.fogColor = sky.fogColor;
            view.fogStart = sky.fogStart;
            view.fogEnd = sky.fogEnd;
            view.daylight = sky.daylight;
            view.sunDirection = sky.sunDirection;
            view.background = background.data();
            view.backgroundCount = static_cast<uint32_t>(background.size());
            renderer->drawWorld(view);
        } else {
            renderer->beginFrame(canvas.r / 255.0f, canvas.g / 255.0f, canvas.b / 255.0f);
        }
        renderer->drawUi(drawList);
        renderer->endFrame();
    }
    return 0;
}

void Client::syncAccount()
{
    AccountSnapshot snapshot = account.snapshot();
    menu::AccountInfo info;
    switch (snapshot.state) {
    case AccountState::SignedOut:
        info.status = menu::AccountStatus::SignedOut;
        break;
    case AccountState::Connecting:
        info.status = menu::AccountStatus::Connecting;
        break;
    case AccountState::AwaitingCode:
        info.status = menu::AccountStatus::AwaitingCode;
        break;
    case AccountState::SignedIn:
        info.status = menu::AccountStatus::SignedIn;
        break;
    case AccountState::Failed:
        info.status = menu::AccountStatus::Failed;
        break;
    }
    info.verificationUri = std::move(snapshot.verificationUri);
    info.userCode = std::move(snapshot.userCode);
    info.gamertag = std::move(snapshot.gamertag);
    info.error = std::move(snapshot.error);
    if (snapshot.avatarRevision != avatarRevision) {
        avatarRevision = snapshot.avatarRevision;
        avatarPixels = std::move(snapshot.avatar);
    }
    info.realmsLoading = snapshot.realmsLoading;
    info.realmsError = std::move(snapshot.realmsError);
    for (const Realm& realm : snapshot.realms) {
        menu::RealmEntry entry;
        entry.id = realm.id;
        entry.name = realm.name.empty() ? "Realm" : realm.name;
        entry.detail = realm.owner.empty() ? "Realm" : "by " + realm.owner;
        entry.open = realm.open;
        entry.expired = realm.expired;
        info.realms.push_back(std::move(entry));
    }
    menu.setAccount(std::move(info));
}

void Client::uploadAtlas()
{
    const std::vector<uint8_t>& coverage = font.atlasPixels();
    uint32_t size = font.atlasSize();
    atlasPixels.resize(static_cast<size_t>(size) * size * 4);
    for (size_t i = 0; i < coverage.size(); ++i) {
        atlasPixels[i * 4 + 0] = 255;
        atlasPixels[i * 4 + 1] = 255;
        atlasPixels[i * 4 + 2] = 255;
        atlasPixels[i * 4 + 3] = coverage[i];
    }

    ui::ImageRef avatarImage;
    ui::Font::ImageSlot slot = font.imageSlot();
    if (avatarPixels.size() == static_cast<size_t>(slot.size) * slot.size * 4) {
        for (uint32_t row = 0; row < slot.size; ++row) {
            std::memcpy(atlasPixels.data() + ((static_cast<size_t>(slot.y) + row) * size + slot.x) * 4, avatarPixels.data() + static_cast<size_t>(row) * slot.size * 4, static_cast<size_t>(slot.size) * 4);
        }
        float extent = static_cast<float>(size);
        avatarImage.u0 = (slot.x + 0.5f) / extent;
        avatarImage.v0 = (slot.y + 0.5f) / extent;
        avatarImage.u1 = (slot.x + slot.size - 0.5f) / extent;
        avatarImage.v1 = (slot.y + slot.size - 0.5f) / extent;
        avatarImage.valid = true;
    }
    menu.setAvatar(avatarImage);

    ui::ImageRef titleImage;
    ui::Font::ImageSlot title = font.titleSlot();
    if (titlePixels.size() == size_t(ui::Font::TitleWidth) * ui::Font::TitleHeight * 4) {
        for (uint32_t row = 0; row < ui::Font::TitleHeight; ++row) {
            std::memcpy(atlasPixels.data() + ((static_cast<size_t>(title.y) + row) * size + title.x) * 4, titlePixels.data() + static_cast<size_t>(row) * ui::Font::TitleWidth * 4, size_t(ui::Font::TitleWidth) * 4);
        }
        float extent = static_cast<float>(size);
        titleImage.u0 = (title.x + 0.5f) / extent;
        titleImage.v0 = (title.y + 0.5f) / extent;
        titleImage.u1 = (title.x + ui::Font::TitleWidth - 0.5f) / extent;
        titleImage.v1 = (title.y + ui::Font::TitleHeight - 0.5f) / extent;
        titleImage.valid = true;
    }
    menu.setTitleImage(titleImage);
    renderer->uploadUiAtlas(atlasPixels.data(), size, size);
}

void Client::applyMeshUpdates()
{
    for (const MeshUpdate& update : session.takeMeshUpdates()) {
        const world::SubChunkKey& key = update.key;
        uint64_t id = (uint64_t(uint32_t(key.x) & 0x3FFFFFu) << 42) | (uint64_t(uint32_t(key.z) & 0x3FFFFFu) << 20) | (uint64_t(uint32_t(key.y) & 0xFFFu) << 8) | uint64_t(uint32_t(key.dimension) & 0xFFu);
        if (update.mesh) {
            ChunkMeshUpload upload;
            upload.cubes = update.mesh->cubes.data();
            upload.cubeCount = static_cast<uint32_t>(update.mesh->cubes.size());
            upload.models = update.mesh->models.data();
            upload.modelCount = static_cast<uint32_t>(update.mesh->models.size());
            upload.translucentCubes = update.mesh->translucentCubes.data();
            upload.translucentCubeCount = static_cast<uint32_t>(update.mesh->translucentCubes.size());
            upload.translucentModels = update.mesh->translucentModels.data();
            upload.translucentModelCount = static_cast<uint32_t>(update.mesh->translucentModels.size());
            renderer->setChunkMesh(id, key.x * 16, key.y * 16, key.z * 16, upload);
            if (update.mesh->cubes.empty() && update.mesh->models.empty()) {
                opaqueChunks.erase(id);
            } else {
                opaqueChunks[id] = { key.x * 16, key.y * 16, key.z * 16 };
            }
        } else {
            renderer->removeChunkMesh(id);
            opaqueChunks.erase(id);
        }
    }
}

/**
 * Sub-chunks with opaque terrain whose bounds touch the camera frustum.
 */
size_t Client::visibleTerrain() const
{
    float aspect = static_cast<float>(window->width()) / static_cast<float>(std::max<uint32_t>(window->height(), 1));
    Mat4 matrix = camera.viewProjection(aspect);
    std::array<std::array<float, 4>, 5> planes {};
    for (size_t axis = 0; axis < 2; ++axis) {
        for (size_t side = 0; side < 2; ++side) {
            float sign = side == 0 ? 1.0f : -1.0f;
            for (size_t column = 0; column < 4; ++column) {
                planes[axis * 2 + side][column] = matrix[column * 4 + 3] + sign * matrix[column * 4 + axis];
            }
        }
    }
    for (size_t column = 0; column < 4; ++column) {
        planes[4][column] = matrix[column * 4 + 2];
    }
    size_t visible = 0;
    for (const auto& [id, origin] : opaqueChunks) {
        float cx = static_cast<float>(origin[0] + 8 - camera.x());
        float cy = static_cast<float>(origin[1] + 8 - camera.y());
        float cz = static_cast<float>(origin[2] + 8 - camera.z());
        bool inside = true;
        for (const std::array<float, 4>& plane : planes) {
            float radius = 8.0f * (std::abs(plane[0]) + std::abs(plane[1]) + std::abs(plane[2]));
            if (plane[0] * cx + plane[1] * cy + plane[2] * cz + plane[3] < -radius) {
                inside = false;
                break;
            }
        }
        if (inside) {
            ++visible;
        }
    }
    return visible;
}

/**
 * The loading cover lifts once the view is dense (enough visible terrain) or
 * the whole requested area has arrived and every mesh is built, and the GPU
 * has finished a frame submitted after that point, one that drew opaque
 * terrain for a dense view; after that it stays lifted for the session.
 */
bool Client::terrainReady(const SessionSnapshot& snapshot)
{
    if (terrainReleased) {
        return true;
    }
    bool dense = visibleTerrain() >= MinVisibleTerrain;
    bool bounded = snapshot.cohortComplete && snapshot.world.pendingSubChunks == 0 && snapshot.meshJobs == 0 && !snapshot.updatesPending;
    if (!dense && !bounded) {
        readinessFrame.reset();
        return false;
    }
    if (!readinessFrame) {
        readinessFrame = renderer->submittedFrames();
        return false;
    }
    CompletedFrame completed = renderer->completedFrame();
    if (completed.submission <= *readinessFrame) {
        return false;
    }
    terrainReleased = (dense && completed.opaqueChunks != 0) || bounded;
    if (terrainReleased) {
        debugLog(std::string("terrain released, ") + (dense ? "dense view" : "bounded view") + ", gpu opaque chunks " + std::to_string(completed.opaqueChunks));
    }
    return terrainReleased;
}

void Client::syncSession()
{
    SessionSnapshot snapshot = session.snapshot();
    const std::vector<uint8_t>* wantedTitle = snapshot.titleImage.get();
    if (wantedTitle != shownTitle.get()) {
        shownTitle = snapshot.titleImage;
        titlePixels = shownTitle ? *shownTitle : std::vector<uint8_t> {};
        ++titleRevision;
    }
    if (snapshot.state == SessionState::Joined && snapshot.joinCount != seenJoin) {
        seenJoin = snapshot.joinCount;
        seenTeleport = snapshot.teleportCount;
        renderer->clearChunkMeshes();
        opaqueChunks.clear();
        terrainReleased = false;
        readinessFrame.reset();
        camera.placeAt(snapshot.spawnX, snapshot.spawnY, snapshot.spawnZ, snapshot.spawnYaw, snapshot.spawnPitch);
    }
    if (snapshot.state == SessionState::Joined && snapshot.assets && snapshot.assets != blockAssets) {
        std::shared_ptr<const world::BlockAssets> assets = snapshot.assets;
        const world::TextureArray& textures = assets->textures();
        std::array<const uint8_t*, world::TextureMipLevels> mips {};
        for (uint32_t level = 0; level < world::TextureMipLevels; ++level) {
            mips[level] = textures.mips[level].data();
        }
        BlockTextureUpload upload;
        upload.mips = mips.data();
        upload.layers = textures.layers;
        upload.size = world::TextureSize;
        upload.mipLevels = world::TextureMipLevels;
        renderer->uploadBlockTextures(upload);
        blockAssets = assets;
    }
    if (snapshot.state == SessionState::Joined && snapshot.teleportCount != seenTeleport) {
        seenTeleport = snapshot.teleportCount;
        camera.placeAt(snapshot.spawnX, snapshot.spawnY, snapshot.spawnZ, snapshot.spawnYaw, snapshot.spawnPitch);
    }
    if (snapshot.state != SessionState::Joined && seenJoin != 0 && worldShown) {
        renderer->clearChunkMeshes();
        opaqueChunks.clear();
    }
    if (snapshot.state != SessionState::Joined) {
        terrainReleased = false;
        readinessFrame.reset();
    }
    worldShown = snapshot.state == SessionState::Joined;
    timeState.worldTime = snapshot.worldTime;
    timeState.worldTimeStamp = snapshot.worldTimeStamp;
    timeState.rainLevel = snapshot.rainLevel;
    timeState.thunderLevel = snapshot.thunderLevel;
    timeState.daylightCycle = snapshot.daylightCycle;
    timeState.chunkRadius = snapshot.chunkRadius;
    menu::SessionInfo info;
    switch (snapshot.state) {
    case SessionState::Idle:
        info.status = menu::SessionStatus::Idle;
        break;
    case SessionState::Resolving:
        info.status = menu::SessionStatus::Resolving;
        break;
    case SessionState::Connecting:
        info.status = menu::SessionStatus::Connecting;
        break;
    case SessionState::Joined:
        info.loadingTerrain = !terrainReady(snapshot);
        info.status = info.loadingTerrain ? menu::SessionStatus::Connecting : menu::SessionStatus::Joined;
        break;
    case SessionState::Disconnected:
        info.status = menu::SessionStatus::Disconnected;
        break;
    case SessionState::Failed:
        info.status = menu::SessionStatus::Failed;
        break;
    }
    info.name = std::move(snapshot.name);
    info.displayName = std::move(snapshot.displayName);
    info.levelName = std::move(snapshot.levelName);
    info.gameMode = std::move(snapshot.gameMode);
    info.position = std::move(snapshot.position);
    info.dimension = snapshot.dimension;
    info.chunkRadius = snapshot.chunkRadius;
    info.packetsReceived = snapshot.packetsReceived;
    info.columns = snapshot.world.columns;
    info.subChunks = snapshot.world.subChunks;
    info.pendingSubChunks = snapshot.world.pendingSubChunks;
    info.blockUpdates = snapshot.world.blockUpdates;
    info.worldErrors = snapshot.world.decodeErrors;
    info.lastWorldError = std::move(snapshot.world.lastError);
    info.meshes = snapshot.meshes;
    info.meshQuads = snapshot.meshQuads;
    info.meshJobs = snapshot.meshJobs;
    info.textureLayers = snapshot.textureLayers;
    info.materials = snapshot.materials;
    info.diagnosticVisuals = snapshot.diagnosticVisuals;
    info.assetsError = std::move(snapshot.assetsError);
    char registry[160];
    std::snprintf(registry, sizeof(registry), "IDs %s \xC2\xB7 custom %zu (%zu states) \xC2\xB7 air %u \xC2\xB7 unknown %llu \xC2\xB7 at player: %s",
        snapshot.hashedIds ? "hashed" : "sequential", snapshot.customBlocks, snapshot.customPermutations, snapshot.airSequential,
        static_cast<unsigned long long>(snapshot.unresolvedLookups), snapshot.blockAtPlayer.c_str());
    info.registryInfo = registry;
    info.targetBlock = std::move(snapshot.targetBlock);
    info.packPrompt = snapshot.packPrompt;
    info.packCount = snapshot.packCount;
    info.packBytes = snapshot.packBytes;
    info.packDownloading = snapshot.packDownloading;
    info.packReceived = snapshot.packReceived;
    info.packTotal = snapshot.packTotal;
    info.error = std::move(snapshot.error);
    menu.setSession(std::move(info));
}

void Client::loadWorlds()
{
    std::vector<menu::WorldEntry> entries;
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (const LocalWorld& world : scanLocalWorlds()) {
        std::string played = "Never played";
        if (world.lastPlayed > 0) {
            int64_t elapsed = std::max<int64_t>(0, now - world.lastPlayed);
            if (elapsed < 3600) {
                played = "Played " + std::to_string(elapsed / 60) + " min ago";
            } else if (elapsed < 86400) {
                played = "Played " + std::to_string(elapsed / 3600) + " h ago";
            } else {
                played = "Played " + std::to_string(elapsed / 86400) + " days ago";
            }
        }
        double megabytes = static_cast<double>(world.sizeBytes) / (1024.0 * 1024.0);
        char size[32];
        std::snprintf(size, sizeof(size), "%.1f MB", megabytes);
        entries.push_back({ world.name, played + " \xC2\xB7 " + size });
    }
    menu.setWorlds(std::move(entries));
}

void Client::loadSettings()
{
    std::ifstream file(settingsFile);
    KeyBindings bindings;
    std::string line;
    while (std::getline(file, line)) {
        size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, separator);
        std::string value = line.substr(separator + 1);
        if (key == "interfaceScale") {
            float parsed = std::strtof(value.c_str(), nullptr);
            if (parsed >= 1.0f && parsed <= 2.0f) {
                menu.setInterfaceScale(parsed);
            }
        } else if (key == "renderDistance") {
            menu.setRenderDistance(std::clamp(std::atoi(value.c_str()), menu::MinRenderDistance, menu::MaxRenderDistance));
        } else if (key == "maxFps") {
            int parsed = std::atoi(value.c_str());
            menu.setMaxFps(parsed == menu::UnlimitedFps ? parsed : std::clamp(parsed, menu::MinMaxFps, menu::MaxMaxFps));
        }
        for (size_t i = 0; i < KeyBindings::Count; ++i) {
            if (key == std::string("key.") + KeyBindings::id(i)) {
                Key bound = keyFromName(value);
                if (bound != Key::None) {
                    bindings.keys[i] = bound;
                }
            }
        }
    }
    menu.setKeyBindings(bindings);
    savedScale = menu.interfaceScale();
    savedBindings = bindings;
    savedRenderDistance = menu.renderDistance();
    savedMaxFps = menu.maxFps();
}

void Client::saveSettings()
{
    std::ofstream file(settingsFile, std::ios::trunc);
    file << "interfaceScale=" << menu.interfaceScale() << '\n';
    file << "renderDistance=" << menu.renderDistance() << '\n';
    file << "maxFps=" << menu.maxFps() << '\n';
    const KeyBindings& current = menu.keyBindings();
    for (size_t i = 0; i < KeyBindings::Count; ++i) {
        file << "key." << KeyBindings::id(i) << '=' << keyName(current.keys[i]) << '\n';
    }
    savedScale = menu.interfaceScale();
    savedBindings = current;
    savedRenderDistance = menu.renderDistance();
    savedMaxFps = menu.maxFps();
}

}
