#include "client/Client.h"
#include "menu/GuiScale.h"
#include "client/HandVisibility.h"
#include "client/DebugLog.h"
#include "client/DiscordPresence.h"

#include "platform/Paths.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/Localization.h"
#include "ui/Theme.h"
#include "util/Text.h"

#include "client/Sky.h"
#include "world/PackSource.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace kestrel {

namespace {

constexpr size_t MinVisibleTerrain = 1024;
constexpr int HiddenMaxFps = 30;
constexpr uint32_t EntityQuadFlag = 1u << 5;
constexpr uint32_t AdditiveQuadFlag = 1u << 6;
constexpr uint32_t ShadedQuadFlag = 1u << 8;
constexpr uint8_t OpenSkyLight = 0xF0;

uint64_t subChunkId(const world::SubChunkKey& key)
{
    return (uint64_t(uint32_t(key.x) & 0x3FFFFFu) << 42) | (uint64_t(uint32_t(key.z) & 0x3FFFFFu) << 20) | (uint64_t(uint32_t(key.y) & 0xFFFu) << 8) | uint64_t(uint32_t(key.dimension) & 0xFFu);
}
constexpr const char* FeaturedSpritePrefix = "dynamic/featured/";
constexpr float SpyglassFovScale = 0.1f;
constexpr float MaxCameraEaseSeconds = 60.0f;
}

/**
 * The field of view in degrees once a server's camera instruction is
 * applied: a new instruction eases from where the view stands toward its
 * degrees, and a release eases back to the player's own setting before the
 * setting takes over again.
 */
float Client::serverFovDegrees(float settingDegrees, float deltaSeconds)
{
    if (cameraFovRequest.serial == 0) {
        serverFov = {};
        serverFovSerial = 0;
        return settingDegrees;
    }
    auto current = [&]() {
        if (!serverFov.active) {
            return settingDegrees;
        }
        float progress = serverFov.duration > 0.0f ? cameraEase(serverFov.easeType, serverFov.elapsed / serverFov.duration) : 1.0f;
        float target = serverFov.returning ? settingDegrees : serverFov.to;
        return serverFov.from + (target - serverFov.from) * progress;
    };
    if (cameraFovRequest.serial != serverFovSerial) {
        serverFovSerial = cameraFovRequest.serial;
        float from = current();
        float duration = std::isfinite(cameraFovRequest.easeSeconds) ? std::clamp(cameraFovRequest.easeSeconds, 0.0f, MaxCameraEaseSeconds) : 0.0f;
        bool returning = cameraFovRequest.clear || !std::isfinite(cameraFovRequest.degrees) || cameraFovRequest.degrees <= 0.0f;
        if (returning && duration <= 0.0f) {
            serverFov = {};
        } else {
            serverFov.active = true;
            serverFov.from = from;
            serverFov.to = returning ? settingDegrees : cameraFovRequest.degrees;
            serverFov.returning = returning;
            serverFov.elapsed = 0.0f;
            serverFov.duration = duration;
            serverFov.easeType = cameraFovRequest.easeType;
        }
    }
    float degrees = current();
    if (serverFov.active && std::isfinite(deltaSeconds) && deltaSeconds > 0.0f) {
        serverFov.elapsed += deltaSeconds;
        if (serverFov.returning && serverFov.elapsed >= serverFov.duration) {
            serverFov = {};
        }
    }
    return degrees;
}

Client::Client(LaunchOptions options)
    : store(platform::dataDirectory() / "servers.txt")
    , menu(store)
    , account(platform::dataDirectory() / "microsoft-token.json")
    , settingsFile(platform::dataDirectory() / "settings.txt")
    , globalResources(platform::dataDirectory() / "resource_packs")
{
    StartupTimer timer;
    store.load();
    loadSettings();
    account.restore();
    timer.mark("servers, settings and account");
    featured = std::make_unique<FeaturedServers>(menu.language());
    menu.formPanel().imageSprite = [this](const menu::FormImage& image) { return formImage(image); };
    timer.mark("featured servers");
    if (!font.load(assets, skin)) {
        throw std::runtime_error("Kestrel draws its menus with the installed game's fonts and textures, install Minecraft Bedrock or set KESTREL_VANILLA_PACK");
    }
    timer.mark("fonts");
    launch = std::move(options);
    if (launch.connect) {
        pendingConnect = menu::ConnectRequest { *launch.connect, *launch.connect };
    }
    window = Window::create("Kestrel", launch.width, launch.height, !launch.hidden);
    if (savedFullscreen && !launch.hidden) {
        window->toggleFullscreen();
    }
    timer.mark("window");
    renderer = Renderer::create(*window);
    timer.mark("renderer");
    startSeconds = secondsNow();
    pendingSoundEngine = std::async(std::launch::async, [] {
        auto engine = std::make_unique<audio::SoundEngine>();
        debugLog(std::string("sound engine ") + (engine->ready() ? "ready" : "failed to open the output device"));
        return engine;
    });
    std::filesystem::path vanilla = world::PackSource::locateVanilla();
    if (!vanilla.empty()) {
        vanillaSounds = std::make_shared<world::PackSource>(vanilla);
        std::error_code error;
        if (std::filesystem::is_directory(vanilla.parent_path() / "vanilla_music", error)) {
            musicSounds = std::make_shared<world::PackSource>(vanilla.parent_path() / "vanilla_music");
        }
    }
    timer.mark("vanilla pack");
    ui::Localization::shared().load(vanillaSounds, menu.language());
    std::error_code oreuiError;
    if (!vanilla.empty() && std::filesystem::is_directory(vanilla.parent_path() / "oreui", oreuiError)) {
        ui::Localization::shared().setInterfacePack(std::make_shared<world::PackSource>(vanilla.parent_path() / "oreui"));
    }
    timer.mark("localization");
    startMods();
    timer.mark("mods");
    if (launch.agent) {
        startAgent();
    }
}

Client::~Client()
{
    // Mods hold shaders on the renderer, so they go first.
    if (mods) {
        mods->shutdown();
    }
    mods.reset();
    renderer.reset();
    window.reset();
}

int Client::run()
{
    DiscordPresence discord;
    float bakedScale = 0.0f;
    bool firstFrameLogged = false;
    constexpr ui::Color canvas = ui::theme::Black;
    auto lastFrame = std::chrono::steady_clock::now();

    while (true) {
        profiler.beginFrame();
        {
            Profiler::Section section(profiler, "window events");
            if (!window->pump()) {
                break;
            }
        }
        discord.update();
        driveGamepad();
        if (agentServer) {
            Profiler::Section section(profiler, "agent");
            serveAgent();
        }
        mods->handleInput(window->input(), menu.capturesMouse(), guiScale());
        if (window->consumeFocusLost() && !agentServer) {
            menu.pauseIfPlaying();
        }
        if (window->consumeResize()) {
            Profiler::Section section(profiler, "resize");
            renderer->resize(window->width(), window->height());
        }

        int limit = menu.maxFps();
        // Nobody watches a hidden window, and an agent reads it a few times a second at most.
        if (!window->visible()) {
            limit = limit == menu::UnlimitedFps ? HiddenMaxFps : std::min(limit, HiddenMaxFps);
        }
        if (limit != menu::UnlimitedFps) {
            Profiler::Section section(profiler, "fps cap wait");
            std::this_thread::sleep_until(lastFrame + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / limit)));
        }
        auto now = std::chrono::steady_clock::now();
        updateGlobalResources();
        float environmentDeltaSeconds = std::clamp(std::chrono::duration<float>(now - lastFrame).count(), 0.0f, 1.0f);
        float deltaSeconds = std::min(environmentDeltaSeconds, 0.1f);
        lastFrame = now;
        countFrame(now);

        {
            Profiler::Section section(profiler, "account");
            syncAccount();
            syncSocial();
            syncDressingRoom();
        }
        {
            Profiler::Section section(profiler, "session sync");
            session.setRenderDistance(menu.renderDistance());
            syncSession();
            syncChat();
            syncForms();
            for (menu::ModAction& action : menu.takeModActions()) {
                mods->request(std::move(action));
            }
            mods->update(deltaSeconds);
            visuals = mods->visuals();
            if (std::optional<modding::BlockFilter> hidden = mods->takeHiddenBlocks()) {
                session.setHiddenBlocks(std::move(hidden->names), hidden->visibleOnly);
            }
            menu.setModKeyBinds(mods->listedKeyBinds());
            menu.setMods(mods->listedMods());
        }
        {
            Profiler::Section section(profiler, "mesh upload");
            applyMeshUpdates();
        }

        {
            Profiler::Section section(profiler, "camera");
            menu.setInventory(hudState.container, hudState.gameType == 1);
            menu.prepareInventoryInput(window->input());
            bool captured = menu.capturesMouse() && !mods->wantsCursor();
            window->setMouseCaptured(captured);
            float baseFov = serverFovDegrees(static_cast<float>(menu.fov()), deltaSeconds);
            camera.setFovOverridden(serverFov.active);
            camera.setBaseFov(serverFov.active ? baseFov : baseFov * mods->fovScale());
            if (playerView.active) {
                const InputState& keys = window->input();
                const KeyBindings& bindings = menu.keyBindings();
                camera.look(keys, captured);
                MotionInput input;
                if (captured) {
                    input.forward = float(keys.isHeld(bindings.forward())) - float(keys.isHeld(bindings.back()));
                    input.sideways = float(keys.isHeld(bindings.left())) - float(keys.isHeld(bindings.right()));
                    if (input.forward != 0.0f && input.sideways != 0.0f) {
                        input.forward *= 0.70710677f;
                        input.sideways *= 0.70710677f;
                    }
                    input.jump = keys.isHeld(bindings.up());
                    input.sneak = keys.isHeld(bindings.down());
                    input.sprint = keys.isHeld(Key::Control);
                    if (padMove[0] != 0.0f || padMove[1] != 0.0f) {
                        input.forward = padMove[1];
                        input.sideways = -padMove[0];
                    }
                }
                input.yaw = camera.minecraftYaw();
                input.pitch = camera.minecraftPitch();
                mods->adjustMovement(input);
                localLookYaw = input.yaw;
                localLookPitch = input.pitch;
                session.setMotionInput(input);
                constexpr int32_t SpeedEffect = 1;
                constexpr int32_t SlownessEffect = 2;
                int32_t speedLevels = 0;
                int32_t slownessLevels = 0;
                double now = secondsNow();
                for (const HudEffect& effect : hudState.effects) {
                    if (effect.expires >= 0.0 && effect.expires < now) {
                        continue;
                    }
                    int32_t levels = std::clamp(effect.amplifier + 1, 0, 255);
                    if (effect.id == SpeedEffect) {
                        speedLevels = std::max(speedLevels, levels);
                    } else if (effect.id == SlownessEffect) {
                        slownessLevels = std::max(slownessLevels, levels);
                    }
                }
                float speedRatio = 1.0f + (playerView.sprinting ? 0.3f : 0.0f) + 0.2f * float(speedLevels) - 0.15f * float(slownessLevels);
                float fovTarget = (std::max(speedRatio, 0.0f) + 1.0f) * 0.5f;
                if (playerView.flying) {
                    fovTarget *= 1.1f;
                }
                if (!menu.gameplayFov()) {
                    fovTarget = 1.0f;
                }
                // Drawing a bow narrows the view as the pull tightens over its first second.
                const HudItem& heldItem = hudState.inventory[static_cast<size_t>(std::clamp(hudState.selectedSlot, 0, 8))];
                double drawn = localItemUseTicks();
                if (drawn > 0.0 && heldItem.identifier == "minecraft:bow") {
                    float pull = static_cast<float>(std::min(drawn / 20.0, 1.0));
                    fovTarget *= 1.0f - pull * pull * 0.15f;
                }
                if (drawn > 0.0 && heldItem.identifier == "minecraft:spyglass") {
                    fovTarget = SpyglassFovScale;
                }
                fovTarget = std::clamp(fovTarget, 0.05f, 2.0f);
                camera.easeFov(fovTarget, deltaSeconds);
                // The session thread runs a tick a few ms late now and then, so keep gliding a little past it
                // instead of stopping dead until the tick lands.
                double blend = std::clamp((secondsNow() - playerView.tickTime) / 0.05, 0.0, 1.2);
                double eye = playerView.eyeHeight();
                eyePosition = { playerView.previous[0] + (playerView.current[0] - playerView.previous[0]) * blend,
                    playerView.previous[1] + (playerView.current[1] - playerView.previous[1]) * blend + eye,
                    playerView.previous[2] + (playerView.current[2] - playerView.previous[2]) * blend };
                perspective = std::clamp(menu.option("third_person", PerspectiveFirst), PerspectiveFirst, PerspectiveFront);
                if (captured && !serverCamera.controlsPerspective() && keys.pressedKey == bindings.perspective()) {
                    perspective = (perspective + 1) % 3;
                    menu.setExtraOption("third_person", perspective);
                }
                std::array<float, 3> look = camera.forward();
                double reach = perspective == PerspectiveBack ? -ThirdPersonRadius : perspective == PerspectiveFront ? ThirdPersonRadius : 0.0;
                std::array<double, 3> boom { look[0] * reach, look[1] * reach, look[2] * reach };
                session.setCameraBoom(eyePosition, boom);
                camera.setFacingSubject(perspective == PerspectiveFront);
                camera.setPosition(eyePosition[0] + boom[0] * boomFraction, eyePosition[1] + boom[1] * boomFraction, eyePosition[2] + boom[2] * boomFraction);
            } else {
                perspective = PerspectiveFirst;
                camera.setFacingSubject(false);
                camera.easeFov(1.0f, deltaSeconds);
                camera.update(window->input(), menu.keyBindings(), deltaSeconds, captured);
                eyePosition = { camera.x(), camera.y(), camera.z() };
            }
            session.setLookRay(eyePosition, camera.forward());
            camera.setHurtProgress(playerView.active && hudState.lastHurt > 0.0
                ? static_cast<float>(std::clamp(1.0 - (secondsNow() - hudState.lastHurt) / 0.5, 0.0, 1.0)) * visuals.hurtCamera.value_or(1.0f) : 0.0f);
        }
        std::array<double, 3> playerCameraPosition { camera.x(), camera.y(), camera.z() };
        float playerCameraYaw = camera.minecraftYaw();
        float playerCameraPitch = camera.minecraftPitch();
        bool playerCameraFacing = camera.isFacingSubject();
        int playerPerspective = perspective;
        std::optional<ActorView> renderSelf;
        interpolateActors(secondsNow());
        if (worldShown) {
            if (playerView.active) renderSelf = localActorView(deltaSeconds);
            ServerCameraContext cameraContext;
            cameraContext.player = { eyePosition, playerCameraYaw, playerCameraPitch };
            cameraContext.player.center = { eyePosition[0], eyePosition[1] - playerView.eyeHeight() + 0.9, eyePosition[2] };
            cameraContext.base = { playerCameraPosition, playerCameraYaw + (playerCameraFacing ? 180.0f : 0.0f), playerCameraFacing ? -playerCameraPitch : playerCameraPitch };
            cameraContext.actor = [&](int64_t id) -> std::optional<CameraSubject> {
                if (id == cameraLocalUnique) return cameraContext.player;
                auto actor = std::find_if(actorViews.begin(), actorViews.end(), [&](const ActorView& a) { return a.uniqueId == id; });
                if (actor == actorViews.end()) return {};
                double height = actor->height > 0.0f ? actor->height : 1.8 * actor->scale;
                return CameraSubject { { actor->x, actor->y + height * 0.9, actor->z }, actor->yaw, actor->pitch,
                    { actor->x, actor->y + height * 0.5, actor->z } };
            };
            cameraContext.obstruction = [&](const std::array<double, 3>& origin, const std::array<double, 3>& delta) {
                session.setServerCameraBoom(origin, delta);
                return serverBoomFraction;
            };
            serverCamera.update(camera, cameraContext, session.takeCameraEvents(), deltaSeconds);
            if (!serverCamera.orbital()) session.setServerCameraBoom({}, {});
            if (!serverCamera.playerEffects()) camera.setHurtProgress(0.0f);
            cameraDetached = serverCamera.detached();
            perspective = serverCamera.renderPerspective(playerPerspective);
        }
        {
            Profiler::Section section(profiler, "hud");
            handleHotbarInput();
            updateEmotes(secondsNow());
            updateGameTips();
            menu.setHud(buildHudView());
            if (!worldShown) {
                for (const menu::SavedServer& server : store.servers()) {
                    pinger.request(server.address);
                }
                std::map<std::string, menu::ServerStatus> status;
                for (const auto& [address, ping] : pinger.results()) {
                    menu::ServerStatus& entry = status[address];
                    entry.checked = ping.state != PingState::Checking;
                    entry.online = ping.state == PingState::Online;
                    entry.motd = ping.motd;
                    entry.players = ping.players;
                    entry.latencyMs = ping.latencyMs;
                }
                menu.setServerStatus(std::move(status));
                syncFeatured();
            }
        }

        float scale = guiScale();
        bool rebaked = scale != bakedScale;
        if (rebaked) {
            Profiler::Section section(profiler, "font atlas");
            font.bake(scale);
            bakedScale = scale;
        }

        menu.setChrome({ window->drawsCaptionButtons(), window->captionInsetLeft() / scale, window->maximized(), window->fullscreen() });

        if (rebaked || skin.dirty()) {
            Profiler::Section section(profiler, "ui atlas");
            uploadAtlas(rebaked);
        }
        skin.beginFrame();
        drawList.reset(scale, font.whiteU(), font.whiteV());
        ui::Context context(drawList, font, skin, window->input(), widgets, scale);
        context.recordWidgets(agentServer != nullptr);
        {
            Profiler::Section section(profiler, "menu ui");
            menu.frame(context, window->width() / scale, window->height() / scale);
            if (padCursorShown && window->input().gamepad.connected && !menu.capturesMouse()) {
                const InputState& pointer = window->input();
                context.sprite({ pointer.mouseX / scale, pointer.mouseY / scale, 16.0f, 16.0f }, "ui/cursor");
            }
            if (agentServer) {
                agentWidgets = context.widgets();
            }
            if (menu.worldVisible() && !menu.hudHidden()) {
                mods->drawHud(context, window->width() / scale, window->height() / scale, !menu.capturesMouse());
            }
            for (auto& command : menu.inventoryPanel().takeCommands()) session.requestInventory(std::move(command));
            for (menu::FormAnswer& answer : menu.formPanel().takeAnswers()) {
                session.answerForm(answer.id, std::move(answer.data), answer.busy);
            }
            context.endFrame();
        }

        if (!firstFrameLogged) {
            firstFrameLogged = true;
            char at[48];
            std::snprintf(at, sizeof(at), "startup first menu frame at %.1f ms", processMilliseconds());
            debugLog(at);
        }

        WindowChrome chrome;
        float caption = menu.captionHeight();
        chrome.captionHeight = caption * scale;
        for (const ui::Rect& rect : context.interactiveRects()) {
            if (rect.y < caption) {
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
            social.setAccount(nullptr, {});
            account.signOut();
            break;
        case menu::AccountRequest::None:
            break;
        }
        for (const SocialRequest& request : menu.takeSocialRequests()) {
            social.handle(request);
        }
        if (menu.takeRealmsRefreshRequest()) {
            account.refreshRealms();
        }

        if (std::optional<menu::ConnectRequest> request = menu.takeConnectRequest()) {
            session.connect(request->name, request->address, account.signedInAuthentication(), offlineName());
        }
        if (menu.takeRespawnRequest()) {
            session.requestRespawn();
        }
        if (menu.takeDisconnectRequest()) {
            session.disconnect();
        }
        if (std::optional<bool> answer = menu.takePackAnswer()) {
            session.answerResourcePacks(*answer);
        }
        if (menu.language() != savedLanguage) {
            ui::Localization::shared().load(vanillaSounds, menu.language());
            social.setLanguage(menu.language());
            saveSettings();
        }
        renderer->setVsync(menu.vsync());
        if (menu.interfaceScale() != savedScale || !(menu.keyBindings() == savedBindings) || menu.renderDistance() != savedRenderDistance || menu.maxFps() != savedMaxFps || menu.fov() != savedFov || window->fullscreen() != savedFullscreen || menu.paperDollHidden() != savedPaperDollHidden || menu.vsync() != savedVsync || menu.gameplayFov() != savedGameplayFov || menu.safeArea() != savedSafeArea || menu.brightness() != savedBrightness || menu.glintStrength() != savedGlintStrength || menu.glintSpeed() != savedGlintSpeed || menu.extraOptions() != savedExtraOptions || menu.offlineName() != savedOfflineName || menu.soundVolumes() != savedVolumes || !(menu.chatSettings() == savedChat)) {
            saveSettings();
        }
        if (menu.quitRequested() || agentQuit) {
            break;
        }

        if (menu.worldVisible() || (worldShown && !terrainReleased)) {
            std::optional<modding::CameraRequest> detachedView = mods->cameraView();
            cameraDetached = detachedView.has_value() || serverCamera.detached();
            std::array<double, 3> heldPosition { camera.x(), camera.y(), camera.z() };
            float heldYaw = camera.minecraftYaw();
            float heldPitch = camera.minecraftPitch();
            bool heldFacing = camera.isFacingSubject();
            if (detachedView) {
                camera.setFacingSubject(false);
                camera.setPosition(detachedView->position.x, detachedView->position.y, detachedView->position.z);
                camera.setRotation(detachedView->rotation.yaw, detachedView->rotation.pitch);
            }
            float viewAspect = static_cast<float>(window->width()) / static_cast<float>(std::max<uint32_t>(window->height(), 1));
            float verticalFov = 2.0f * std::atan(camera.halfVerticalTangent(viewAspect)) * 180.0f / 3.14159265f;
            mods->setView({ camera.x(), camera.y(), camera.z() }, { camera.minecraftYaw(), camera.minecraftPitch() }, verticalFov);
            float renderDistance = static_cast<float>(std::max(timeState.chunkRadius, 4) * 16);
            session.setRenderedCamera({ camera.x(), camera.y(), camera.z() });
            auto environmentSample = seenSessionSnapshot ? session.cameraEnvironment(*seenSessionSnapshot,
                { camera.x(), camera.y(), camera.z() }) : std::pair<uint8_t, uint32_t> { 0, 1 };
            timeState.cameraMedium = environmentSample.first;
            submergedSeconds = timeState.cameraMedium == 1 ? submergedSeconds + environmentDeltaSeconds : 0.0f;
            world::BiomeColors biomeColors = blockAssets ? blockAssets->biomeTints().colors(environmentSample.second) : world::BiomeColors {};
            float fogScale = biomeColors.waterFogRelative ? renderDistance : 1.0f;
            bool fireResistance = false;
            if (timeState.cameraMedium == 2 && serverCamera.playerEffects()) {
                constexpr int32_t FireResistanceEffect = 12;
                double now = secondsNow();
                for (const HudEffect& effect : hudState.effects) {
                    if (effect.id == FireResistanceEffect && (effect.expires < 0.0 || effect.expires > now)) {
                        fireResistance = true;
                        break;
                    }
                }
            }
            const auto& lavaFog = fireResistance && biomeColors.lavaResistanceFog
                ? biomeColors.lavaResistanceFog : biomeColors.lavaFog;
            SkyFrame sky;
            std::vector<SkyVertex> background;
            {
                Profiler::Section section(profiler, "sky");
                sky = atmosphereAt(currentWorldTime(timeState), renderDistance, timeState.rainLevel, timeState.thunderLevel);
                if (timeState.dimension == 1) {
                    sky = SkyFrame {};
                    if (biomeColors.airFog) {
                        const auto& fog = *biomeColors.airFog;
                        sky.fogColor = { float((fog.color >> 16) & 255) / 255.0f,
                            float((fog.color >> 8) & 255) / 255.0f, float(fog.color & 255) / 255.0f };
                        float scale = fog.relative ? renderDistance : 1.0f;
                        sky.fogStart = fog.start * scale;
                        sky.fogEnd = fog.end * scale;
                    }
                    sky.zenith = sky.horizon = sky.fogColor;
                }
                sky = submergedIn(sky, timeState.cameraMedium,
                    submergedSeconds, biomeColors.waterFog, biomeColors.waterFogStart * fogScale, biomeColors.waterFogEnd * fogScale,
                    lavaFog ? &*lavaFog : nullptr, renderDistance,
                    blockAssets ? blockAssets->biomeTints().powderSnowFog() : nullptr);
                if (blockAssets && timeState.fogStack) {
                    auto medium = timeState.cameraMedium == 1 ? world::FogMedium::Water
                        : timeState.cameraMedium == 2 ? (fireResistance ? world::FogMedium::LavaResistance : world::FogMedium::Lava)
                        : timeState.cameraMedium == 3 ? world::FogMedium::PowderSnow
                        : timeState.dimension == 0 && timeState.rainLevel > 0.0f ? world::FogMedium::Weather : world::FogMedium::Air;
                    if (const auto* fog = blockAssets->biomeTints().commandFog(*timeState.fogStack, medium)) {
                        sky = foggedBy(sky, *fog, renderDistance, submergedSeconds);
                        if (timeState.cameraMedium != 0 || timeState.dimension == 1) sky.zenith = sky.horizon = sky.fogColor;
                    }
                }
                if (blockAssets && timeState.cameraMedium == 0 && timeState.dimension != 1) {
                    background = buildSkyBackground(sky, blockAssets->sunLayer(), blockAssets->moonLayer(sky.moonPhase));
                }
            }

            {
                Profiler::Section section(profiler, "begin frame");
                renderer->beginFrame(sky.fogColor[0], sky.fogColor[1], sky.fogColor[2]);
            }
            WorldView view;
            float aspect = static_cast<float>(window->width()) / static_cast<float>(std::max<uint32_t>(window->height(), 1));
            view.viewProjection = camera.viewProjection(aspect);
            view.cameraX = camera.x();
            view.cameraY = camera.y();
            view.cameraZ = camera.z();
            view.animationTicks = static_cast<float>(std::fmod((secondsNow() - startSeconds) * 20.0, 1048576.0));
            // a mod pushing the fog out scales both ends, the fade keeps its shape
            float fogStretch = visuals.fogScale.value_or(1.0f);
            sky.fogStart *= fogStretch;
            sky.fogEnd *= fogStretch;
            view.fogColor = sky.fogColor;
            view.fogStart = sky.fogStart;
            view.cameraMedium = timeState.cameraMedium;
            view.fogEnd = sky.fogEnd;
            view.daylight = sky.daylight;
            float brightnessLift = menu::brightnessLift(menu.brightness());
            view.nightVision = 1.0f - (1.0f - (serverCamera.playerEffects() ? nightVisionStrength() : 0.0f)) * (1.0f - brightnessLift);
            view.nightVision = std::max(view.nightVision, visuals.brightness.value_or(0.0f));
            if (visuals.hitColor) {
                const mod::Color& hit = *visuals.hitColor;
                // 0 means the game's own red, so a fully clear color still counts as set
                view.hitColor = uint32_t(hit.r) | uint32_t(hit.g) << 8 | uint32_t(hit.b) << 16 | uint32_t(std::max<uint8_t>(hit.a, 1)) << 24;
            }
            view.sunDirection = sky.sunDirection;
            view.background = background.data();
            view.backgroundCount = static_cast<uint32_t>(background.size());
            std::array<int32_t, 3> entityOrigin { int32_t(std::floor(camera.x())), int32_t(std::floor(camera.y())), int32_t(std::floor(camera.z())) };
            std::vector<world::ModelQuadGpu> entityQuads;
            {
                Profiler::Section section(profiler, "entities");
                const auto& self = renderSelf;
                if (self) {
                    if (perspective != PerspectiveFirst || cameraDetached) {
                        actorViews.push_back(*self);
                    }
                }
                std::vector<world::ModelQuadGpu> blendedQuads;
                std::vector<world::ModelQuadGpu> handQuads;
                std::vector<world::ModelQuadGpu> overlayQuads;
                buildActorQuads(entityOrigin, entityQuads, blendedQuads);
                blockParticles.update(secondsNow());
                blockParticles.append(entityOrigin, { camera.x(), camera.y(), camera.z() }, entityQuads);
                appendParticles(deltaSeconds, entityOrigin, entityQuads, blendedQuads);
                appendChestLids(entityOrigin, deltaSeconds, entityQuads);
                if (!menu.hudHidden()) {
                    if (handVisible(menu.hudHidden(), menu.option("hide_hand", 0) != 0)) {
                        appendFirstPerson(entityOrigin, handQuads);
                    }
                    lightQuads(handQuads, 0, lightCorners(camera.x(), camera.y() - 1.0, camera.z()));
                    if (self) {
                        appendPaperDoll(*self, entityOrigin, handQuads);
                    }
                }
                appendBlockOverlays(entityOrigin, overlayQuads);
                view.entityQuadCount = static_cast<uint32_t>(entityQuads.size());
                view.entityBlendCount = static_cast<uint32_t>(blendedQuads.size());
                view.handQuadCount = static_cast<uint32_t>(handQuads.size());
                view.overlayQuadCount = static_cast<uint32_t>(overlayQuads.size());
                entityQuads.insert(entityQuads.end(), blendedQuads.begin(), blendedQuads.end());
                entityQuads.insert(entityQuads.end(), handQuads.begin(), handQuads.end());
                entityQuads.insert(entityQuads.end(), overlayQuads.begin(), overlayQuads.end());
            }
            view.actorDraws = actorDraws.data();
            view.actorDrawCount = static_cast<uint32_t>(actorDraws.size());
            view.entityQuads = entityQuads.data();
            view.entityOrigin = { float(entityOrigin[0] - camera.x()), float(entityOrigin[1] - camera.y()), float(entityOrigin[2] - camera.z()) };
            Profiler::Section section(profiler, "draw world");
            mod::Environment environment;
            environment.camera = { camera.x(), camera.y(), camera.z() };
            environment.sunDirection = sky.sunDirection;
            environment.daylight = sky.daylight;
            environment.fogColor = sky.fogColor;
            environment.fogStart = sky.fogStart;
            environment.fogEnd = sky.fogEnd;
            environment.rain = timeState.rainLevel;
            environment.thunder = timeState.thunderLevel;
            environment.medium = timeState.cameraMedium;
            environment.nightVision = serverCamera.playerEffects() ? nightVisionStrength() : 0.0f;
            environment.moonPhase = sky.moonPhase;
            mods->setEnvironment(environment);
            renderer->drawWorld(view);
            mods->drawWorld(view.viewProjection, environment.camera);
            mods->drawPost(view.viewProjection);
            if (soundEngine && soundEngine->ready() && !serverCamera.playerListener())
                soundEngine->setListener({ camera.x(), camera.y(), camera.z() }, camera.forward());
            camera.setPosition(heldPosition[0], heldPosition[1], heldPosition[2]);
            camera.setRotation(heldYaw, heldPitch);
            camera.setFacingSubject(heldFacing);
            camera.setServerRoll(0.0f);
        } else {
            Profiler::Section section(profiler, "begin frame");
            renderer->beginFrame(canvas.r / 255.0f, canvas.g / 255.0f, canvas.b / 255.0f);
        }
        {
            Profiler::Section section(profiler, "draw ui");
            mods->drawScreen(CustomLayer::BelowUi);
            ui::Color fade = serverCamera.fadeColor();
            if (worldShown && fade.a) {
                drawList.clearClip();
                drawList.clearLayer();
                drawList.setOrigin(0.0f, 0.0f);
                drawList.fill({ 0, 0, float(window->width()) / scale, float(window->height()) / scale }, fade);
            }
            renderer->drawUi(drawList);
            mods->drawScreen(CustomLayer::AboveUi);
        }
        {
            Profiler::Section section(profiler, "present gpu");
            renderer->endFrame();
        }
        camera.setPosition(playerCameraPosition[0], playerCameraPosition[1], playerCameraPosition[2]);
        camera.setRotation(playerCameraYaw, playerCameraPitch);
        camera.setFacingSubject(playerCameraFacing);
        camera.setServerRoll(0.0f);
        perspective = playerPerspective;
        if (!agentCaptures.empty()) {
            finishAgentCaptures();
        }
        profiler.endFrame();
    }
    return 0;
}

void Client::collectFeaturedImages()
{
    std::set<std::string> showcaseUrls;
    for (const FeaturedServer& server : featuredList) {
        showcaseUrls.insert(server.showcaseUrls.begin(), server.showcaseUrls.end());
        for (const FeaturedGame& game : server.games) {
            showcaseUrls.insert(game.imageUrl);
        }
    }
    for (auto& [url, bitmap] : featured->takeImages()) {
        if (showcaseUrls.count(url)) {
            featuredShowcases[url] = std::move(bitmap);
            continue;
        }
        skin.setDynamic(FeaturedSpritePrefix + url, std::move(bitmap));
        featuredImages.insert(url);
        featuredDirty = true;
    }
}

void Client::syncForms()
{
    for (FormRequest& request : session.takeForms()) {
        if (agentServer) {
            agent::JsonWriter writer;
            writer.beginObject().field("id", request.id).field("close", request.close);
            if (!request.close) {
                writer.field("json", request.data);
            }
            agentEvents.add("form", writer.endObject().take());
        }
        if (request.close) {
            menu.formPanel().closeAll();
        } else if (mods->filterForm(request)) {
            menu.openForm(request.id, request.data);
        }
    }
    if (worldShown && menu.formPanel().active()) {
        collectFeaturedImages();
    }
}

/**
 * The sprite of a form button icon. Pack textures come straight from the
 * skin, web images go through the featured servers downloader, which already
 * decodes square icons off the main thread.
 */
std::string Client::formImage(const menu::FormImage& image)
{
    if (image.url) {
        if (featuredImages.count(image.data)) {
            return FeaturedSpritePrefix + image.data;
        }
        featured->requestImage(image.data, false, true);
        return menu::FormImageLoading;
    }
    std::string path = image.data;
    for (const char* extension : { ".png", ".tga", ".jpg" }) {
        if (util::endsWith(path, extension)) {
            path.resize(path.size() - std::char_traits<char>::length(extension));
            break;
        }
    }
    // The server's packs come before the game's own textures, as they do in game.
    return loadPackTexture(path) || skin.bitmap(path) ? path : std::string();
}

void Client::syncFeatured()
{
    if (!featuredListed && !featured->loading()) {
        featuredList = featured->servers();
        featuredListed = true;
        featuredDirty = true;
        for (const FeaturedServer& server : featuredList) {
            featured->requestImage(server.iconUrl, false);
        }
        for (const FeaturedServer& server : featuredList) {
            for (const std::string& url : server.showcaseUrls) {
                featured->requestImage(url, true);
            }
            for (const FeaturedGame& game : server.games) {
                featured->requestImage(game.imageUrl, true);
            }
        }
    }
    for (const FeaturedServer& server : featuredList) {
        if (!server.creatorExperience()) {
            pinger.request(server.address);
        }
    }

    collectFeaturedImages();
    std::optional<std::string> focus = menu.focusedFeatured();
    std::set<std::string> wanted;
    for (const FeaturedServer& server : featuredList) {
        if (focus && server.id == *focus) {
            wanted.insert(server.showcaseUrls.begin(), server.showcaseUrls.end());
            for (const FeaturedGame& game : server.games) {
                wanted.insert(game.imageUrl);
            }
        }
    }
    for (auto it = shownShowcases.begin(); it != shownShowcases.end();) {
        if (wanted.count(*it)) {
            ++it;
            continue;
        }
        skin.clearDynamic(FeaturedSpritePrefix + *it);
        featuredImages.erase(*it);
        featuredDirty = true;
        it = shownShowcases.erase(it);
    }
    for (const std::string& url : wanted) {
        auto stored = featuredShowcases.find(url);
        if (stored == featuredShowcases.end() || shownShowcases.count(url)) {
            continue;
        }
        skin.setDynamic(FeaturedSpritePrefix + url, stored->second);
        featuredImages.insert(url);
        shownShowcases.insert(url);
        featuredDirty = true;
    }
    if (!featuredDirty) {
        return;
    }
    featuredDirty = false;

    auto sprite = [this](const std::string& url) {
        return featuredImages.count(url) > 0 ? FeaturedSpritePrefix + url : std::string();
    };
    std::vector<menu::FeaturedEntry> entries;
    entries.reserve(featuredList.size());
    for (const FeaturedServer& server : featuredList) {
        menu::FeaturedEntry& entry = entries.emplace_back();
        entry.id = server.id;
        entry.name = server.name;
        entry.creator = server.creator;
        entry.description = server.description;
        entry.newsTitle = server.newsTitle;
        entry.news = server.news;
        entry.address = server.address;
        entry.icon = sprite(server.iconUrl);
        entry.showcaseCount = server.showcaseUrls.size();
        for (const std::string& url : server.showcaseUrls) {
            if (std::string name = sprite(url); !name.empty()) {
                entry.showcase.push_back(std::move(name));
            }
        }
        for (const FeaturedGame& game : server.games) {
            entry.games.push_back({ game.title, game.subtitle, game.description, sprite(game.imageUrl) });
        }
    }
    menu.setFeatured(std::move(entries), !featuredListed);
}

void Client::syncAccount()
{
    AccountSnapshot snapshot = account.snapshot();
    // Wait for the saved sign in to come back, or an online server sees an offline player.
    if (pendingConnect && snapshot.state != AccountState::Connecting) {
        menu.connectTo(std::move(pendingConnect->name), std::move(pendingConnect->address));
        pendingConnect.reset();
    }
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
        if (snapshot.avatar.size() == size_t(ui::Font::ImageSlotSize) * ui::Font::ImageSlotSize * 4) {
            skin.setDynamic("dynamic/avatar", { ui::Font::ImageSlotSize, ui::Font::ImageSlotSize, std::move(snapshot.avatar) });
        } else {
            skin.clearDynamic("dynamic/avatar");
        }
    }
    if (snapshot.profile.revision != profileRevision) {
        profileRevision = snapshot.profile.revision;
        const PlayerProfile& loaded = snapshot.profile;
        for (const std::string& sprite : profileSprites) {
            skin.clearDynamic(sprite);
        }
        profileSprites.clear();
        profileInfo = menu::ProfileInfo {};
        profileInfo.achievementsLoaded = loaded.achievementsLoaded;
        profileInfo.achieved = loaded.achieved;
        profileInfo.total = loaded.total;
        profileInfo.gamerscore = loaded.gamerscore;
        profileInfo.totalGamerscore = loaded.totalGamerscore;
        profileInfo.statsLoaded = loaded.statsLoaded;
        profileInfo.minutesPlayed = loaded.minutesPlayed;
        profileInfo.blocksBroken = loaded.blocksBroken;
        profileInfo.mobsDefeated = loaded.mobsDefeated;
        profileInfo.distanceTravelled = loaded.distanceTravelled;
        auto convert = [this](const std::vector<Achievement>& source, const std::string& group, std::vector<menu::ProfileAchievement>& target) {
            for (size_t i = 0; i < source.size(); ++i) {
                const Achievement& achievement = source[i];
                menu::ProfileAchievement entry;
                entry.name = achievement.name;
                entry.description = achievement.description;
                entry.gamerscore = achievement.gamerscore;
                entry.achieved = achievement.achieved;
                if (!achievement.icon.empty()) {
                    entry.icon = "profile/" + group + "/" + std::to_string(i);
                    skin.setDynamic(entry.icon, { achievement.iconWidth, achievement.iconHeight, achievement.icon });
                    profileSprites.push_back(entry.icon);
                }
                target.push_back(std::move(entry));
            }
        };
        convert(loaded.suggested, "suggested", profileInfo.suggested);
        convert(loaded.recent, "recent", profileInfo.recent);
    }
    info.profile = profileInfo;
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
    std::string socialKey = snapshot.state == AccountState::SignedIn ? (snapshot.xuid.empty() ? std::string("signed-in") : snapshot.xuid) : std::string();
    if (socialKey != socialAccount) {
        socialAccount = socialKey;
        for (const std::string& sprite : socialSprites) {
            skin.clearDynamic(sprite);
        }
        socialSprites.clear();
        social.setLanguage(menu.language());
        social.setAccount(account.sharedAuthentication(), socialKey);
    }
    menu.setAccount(std::move(info));
}

void Client::syncSocial()
{
    if (menu.socialDrawerOpen()) {
        social.refreshFriendsIfStale(std::chrono::seconds(60));
    }
    if (social.revision() == socialRevision) {
        return;
    }
    socialRevision = social.revision();
    SocialSnapshot snapshot = social.snapshot();
    if (snapshot.realmsChanged != realmsChangedSeen) {
        realmsChangedSeen = snapshot.realmsChanged;
        if (snapshot.realmsChanged != 0) {
            account.refreshRealms();
        }
    }
    for (SocialAvatar& avatar : social.takeAvatars()) {
        if (avatar.pixels.size() != size_t(ui::Font::ImageSlotSize) * ui::Font::ImageSlotSize * 4) {
            continue;
        }
        std::string sprite = menu::socialAvatarSprite(avatar.xuid);
        skin.setDynamic(sprite, { ui::Font::ImageSlotSize, ui::Font::ImageSlotSize, std::move(avatar.pixels) });
        if (std::find(socialSprites.begin(), socialSprites.end(), sprite) == socialSprites.end()) {
            socialSprites.push_back(sprite);
        }
    }
    menu.setSocial(std::move(snapshot));
}

// Keep enough logical space for the interface at every scale and window size.
float Client::guiScale() const
{
    return menu::effectiveGuiScale(static_cast<float>(window->width()), static_cast<float>(window->height()),
        menu.option("gui_scale", 0), visuals.interfaceScale.value_or(menu.interfaceScale()));
}

/**
 * Loads the dressing room pages the menu asks for and hands it every page
 * that changed, the thumbnails put in the skin as sprites.
 */
void Client::syncDressingRoom()
{
    for (const std::string& page : menu.takeDressingRequests()) {
        dressingCatalog.request(page, account.signedInAuthentication());
    }
    std::map<std::string, DressingPage> pages = dressingCatalog.snapshot(dressingRevisions);
    bool changed = false;
    for (const auto& [id, page] : pages) {
        uint64_t& seen = dressingRevisions[id];
        if (seen == page.revision) {
            continue;
        }
        seen = page.revision;
        changed = true;
        for (const std::string& sprite : dressingSprites[id]) {
            skin.clearDynamic(sprite);
        }
        dressingSprites[id].clear();
    }
    if (!changed) {
        return;
    }
    menu::DressingRoomView view;
    for (const auto& [id, page] : pages) {
        menu::DressingPageView& target = view[id];
        target.loading = page.loading;
        target.loaded = page.loaded;
        target.error = page.error;
        target.balance = page.balance;
        auto convert = [&](const std::vector<DressingItem>& items, std::vector<menu::DressingPiece>& out, bool owned) {
            for (const DressingItem& item : items) {
                menu::DressingPiece piece { item.id, item.title, item.rarity, item.creator, owned, {}, item.packType, item.coins, item.bonus, item.header, item.coinText, item.footer };
                if (!item.thumbnail.empty()) {
                    piece.sprite = "dressing/" + id + "/" + item.id;
                    if (std::find(dressingSprites[id].begin(), dressingSprites[id].end(), piece.sprite) == dressingSprites[id].end()) {
                        skin.setDynamic(piece.sprite, { item.thumbnailWidth, item.thumbnailHeight, item.thumbnail });
                        dressingSprites[id].push_back(piece.sprite);
                    }
                }
                out.push_back(std::move(piece));
            }
        };
        convert(page.owned, target.owned, true);
        convert(page.others, target.others, false);
    }
    menu.setDressingRoom(std::move(view));
}

void Client::uploadAtlas(bool fontChanged)
{
    constexpr uint32_t size = ui::Skin::AtlasSize;
    bool initial = atlasPixels.empty();
    if (initial) atlasPixels.resize(static_cast<size_t>(size) * size * 4, 0);
    std::vector<ui::ImageRegion> regions;
    const std::vector<uint8_t>& coverage = font.coverage();
    if (initial || fontChanged) {
        for (size_t i = 0; i < coverage.size(); ++i) {
            atlasPixels[i * 4 + 0] = 255;
            atlasPixels[i * 4 + 1] = 255;
            atlasPixels[i * 4 + 2] = 255;
            atlasPixels[i * 4 + 3] = coverage[i];
        }
        regions.push_back({ 0, 0, size, static_cast<uint32_t>(coverage.size() / size) });
    }
    auto sprites = skin.pack(atlasPixels);
    regions.insert(regions.end(), sprites.begin(), sprites.end());
    if (initial) renderer->uploadUiAtlas(atlasPixels.data(), size, size);
    else if (!regions.empty()) renderer->updateUiAtlas(atlasPixels.data(), size, size, regions);
}

void Client::applyMeshUpdate(const MeshUpdate& update)
{
    const world::SubChunkKey& key = update.key;
    uint64_t id = subChunkId(key);
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
        litChunks[id] = update.mesh;
    } else {
        renderer->removeChunkMesh(id);
        opaqueChunks.erase(id);
        litChunks.erase(id);
    }
}

void Client::applyMeshUpdates()
{
    const auto removalDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
    for (size_t count = 0; count < 128; ++count) {
        if (count && std::chrono::steady_clock::now() >= removalDeadline) break;
        auto updates = session.takeMeshUpdates(1, blockAssets.get(), MeshUpdateKind::Removal);
        if (updates.empty()) break;
        applyMeshUpdate(updates.front());
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    size_t uploadedBytes = 0;
    for (size_t count = 0; count < 32; ++count) {
        if (count && (std::chrono::steady_clock::now() >= deadline || uploadedBytes >= 4 * 1024 * 1024)) break;
        auto updates = session.takeMeshUpdates(1, blockAssets.get(), MeshUpdateKind::Terrain);
        if (updates.empty()) break;
        const MeshUpdate& update = updates.front();
        if (update.mesh) uploadedBytes += (update.mesh->cubes.size() + update.mesh->translucentCubes.size()) * sizeof(world::PackedQuad)
            + (update.mesh->models.size() + update.mesh->translucentModels.size()) * sizeof(world::ModelQuadGpu);
        applyMeshUpdate(update);
    }
}

/**
 * Block and sky light of the cell holding the point, as the mesher solved it.
 * Sub-chunks without a mesh never had their light solved; those are mostly
 * open air, so they count as open sky.
 */
uint8_t Client::lightAt(double x, double y, double z) const
{
    int32_t blockX = static_cast<int32_t>(std::floor(x));
    int32_t blockY = static_cast<int32_t>(std::floor(y));
    int32_t blockZ = static_cast<int32_t>(std::floor(z));
    world::SubChunkKey key { timeState.dimension, blockX >> 4, blockY >> 4, blockZ >> 4 };
    auto found = litChunks.find(subChunkId(key));
    if (found == litChunks.end() || found->second->light.empty()) {
        return OpenSkyLight;
    }
    return found->second->light[world::linearIndex(uint32_t(blockX & 15), uint32_t(blockY & 15), uint32_t(blockZ & 15))];
}

/**
 * The light an entity standing at the point takes: the cell at the feet and
 * the one above, whichever is brighter per channel, so a mob half sunk in a
 * slab or snow layer does not go black. Packed for all four quad corners.
 */
uint32_t Client::lightCorners(double x, double y, double z) const
{
    uint8_t feet = lightAt(x, y, z);
    uint8_t head = lightAt(x, y + 1.0, z);
    uint32_t level = uint32_t(std::max(feet & 15, head & 15)) | (uint32_t(std::max(feet >> 4, head >> 4)) << 4);
    return level * 0x01010101u;
}

/**
 * Lights the quads appended since first. Additive quads like spider eyes
 * stay bright. Block textured quads, like a block in hand, are always shaded
 * by their light and keep their tint in the upper bits, so only entity quads
 * get the shaded flag.
 */
void Client::lightQuads(std::vector<world::ModelQuadGpu>& quads, size_t first, uint32_t corners) const
{
    for (size_t index = first; index < quads.size(); ++index) {
        world::ModelQuadGpu& quad = quads[index];
        if (!(quad.words[11] & EntityQuadFlag)) {
            quad.words[12] = corners;
            continue;
        }
        if (quad.words[11] & AdditiveQuadFlag) {
            continue;
        }
        quad.words[11] |= ShadedQuadFlag;
        quad.words[12] = corners;
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
    bool local = snapshot.localTerrainReady;
    bool dense = visibleTerrain() >= MinVisibleTerrain;
    bool bounded = snapshot.cohortComplete && snapshot.world.pendingSubChunks == 0 && snapshot.meshJobs == 0 && !snapshot.updatesPending;
    if (!local && !dense && !bounded) {
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
    terrainReleased = local || (dense && completed.opaqueChunks != 0) || bounded;
    if (terrainReleased) {
        debugLog(std::string("terrain released, ") + (local ? "local terrain" : dense ? "dense view" : "bounded view") + ", gpu opaque chunks " + std::to_string(completed.opaqueChunks));
    }
    return terrainReleased;
}

void Client::syncSession()
{
    auto published = session.sharedSnapshot();
    const SessionSnapshot& snapshot = *published;
    if (snapshot.state != SessionState::Joined || cameraSessionJoin != snapshot.joinCount || cameraDimension != snapshot.dimension) {
        serverCamera.reset(snapshot.state == SessionState::Joined && cameraSessionJoin == snapshot.joinCount);
        submergedSeconds = 0.0f;
        localSwimAmount = 0.0f;
        serverFov = {};
        serverFovSerial = 0;
        serverBoomFraction = 0.0;
        cameraSessionJoin = snapshot.joinCount;
        cameraDimension = snapshot.dimension;
    }
    cameraLocalUnique = snapshot.localUniqueActorId;
    bool changed = published != seenSessionSnapshot;
    seenSessionSnapshot = std::move(published);
    if (agentSession) {
        agentSession->observe(snapshot);
    }
    mods->observe(snapshot);
    playerView = snapshot.state == SessionState::Joined ? snapshot.player : PlayerView {};
    if (menu.debugVisible()) {
        menu.setDebugView(buildDebugView(snapshot));
    }
    updateAudio(snapshot);
    static const std::vector<std::shared_ptr<const world::PackFiles>> noPacks;
    applyServerPacks(snapshot.state == SessionState::Joined ? snapshot.packs : globalResources.packs());
    const std::vector<uint8_t>* wantedTitle = snapshot.titleImage.get();
    if (wantedTitle != shownTitle.get()) {
        shownTitle = snapshot.titleImage;
        if (shownTitle && shownTitle->size() == size_t(ui::Font::TitleWidth) * ui::Font::TitleHeight * 4) {
            skin.setDynamic("dynamic/title", { ui::Font::TitleWidth, ui::Font::TitleHeight, *shownTitle });
        } else {
            skin.clearDynamic("dynamic/title");
        }
    }
    if (snapshot.state == SessionState::Joined && snapshot.joinCount != seenJoin) {
        seenJoin = snapshot.joinCount;
        seenTeleport = snapshot.teleportCount;
        menu.clearChat();
        popupMessage = {};
        tipMessage = {};
        actionbarMessage = {};
        titleView = {};
        gameTip = {};
        renderer->clearChunkMeshes();
        opaqueChunks.clear();
        litChunks.clear();
        terrainReleased = false;
        readinessFrame.reset();
        camera.placeAt(snapshot.spawnX, snapshot.spawnY, snapshot.spawnZ, snapshot.spawnYaw, snapshot.spawnPitch);
    }
    if (snapshot.state == SessionState::Joined && snapshot.assets && snapshot.assets != blockAssets) {
        renderer->clearChunkMeshes();
        opaqueChunks.clear();
        litChunks.clear();
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
        std::vector<uint8_t> entityPixels = assets->entityTexturePixels();
        entityPixels.resize(entityPixels.size() + size_t(world::SkinPoolLayers + HeldItemTextureSlots + world::DroppedIconSlots + 1) * world::EntityTextureSize * world::EntityTextureSize * 4, 0);
        uint32_t mapBackground = assets->entityTextureLayers() + world::SkinPoolLayers + HeldItemTextureSlots + world::DroppedIconSlots;
        loadMapArt(snapshot.packs, entityPixels.data() + size_t(mapBackground) * world::EntityTextureSize * world::EntityTextureSize * 4);
        uint32_t particleBase = mapBackground + 1;
        uint32_t particleLayers = loadParticles(snapshot.packs, particleBase, entityPixels);
        renderer->uploadEntityTextures(entityPixels.data(), world::EntityTextureSize, particleBase + particleLayers);
        heldMeshes.clear();
        actorGeometry.clear();
        droppedMeshes.clear();
        droppedIconKeys = {};
        nextDroppedIcon = 0;
        lastHeldIdentity.clear();
        handItem = {};
        handUpdatedAt = 0.0;
        handEquip = 0.0f;
        handRestAnimator = world::EntityAnimator();
        paperDollAnimator = world::EntityAnimator();
        handAnimator = world::EntityAnimator();
        for (const auto& [layer, pixels] : skinPixels) {
            renderer->updateEntityTexture(assets->skinLayerBase() + layer, pixels.data());
        }
        if (blockAssets != assets) {
            partMatches.clear();
            armorBoneMatches.clear();
            animators.clear();
            actorPoses.clear();
            for (const auto& [name, rendered] : itemIcons) {
                skin.clearDynamic(name);
            }
            itemIcons.clear();
            debugLog("item textures " + std::to_string(assets->itemTextureCount()));
        }
        blockAssets = assets;
        if (snapshot.reloadedMeshes) {
            for (const auto& update : *snapshot.reloadedMeshes) applyMeshUpdate(update);
            session.acknowledgeResourceReload(snapshot.resourceReloadSerial, snapshot.assets.get());
        }
    }
    if (snapshot.state == SessionState::Joined && snapshot.teleportCount != seenTeleport) {
        seenTeleport = snapshot.teleportCount;
        camera.placeAt(snapshot.spawnX, snapshot.spawnY, snapshot.spawnZ, snapshot.spawnYaw, snapshot.spawnPitch);
    }
    if (snapshot.state != SessionState::Joined && seenJoin != 0 && worldShown) {
        renderer->clearChunkMeshes();
        opaqueChunks.clear();
        litChunks.clear();
    }
    if (snapshot.state != SessionState::Joined) {
        submergedSeconds = 0.0f;
        localSwimAmount = 0.0f;
        swimAmounts.clear();
        blockParticles.clear();
        clearParticles();
        menu.inventoryPanel().reset();
        menu.formPanel().reset();
        terrainReleased = false;
        readinessFrame.reset();
    }
    worldShown = snapshot.state == SessionState::Joined;
    timeState.worldTime = snapshot.worldTime;
    timeState.worldTimeStamp = snapshot.worldTimeStamp;
    timeState.worldClockPaused = snapshot.worldClockPaused;
    timeState.rainLevel = snapshot.rainLevel;
    timeState.thunderLevel = snapshot.thunderLevel;
    timeState.fogStack = snapshot.fogStack;
    timeState.cameraMedium = snapshot.cameraMedium;
    timeState.dimension = snapshot.dimension;
    if (changed) {
        actorViews = snapshot.actors;
    } else {
        actorViews.resize(snapshot.actors.size());
        for (size_t index = 0; index < actorViews.size(); ++index) {
            const ActorView& source = snapshot.actors[index];
            ActorView& target = actorViews[index];
            target.x = source.x;
            target.y = source.y;
            target.z = source.z;
            target.yaw = source.yaw;
            target.headYaw = source.headYaw;
            target.pitch = source.pitch;
        }
    }
    localRuntime = snapshot.localRuntimeId;
    localSkinSlot = snapshot.localSkinSlot;
    cameraFovRequest = snapshot.state == SessionState::Joined ? snapshot.cameraFov : CameraFovRequest {};
    boomFraction = snapshot.boomFraction;
    serverBoomFraction = snapshot.serverBoomFraction;
    localSlim = snapshot.localSlim;
    if (changed) {
        menu.setCommands(snapshot.commands);
        menu.setPlayers(snapshot.players);
    }
    menu.setOperatorCommands(snapshot.player.operatorCommands);
    if (snapshot.hud.lastSwing > hudState.lastSwing) {
        startSwing(secondsNow());
    }
    if (changed) {
        hudState = snapshot.hud;
        sidebarView = snapshot.sidebar;
        selectionView = snapshot.selection;
        crackViews = snapshot.cracks;
        chestLidViews = snapshot.chestLids;
        frameItemViews = snapshot.frameItems;
    }
    ridingView = snapshot.state == SessionState::Joined ? snapshot.riding : std::string();
    targetBlockName = snapshot.targetBlock ? snapshot.targetBlock->name : std::string();
    for (const ParticleBurst& burst : session.takeParticleBursts()) {
        blockParticles.spawn(burst);
    }
    takeSessionParticles(snapshot);
    bool skinsChanged = false;
    for (SkinUpload& skin : session.takeSkinUploads()) {
        releaseSkinLayers(skin.slot);
        SkinView view;
        view.base = placeSkinTexture(skin.slot, skin.image, world::MaxSkinSide / world::EntityTextureSize);
        view.cape = placeSkinTexture(skin.slot, skin.cape, 1);
        for (SkinAnimationUpload& animation : skin.animations) {
            SkinAnimationView placed;
            placed.texture = placeSkinTexture(skin.slot, animation.image, world::MaxEntityTiles);
            placed.rig = std::move(animation.rig);
            placed.kind = animation.kind;
            placed.frames = std::max<uint32_t>(animation.frames, 1);
            placed.blinking = animation.blinking;
            if (placed.texture.present && placed.rig) {
                view.animations.push_back(std::move(placed));
            }
        }
        skinViews[skin.slot] = std::move(view);
        if (skin.slot == localSkinSlot && !skin.image.empty()) {
            this->skin.setDynamic("dynamic/inventory_skin", ui::shrinkBitmap({ skin.image.width, skin.image.height, skin.image.pixels }, 64));
        }
        if (auto previousRig = skinRigs.find(skin.slot); previousRig != skinRigs.end()) actorGeometry.erase(previousRig->second.get());
        skinRigs[skin.slot] = std::move(skin.rig);
        skinsChanged = true;
    }
    if (skinsChanged) {
        actorPoses.clear();
        partMatches.clear();
        armorBoneMatches.clear();
    }
    timeState.daylightCycle = snapshot.daylightCycle;
    timeState.chunkRadius = snapshot.chunkRadius;
    // a mod's time and weather only change what this client shows
    if (visuals.time) {
        timeState.worldTime = *visuals.time;
        timeState.daylightCycle = false;
    }
    if (visuals.rain) {
        timeState.rainLevel = *visuals.rain;
    }
    if (visuals.thunder) {
        timeState.thunderLevel = *visuals.thunder;
    }
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
    info.name = snapshot.name;
    info.displayName = snapshot.displayName;
    info.levelName = snapshot.levelName;
    info.gameMode = snapshot.gameMode;
    info.position = snapshot.position;
    if (playerView.active) {
        info.playerBlock = std::array<int, 3> {
            static_cast<int>(std::floor(playerView.current[0])),
            static_cast<int>(std::floor(playerView.current[1])),
            static_cast<int>(std::floor(playerView.current[2])),
        };
    }
    if (selectionView) {
        info.facingBlock = std::array<int, 3> { selectionView->cell[0], selectionView->cell[1], selectionView->cell[2] };
    }
    info.dimension = snapshot.dimension;
    info.chunkRadius = snapshot.chunkRadius;
    info.packetsReceived = snapshot.packetsReceived;
    info.columns = snapshot.world.columns;
    info.subChunks = snapshot.world.subChunks;
    info.pendingSubChunks = snapshot.world.pendingSubChunks;
    info.blockUpdates = snapshot.world.blockUpdates;
    info.worldErrors = snapshot.world.decodeErrors;
    info.lastWorldError = snapshot.world.lastError;
    info.meshes = snapshot.meshes;
    info.meshQuads = snapshot.meshQuads;
    info.meshJobs = snapshot.meshJobs;
    info.textureLayers = snapshot.textureLayers;
    info.materials = snapshot.materials;
    info.diagnosticVisuals = snapshot.diagnosticVisuals;
    info.assetsError = snapshot.assetsError;
    info.packPrompt = snapshot.packPrompt;
    info.packCount = snapshot.packCount;
    info.packSkippable = snapshot.packSkippable;
    info.packBytes = snapshot.packBytes;
    info.packDownloading = snapshot.packDownloading;
    info.packReceived = snapshot.packReceived;
    info.packTotal = snapshot.packTotal;
    info.packsResolved = snapshot.packsResolved;
    info.error = snapshot.error;
    info.packetError = snapshot.packetError;
    info.dead = snapshot.dead && snapshot.state == SessionState::Joined;
    info.changingDimension = snapshot.changingDimension && snapshot.state == SessionState::Joined;
    if (info.dead && !snapshot.deathCause.empty()) {
        info.deathMessage = ui::Localization::shared().translateMessage(snapshot.deathCause, snapshot.deathParameters);
    }
    menu.setSession(std::move(info));
}

}
