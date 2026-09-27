#include "client/Client.h"

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

namespace kestrel {

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

        auto now = std::chrono::steady_clock::now();
        float deltaSeconds = std::min(std::chrono::duration<float>(now - lastFrame).count(), 0.1f);
        lastFrame = now;

        syncAccount();
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
        if (rebaked || avatarRevision != uploadedAvatarRevision) {
            uploadAtlas();
            uploadedAvatarRevision = avatarRevision;
        }

        menu.setChrome({ window->drawsCaptionButtons(), window->captionInsetLeft() / menu.interfaceScale(), window->maximized() });

        drawList.reset(scale, font.whiteU(), font.whiteV());
        ui::Context context(drawList, font, window->input(), widgets, scale);
        menu.frame(context, window->width() / scale, window->height() / scale);
        context.endFrame();

        WindowChrome chrome;
        chrome.captionHeight = ui::theme::HeaderHeight * scale;
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
        if (menu.interfaceScale() != savedScale || !(menu.keyBindings() == savedBindings)) {
            saveSettings();
        }
        if (menu.quitRequested()) {
            break;
        }

        if (menu.worldVisible()) {
            float renderDistance = static_cast<float>(std::max(timeState.chunkRadius, 4) * 16);
            SkyFrame sky = atmosphereAt(currentWorldTime(timeState), renderDistance);
            std::vector<SkyVertex> background;
            if (blockAssets) {
                background = buildSkyBackground(sky, blockAssets->sunLayer(), blockAssets->moonLayer(sky.moonPhase));
            }
            std::vector<std::array<float, 3>> cloudOrigins = cloudTileOrigins(sky, camera.x(), camera.y(), camera.z());

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
            view.cloudOrigins = cloudOrigins.data();
            view.cloudOriginCount = static_cast<uint32_t>(cloudOrigins.size());
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
        } else {
            renderer->removeChunkMesh(id);
        }
    }
}

void Client::syncSession()
{
    SessionSnapshot snapshot = session.snapshot();
    if (snapshot.state == SessionState::Joined && snapshot.joinCount != seenJoin) {
        seenJoin = snapshot.joinCount;
        seenTeleport = snapshot.teleportCount;
        renderer->clearChunkMeshes();
        camera.placeAt(snapshot.spawnX, snapshot.spawnY, snapshot.spawnZ, snapshot.spawnYaw, snapshot.spawnPitch);
        if (!blockTexturesUploaded) {
            std::string error;
            if (std::shared_ptr<const world::BlockAssets> assets = world::BlockAssets::shared(error)) {
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
                std::vector<SkyVertex> clouds = buildCloudMesh(assets->cloudMask());
                renderer->setCloudMesh(clouds.data(), static_cast<uint32_t>(clouds.size()));
                blockAssets = assets;
                blockTexturesUploaded = true;
            }
        }
    }
    if (snapshot.state == SessionState::Joined && snapshot.teleportCount != seenTeleport) {
        seenTeleport = snapshot.teleportCount;
        camera.placeAt(snapshot.spawnX, snapshot.spawnY, snapshot.spawnZ, snapshot.spawnYaw, snapshot.spawnPitch);
    }
    if (snapshot.state != SessionState::Joined && seenJoin != 0 && worldShown) {
        renderer->clearChunkMeshes();
    }
    worldShown = snapshot.state == SessionState::Joined;
    timeState.worldTime = snapshot.worldTime;
    timeState.worldTimeStamp = snapshot.worldTimeStamp;
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
        info.status = menu::SessionStatus::Joined;
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
}

void Client::saveSettings()
{
    std::ofstream file(settingsFile, std::ios::trunc);
    file << "interfaceScale=" << menu.interfaceScale() << '\n';
    const KeyBindings& current = menu.keyBindings();
    for (size_t i = 0; i < KeyBindings::Count; ++i) {
        file << "key." << KeyBindings::id(i) << '=' << keyName(current.keys[i]) << '\n';
    }
    savedScale = menu.interfaceScale();
    savedBindings = current;
}

}
