#include "client/Client.h"
#include "client/DebugLog.h"

#include "platform/Paths.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/Theme.h"

#include "client/LocalWorlds.h"
#include "client/Sky.h"

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
constexpr double MaxActorDistance = 120.0;
constexpr uint32_t EntityQuadFlag = 1u << 5;
constexpr uint32_t FullSkyLight = 0xF0F0F0F0u;
/**
 * The durability of vanilla tools, weapons and armor, or 0 for items that do
 * not wear out.
 */
int32_t itemMaxDurability(const std::string& identifier)
{
    static const std::pair<const char*, int32_t> Exact[] = {
        { "minecraft:bow", 384 },
        { "minecraft:crossbow", 464 },
        { "minecraft:trident", 250 },
        { "minecraft:shield", 336 },
        { "minecraft:fishing_rod", 384 },
        { "minecraft:flint_and_steel", 64 },
        { "minecraft:shears", 238 },
        { "minecraft:elytra", 432 },
        { "minecraft:carrot_on_a_stick", 25 },
        { "minecraft:warped_fungus_on_a_stick", 100 },
        { "minecraft:turtle_helmet", 275 },
        { "minecraft:mace", 500 },
        { "minecraft:brush", 64 },
        { "minecraft:wolf_armor", 64 },
    };
    for (const auto& [name, value] : Exact) {
        if (identifier == name) {
            return value;
        }
    }
    static const std::pair<const char*, int32_t> ToolMaterials[] = {
        { "minecraft:wooden_", 59 },
        { "minecraft:stone_", 131 },
        { "minecraft:iron_", 250 },
        { "minecraft:golden_", 32 },
        { "minecraft:diamond_", 1561 },
        { "minecraft:netherite_", 2031 },
        { "minecraft:copper_", 190 },
    };
    static const char* ToolKinds[] = { "sword", "pickaxe", "axe", "shovel", "hoe", "spear" };
    for (const auto& [prefix, value] : ToolMaterials) {
        if (identifier.rfind(prefix, 0) != 0) {
            continue;
        }
        std::string kind = identifier.substr(std::char_traits<char>::length(prefix));
        for (const char* tool : ToolKinds) {
            if (kind == tool) {
                return value;
            }
        }
    }
    static const std::pair<const char*, int32_t> ArmorMaterials[] = {
        { "minecraft:leather_", 5 },
        { "minecraft:chainmail_", 15 },
        { "minecraft:iron_", 15 },
        { "minecraft:golden_", 7 },
        { "minecraft:diamond_", 33 },
        { "minecraft:netherite_", 37 },
        { "minecraft:copper_", 11 },
    };
    static const std::pair<const char*, int32_t> ArmorPieces[] = {
        { "helmet", 11 },
        { "chestplate", 16 },
        { "leggings", 15 },
        { "boots", 13 },
    };
    for (const auto& [prefix, factor] : ArmorMaterials) {
        if (identifier.rfind(prefix, 0) != 0) {
            continue;
        }
        std::string kind = identifier.substr(std::char_traits<char>::length(prefix));
        for (const auto& [piece, base] : ArmorPieces) {
            if (kind == piece) {
                return base * factor;
            }
        }
    }
    return 0;
}

/**
 * The armor points one vanilla armor piece adds to the armor bar.
 */
int32_t itemArmorPoints(const std::string& identifier)
{
    static const std::pair<const char*, std::array<int32_t, 4>> Materials[] = {
        { "minecraft:leather_", { 1, 3, 2, 1 } },
        { "minecraft:chainmail_", { 2, 5, 4, 1 } },
        { "minecraft:iron_", { 2, 6, 5, 2 } },
        { "minecraft:golden_", { 2, 5, 3, 1 } },
        { "minecraft:diamond_", { 3, 8, 6, 3 } },
        { "minecraft:netherite_", { 3, 8, 6, 3 } },
        { "minecraft:copper_", { 2, 4, 3, 1 } },
    };
    static const char* Pieces[] = { "helmet", "chestplate", "leggings", "boots" };
    if (identifier == "minecraft:turtle_helmet") {
        return 2;
    }
    for (const auto& [prefix, points] : Materials) {
        if (identifier.rfind(prefix, 0) != 0) {
            continue;
        }
        std::string kind = identifier.substr(std::char_traits<char>::length(prefix));
        for (size_t piece = 0; piece < 4; ++piece) {
            if (kind == Pieces[piece]) {
                return points[piece];
            }
        }
    }
    return 0;
}

/**
 * A readable name for an item without a custom name: the identifier without
 * its namespace, words capitalized.
 */
std::string itemDisplayName(const std::string& identifier)
{
    std::string name = identifier.substr(identifier.find(':') == std::string::npos ? 0 : identifier.find(':') + 1);
    bool capital = true;
    for (char& c : name) {
        if (c == '_') {
            c = ' ';
            capital = true;
        } else if (capital) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            capital = false;
        }
    }
    return name;
}

int16_t roundToShort(float value)
{
    return static_cast<int16_t>(value >= 0.0f ? static_cast<int32_t>(value + 0.5f) : static_cast<int32_t>(value - 0.5f));
}

float wrapDegrees(float degrees)
{
    float wrapped = std::fmod(degrees + 180.0f, 360.0f);
    return (wrapped < 0.0f ? wrapped + 360.0f : wrapped) - 180.0f;
}

/**
 * Case insensitive match of a bone name against a lowercase pattern where '*'
 * stands for any run of characters.
 */
bool matchesPattern(const std::string& pattern, const std::string& name)
{
    size_t p = 0;
    size_t n = 0;
    size_t star = std::string::npos;
    size_t resume = 0;
    while (n < name.size()) {
        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(name[n])));
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            resume = n;
        } else if (p < pattern.size() && pattern[p] == c) {
            ++p;
            ++n;
        } else if (star != std::string::npos) {
            p = star + 1;
            n = ++resume;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

/**
 * The choice an entity's selector expression lands on, clamped into the
 * choice list; NoEntityChoice when there is none.
 */
uint32_t pickChoice(world::EntityAnimator& animator, const world::molang::Script& selector, const std::vector<uint32_t>& choices)
{
    if (choices.empty()) {
        return world::NoEntityChoice;
    }
    double value = animator.evaluate(selector);
    size_t index = std::isfinite(value) && value > 0.0 ? std::min(static_cast<size_t>(value), choices.size() - 1) : 0;
    return choices[index];
}

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
    if (!font.load(assets, skin)) {
        throw std::runtime_error("Kestrel draws its menus with the installed game's fonts and textures, install Minecraft Bedrock or set KESTREL_VANILLA_PACK");
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
        if (window->consumeResize()) {
            Profiler::Section section(profiler, "resize");
            renderer->resize(window->width(), window->height());
        }

        if (int limit = menu.maxFps(); limit != menu::UnlimitedFps) {
            Profiler::Section section(profiler, "fps cap wait");
            std::this_thread::sleep_until(lastFrame + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / limit)));
        }
        auto now = std::chrono::steady_clock::now();
        float deltaSeconds = std::min(std::chrono::duration<float>(now - lastFrame).count(), 0.1f);
        lastFrame = now;

        {
            Profiler::Section section(profiler, "account");
            syncAccount();
        }
        {
            Profiler::Section section(profiler, "session sync");
            session.setRenderDistance(menu.renderDistance());
            syncSession();
        }
        {
            Profiler::Section section(profiler, "mesh upload");
            applyMeshUpdates();
        }

        {
            Profiler::Section section(profiler, "camera");
            bool captured = menu.capturesMouse();
            window->setMouseCaptured(captured);
            camera.setBaseFov(static_cast<float>(menu.fov()));
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
                }
                input.yaw = camera.minecraftYaw();
                input.pitch = camera.minecraftPitch();
                session.setMotionInput(input);
                float fovTarget = playerView.flying ? 1.1f : 1.0f;
                fovTarget *= (playerView.movementSpeed / 0.1f + 1.0f) * 0.5f;
                fovTarget = std::clamp(fovTarget, 0.1f, 1.5f);
                camera.easeFov(fovTarget, deltaSeconds);
                double blend = std::clamp((secondsNow() - playerView.tickTime) / 0.05, 0.0, 1.0);
                double eye = playerView.sneaking ? 1.54 : 1.62;
                camera.setPosition(playerView.previous[0] + (playerView.current[0] - playerView.previous[0]) * blend,
                    playerView.previous[1] + (playerView.current[1] - playerView.previous[1]) * blend + eye,
                    playerView.previous[2] + (playerView.current[2] - playerView.previous[2]) * blend);
            } else {
                camera.easeFov(1.0f, deltaSeconds);
                camera.update(window->input(), menu.keyBindings(), deltaSeconds, captured);
            }
            char cameraText[96];
            std::snprintf(cameraText, sizeof(cameraText), "Camera %.1f, %.1f, %.1f", camera.x(), camera.y(), camera.z());
            menu.setCameraInfo(cameraText);
            session.setLookRay({ camera.x(), camera.y(), camera.z() }, camera.forward());
        }
        {
            Profiler::Section section(profiler, "hud");
            handleHotbarInput();
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
                    entry.maxPlayers = ping.maxPlayers;
                }
                menu.setServerStatus(std::move(status));
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

        drawList.reset(scale, font.whiteU(), font.whiteV());
        ui::Context context(drawList, font, skin, window->input(), widgets, scale);
        {
            Profiler::Section section(profiler, "menu ui");
            menu.frame(context, window->width() / scale, window->height() / scale);
            context.endFrame();
        }
        if (rebaked || skin.dirty()) {
            Profiler::Section section(profiler, "ui atlas");
            uploadAtlas();
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
        if (menu.interfaceScale() != savedScale || !(menu.keyBindings() == savedBindings) || menu.renderDistance() != savedRenderDistance || menu.maxFps() != savedMaxFps || menu.fov() != savedFov) {
            saveSettings();
        }
        if (menu.quitRequested()) {
            break;
        }

        if (menu.worldVisible() || (worldShown && !terrainReleased)) {
            float renderDistance = static_cast<float>(std::max(timeState.chunkRadius, 4) * 16);
            SkyFrame sky;
            std::vector<SkyVertex> background;
            {
                Profiler::Section section(profiler, "sky");
                sky = submergedIn(atmosphereAt(currentWorldTime(timeState), renderDistance, timeState.rainLevel, timeState.thunderLevel), timeState.cameraMedium);
                if (blockAssets && timeState.cameraMedium == 0) {
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
            view.fogColor = sky.fogColor;
            view.fogStart = sky.fogStart;
            view.fogEnd = sky.fogEnd;
            view.daylight = sky.daylight;
            view.nightVision = nightVisionStrength();
            view.sunDirection = sky.sunDirection;
            view.background = background.data();
            view.backgroundCount = static_cast<uint32_t>(background.size());
            std::array<int32_t, 3> entityOrigin { int32_t(std::floor(camera.x())), int32_t(std::floor(camera.y())), int32_t(std::floor(camera.z())) };
            std::vector<world::ModelQuadGpu> entityQuads;
            {
                Profiler::Section section(profiler, "entities");
                interpolateActors(secondsNow());
                entityQuads = buildActorQuads(entityOrigin);
            }
            view.entityQuads = entityQuads.data();
            view.entityQuadCount = static_cast<uint32_t>(entityQuads.size());
            view.entityOrigin = { float(entityOrigin[0] - camera.x()), float(entityOrigin[1] - camera.y()), float(entityOrigin[2] - camera.z()) };
            Profiler::Section section(profiler, "draw world");
            renderer->drawWorld(view);
        } else {
            Profiler::Section section(profiler, "begin frame");
            renderer->beginFrame(canvas.r / 255.0f, canvas.g / 255.0f, canvas.b / 255.0f);
        }
        {
            Profiler::Section section(profiler, "draw ui");
            renderer->drawUi(drawList);
        }
        {
            Profiler::Section section(profiler, "present gpu");
            renderer->endFrame();
        }
        profiler.endFrame();
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
        if (snapshot.avatar.size() == size_t(ui::Font::ImageSlotSize) * ui::Font::ImageSlotSize * 4) {
            skin.setDynamic("dynamic/avatar", { ui::Font::ImageSlotSize, ui::Font::ImageSlotSize, std::move(snapshot.avatar) });
        } else {
            skin.clearDynamic("dynamic/avatar");
        }
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

// Mirrors the game's automatic GUI scale, one menu unit being a whole number of pixels.
float Client::guiScale() const
{
    float byHeight = std::floor(static_cast<float>(window->height()) / 360.0f);
    float byWidth = std::floor(static_cast<float>(window->width()) / 660.0f);
    float automatic = std::max(1.0f, std::min(byHeight, byWidth));
    return std::max(1.0f, std::round(automatic * menu.interfaceScale()));
}

void Client::uploadAtlas()
{
    constexpr uint32_t size = ui::Skin::AtlasSize;
    atlasPixels.assign(static_cast<size_t>(size) * size * 4, 0);
    const std::vector<uint8_t>& coverage = font.coverage();
    for (size_t i = 0; i < coverage.size(); ++i) {
        atlasPixels[i * 4 + 0] = 255;
        atlasPixels[i * 4 + 1] = 255;
        atlasPixels[i * 4 + 2] = 255;
        atlasPixels[i * 4 + 3] = coverage[i];
    }
    skin.pack(atlasPixels);
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
 * The quads of every entity close enough to the camera, placed around origin
 * in 1/256 block: every bone posed by the entity's animations, then the model
 * scaled and turned to its body yaw. Entity quads set bit 5 of the shade word
 * so they sample the entity textures.
 */
std::vector<world::ModelQuadGpu> Client::buildActorQuads(const std::array<int32_t, 3>& origin)
{
    std::vector<world::ModelQuadGpu> out;
    if (!blockAssets) {
        animators.clear();
        return out;
    }
    double now = secondsNow();
    double worldTime = currentWorldTime(timeState);
    ++actorFrame;
    WorldView cullView;
    float aspect = static_cast<float>(window->width()) / static_cast<float>(std::max<uint32_t>(window->height(), 1));
    cullView.viewProjection = camera.viewProjection(aspect);
    cullView.cameraX = camera.x();
    cullView.cameraY = camera.y();
    cullView.cameraZ = camera.z();
    ChunkFrustum frustum(cullView);
    std::unordered_set<uint64_t> present;
    for (const ActorView& actor : actorViews) {
        present.insert(actor.runtimeId);
        double dx = actor.x - origin[0];
        double dy = actor.y - origin[1];
        double dz = actor.z - origin[2];
        if (std::abs(dx) > MaxActorDistance || std::abs(dy) > MaxActorDistance || std::abs(dz) > MaxActorDistance) {
            continue;
        }
        int32_t blockX = static_cast<int32_t>(std::floor(actor.x));
        int32_t blockY = static_cast<int32_t>(std::floor(actor.y));
        int32_t blockZ = static_cast<int32_t>(std::floor(actor.z));
        if (!frustum.contains(cullView, blockX - 8, blockY - 7, blockZ - 8)) {
            continue;
        }
        const world::EntityModel* model = actor.slim ? blockAssets->entityModel(actor.identifier + "#slim") : nullptr;
        if (!model) {
            model = blockAssets->entityModel(actor.identifier);
        }
        if (!model) {
            continue;
        }
        world::EntityAnimator& animator = animators[actor.runtimeId];
        world::AnimationInput input;
        input.x = actor.x;
        input.y = actor.y;
        input.z = actor.z;
        input.yaw = actor.yaw;
        input.headYaw = actor.headYaw;
        input.pitch = actor.pitch;
        input.now = now;
        input.worldTime = worldTime;
        input.flags = actor.flags;
        input.variant = actor.variant;
        input.markVariant = actor.markVariant;
        input.color = actor.color;
        input.skinId = actor.skinId;
        input.identifier = actor.identifier;
        input.name = actor.name;
        input.onGround = actor.onGround;
        if (model->rigs.empty()) {
            continue;
        }
        const world::EntityRenderController* controller = nullptr;
        for (const world::EntityRenderController& candidate : model->controllers) {
            if (candidate.condition.empty() || animator.evaluate(candidate.condition) != 0.0) {
                controller = &candidate;
                break;
            }
        }
        uint32_t rigIndex = controller ? pickChoice(animator, controller->geometry, controller->geometryChoices) : 0;
        const world::EntityRig* chosenRig = &model->rigs[rigIndex < model->rigs.size() ? rigIndex : 0];
        if (actor.skinSlot != NoSkin) {
            if (auto skinRig = skinRigs.find(actor.skinSlot); skinRig != skinRigs.end() && skinRig->second) {
                chosenRig = skinRig->second.get();
            }
        }
        const world::EntityRig& rig = *chosenRig;
        double cameraDistance = std::sqrt((actor.x - camera.x()) * (actor.x - camera.x()) + (actor.y - camera.y()) * (actor.y - camera.y()) + (actor.z - camera.z()) * (actor.z - camera.z()));
        uint64_t interval = cameraDistance < 16.0 ? 1 : cameraDistance < 32.0 ? 2 : cameraDistance < 64.0 ? 4 : 8;
        bool stale = animator.matrices().size() != rig.bones.size();
        if (stale || (actorFrame + actor.runtimeId) % interval == 0) {
            Profiler::Section section(profiler, "  animation");
            animator.update(model->scripts.get(), &blockAssets->animationLibrary(), rig.bones, input);
        }
        const std::vector<world::BoneMatrix>& matrices = animator.matrices();
        float scale = animator.scale() * actor.scale * 16.0f;
        uint32_t layer = model->layer;
        std::vector<uint8_t> hidden;
        if (controller) {
            uint32_t chosen = pickChoice(animator, controller->texture, controller->textureChoices);
            if (chosen != world::NoEntityChoice) {
                layer = chosen;
            }
            if (!controller->parts.empty()) {
                std::vector<uint8_t> visible(controller->parts.size(), 1);
                for (size_t rule = 0; rule < controller->parts.size(); ++rule) {
                    visible[rule] = animator.evaluate(controller->parts[rule].visible) != 0.0 ? 1 : 0;
                }
                std::vector<int32_t>& lastRule = partMatches[{ controller, &rig }];
                if (lastRule.size() != rig.bones.size()) {
                    lastRule.assign(rig.bones.size(), -1);
                    for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                        for (size_t rule = 0; rule < controller->parts.size(); ++rule) {
                            if (matchesPattern(controller->parts[rule].pattern, rig.bones[bone].name)) {
                                lastRule[bone] = static_cast<int32_t>(rule);
                            }
                        }
                    }
                }
                std::vector<uint8_t> own(rig.bones.size(), 0);
                for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                    if (lastRule[bone] >= 0) {
                        own[bone] = visible[lastRule[bone]] ? 0 : 1;
                    }
                }
                hidden.assign(rig.bones.size(), 0);
                for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                    int32_t walker = static_cast<int32_t>(bone);
                    for (size_t steps = 0; walker >= 0 && steps <= rig.bones.size(); ++steps) {
                        if (own[walker]) {
                            hidden[bone] = 1;
                            break;
                        }
                        walker = rig.bones[walker].parent;
                    }
                }
            }
        }
        if (actor.skinSlot != NoSkin) {
            layer = blockAssets->skinLayerBase() + actor.skinSlot;
        }
        float radians = (180.0f - wrapDegrees(actor.yaw)) * 3.14159265f / 180.0f;
        float cosine = std::cos(radians);
        float sine = std::sin(radians);
        float baseX = static_cast<float>(dx * 256.0);
        float baseY = static_cast<float>(dy * 256.0);
        float baseZ = static_cast<float>(dz * 256.0);
        out.reserve(out.size() + rig.quads.size());
        for (size_t index = 0; index < rig.quads.size(); ++index) {
            const world::ModelQuad& quad = rig.quads[index];
            size_t bone = index < rig.quadBones.size() ? rig.quadBones[index] : matrices.size();
            if (bone < hidden.size() && hidden[bone]) {
                continue;
            }
            const world::BoneMatrix* matrix = bone < matrices.size() ? &matrices[bone] : nullptr;
            world::ModelQuadGpu gpu;
            std::array<int16_t, 12> positions {};
            for (size_t corner = 0; corner < 4; ++corner) {
                float x = quad.positions[corner][0] / 16.0f;
                float y = quad.positions[corner][1] / 16.0f;
                float z = quad.positions[corner][2] / 16.0f;
                if (matrix) {
                    const world::BoneMatrix& m = *matrix;
                    float px = m[0] * x + m[1] * y + m[2] * z + m[3];
                    float py = m[4] * x + m[5] * y + m[6] * z + m[7];
                    float pz = m[8] * x + m[9] * y + m[10] * z + m[11];
                    x = px;
                    y = py;
                    z = pz;
                }
                x *= scale;
                y *= scale;
                z *= scale;
                float turnedX = cosine * x + sine * z;
                float turnedZ = -sine * x + cosine * z;
                positions[corner * 3 + 0] = roundToShort(baseX + turnedX);
                positions[corner * 3 + 1] = roundToShort(baseY + y);
                positions[corner * 3 + 2] = roundToShort(baseZ + turnedZ);
            }
            for (size_t word = 0; word < 6; ++word) {
                gpu.words[word] = uint32_t(uint16_t(positions[word * 2])) | (uint32_t(uint16_t(positions[word * 2 + 1])) << 16);
            }
            for (size_t corner = 0; corner < 4; ++corner) {
                gpu.words[6 + corner] = uint32_t(quad.uvs[corner][0]) | (uint32_t(quad.uvs[corner][1]) << 16);
            }
            gpu.words[10] = layer;
            gpu.words[11] = (quad.flags & world::QuadFaceMask) | EntityQuadFlag;
            gpu.words[12] = FullSkyLight;
            out.push_back(gpu);
        }
    }
    for (auto it = animators.begin(); it != animators.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = animators.erase(it);
        }
    }
    return out;
}

/**
 * Changes the held hotbar slot with the number keys and the mouse wheel while
 * the game has the mouse.
 */
void Client::handleHotbarInput()
{
    if (!menu.capturesMouse() || !worldShown) {
        return;
    }
    const InputState& input = window->input();
    int selected = hudState.selectedSlot;
    if (input.pressedKey >= Key::Num1 && input.pressedKey <= Key::Num9) {
        selected = static_cast<int>(input.pressedKey) - static_cast<int>(Key::Num1);
    } else if (input.wheel != 0.0f) {
        int steps = input.wheel > 0.0f ? -1 : 1;
        selected = ((selected + steps) % 9 + 9) % 9;
    }
    if (selected != hudState.selectedSlot) {
        session.selectHotbarSlot(selected);
        hudState.selectedSlot = selected;
        hudState.selectedChanged = secondsNow();
    }
}

/**
 * The HUD for this frame from the latest session state: slot icons from the
 * icon cache (new icons are rendered and queued for the atlas), durability,
 * armor points, heart and hunger looks from active effects, the fading name
 * of a newly selected item and blinking effects about to expire.
 */
/**
 * How strongly night vision lights the world: fully while it lasts, pulsing
 * during its last ten seconds, and not at all without it.
 */
float Client::nightVisionStrength() const
{
    constexpr int32_t NightVisionEffect = 16;
    double now = secondsNow();
    for (const HudEffect& effect : hudState.effects) {
        if (effect.id != NightVisionEffect) {
            continue;
        }
        if (effect.expires < 0.0) {
            return 1.0f;
        }
        double remaining = effect.expires - now;
        if (remaining <= 0.0) {
            return 0.0f;
        }
        if (remaining > 10.0) {
            return 1.0f;
        }
        return 0.7f + static_cast<float>(std::sin(remaining * 20.0 * 3.14159265 * 0.2)) * 0.3f;
    }
    return 0.0f;
}

menu::HudView Client::buildHudView()
{
    menu::HudView view;
    if (!blockAssets || !worldShown || !terrainReleased) {
        return view;
    }
    double now = secondsNow();
    const HudState& state = hudState;
    view.visible = true;
    view.showHotbar = state.gameType != 6;
    view.showStats = state.gameType == 0 || state.gameType == 2;
    view.selected = std::clamp(state.selectedSlot, 0, 8);

    std::set<std::string> wanted;
    auto slotOf = [&](const HudItem& item) {
        menu::HudSlot slot;
        if (item.empty()) {
            return slot;
        }
        slot.filled = true;
        slot.count = item.count;
        std::string name = "item/" + item.identifier + "#" + std::to_string(item.aux) + "#" + item.icon;
        wanted.insert(name);
        auto known = itemIcons.find(name);
        if (known == itemIcons.end()) {
            std::vector<uint8_t> pixels = blockAssets->itemIcon(item.identifier, item.aux, item.icon);
            bool rendered = pixels.size() == size_t(world::ItemIconSize) * world::ItemIconSize * 4;
            if (rendered) {
                skin.setDynamic(name, { world::ItemIconSize, world::ItemIconSize, std::move(pixels) });
            }
            debugLog("item icon " + name + (rendered ? " rendered" : " missing"));
            known = itemIcons.emplace(name, rendered).first;
        }
        if (known->second) {
            slot.icon = name;
        }
        int32_t maximum = itemMaxDurability(item.identifier);
        if (maximum > 0 && item.damage > 0) {
            slot.durability = std::clamp(float(maximum - item.damage) / float(maximum), 0.0f, 1.0f);
        }
        return slot;
    };
    for (size_t index = 0; index < 9; ++index) {
        view.hotbar[index] = slotOf(state.inventory[index]);
    }
    view.offhand = slotOf(state.offhand);
    if (itemIcons.size() > 64) {
        for (auto it = itemIcons.begin(); it != itemIcons.end();) {
            if (wanted.count(it->first)) {
                ++it;
            } else {
                skin.clearDynamic(it->first);
                it = itemIcons.erase(it);
            }
        }
    }

    const HudItem& held = state.inventory[size_t(view.selected)];
    if (!held.empty()) {
        view.selectedName = held.customName.empty() ? itemDisplayName(held.identifier) : held.customName;
        double elapsed = now - state.selectedChanged;
        view.labelAlpha = elapsed < 1.5 ? 1.0f : elapsed < 2.0 ? static_cast<float>((2.0 - elapsed) / 0.5) : 0.0f;
    }

    view.health = state.health;
    view.maxHealth = state.maxHealth;
    view.absorption = state.absorption;
    view.hunger = state.hunger;
    view.experience = state.experience;
    view.level = state.level;
    view.air = state.air;
    view.maxAir = state.maxAir;
    for (const HudItem& piece : state.armor) {
        if (!piece.empty()) {
            view.armor += itemArmorPoints(piece.identifier);
        }
    }
    double sinceDrop = now - state.lastHealthDrop;
    view.heartFlash = state.lastHealthDrop > 0.0 && sinceDrop < 1.0 && static_cast<int>(sinceDrop / 0.15) % 2 == 0;
    for (const HudEffect& effect : state.effects) {
        double remaining = effect.expires < 0.0 ? 1.0e9 : effect.expires - now;
        if (remaining <= 0.0) {
            continue;
        }
        if (effect.id == 19) {
            view.heartKind = menu::HeartKind::Poison;
        } else if (effect.id == 20) {
            view.heartKind = menu::HeartKind::Wither;
        } else if (effect.id == 17) {
            view.hungerEffect = true;
        }
        menu::HudEffectView entry;
        entry.id = effect.id;
        entry.ambient = effect.ambient;
        if (remaining < 10.0) {
            double pulse = std::cos(remaining * 3.14159265 * 2.0) * 0.5 + 0.5;
            entry.alpha = static_cast<float>(std::clamp(remaining / 10.0 * 0.5 + pulse * 0.5, 0.0, 1.0));
        }
        view.effects.push_back(entry);
    }
    return view;
}

/**
 * Replaces every entity's network position and rotation with its smoothed
 * one. A new sample starts a glide from the displayed state toward it, lasting
 * as long as the gap since the previous sample (one to three ticks); rotations
 * take the shortest way round. New entities, teleports and jumps longer than
 * eight blocks snap.
 */
void Client::interpolateActors(double now)
{
    constexpr double MinGlide = 0.05;
    constexpr double MaxGlide = 0.15;
    constexpr double SnapDistance = 8.0;
    std::unordered_set<uint64_t> present;
    for (ActorView& actor : actorViews) {
        present.insert(actor.runtimeId);
        std::array<double, 3> target { actor.x, actor.y, actor.z };
        std::array<float, 3> turn { actor.yaw, actor.headYaw, actor.pitch };
        auto [entry, created] = motions.try_emplace(actor.runtimeId);
        ActorMotion& motion = entry->second;
        double jump = std::sqrt((target[0] - motion.shown[0]) * (target[0] - motion.shown[0]) + (target[1] - motion.shown[1]) * (target[1] - motion.shown[1]) + (target[2] - motion.shown[2]) * (target[2] - motion.shown[2]));
        if (created || actor.teleports != motion.teleports || jump > SnapDistance) {
            motion.from = target;
            motion.to = target;
            motion.shown = target;
            motion.turnFrom = turn;
            motion.turnTo = turn;
            motion.turnShown = turn;
            motion.start = now;
            motion.duration = 0.0;
            motion.lastSample = now;
            motion.moves = actor.moves;
            motion.teleports = actor.teleports;
        } else if (actor.moves != motion.moves) {
            motion.from = motion.shown;
            motion.to = target;
            motion.turnFrom = motion.turnShown;
            motion.turnTo = turn;
            motion.duration = std::clamp(now - motion.lastSample, MinGlide, MaxGlide);
            motion.start = now;
            motion.lastSample = now;
            motion.moves = actor.moves;
        }
        double t = motion.duration > 0.0 ? std::clamp((now - motion.start) / motion.duration, 0.0, 1.0) : 1.0;
        for (size_t axis = 0; axis < 3; ++axis) {
            motion.shown[axis] = motion.from[axis] + (motion.to[axis] - motion.from[axis]) * t;
            float delta = wrapDegrees(motion.turnTo[axis] - motion.turnFrom[axis]);
            motion.turnShown[axis] = wrapDegrees(motion.turnFrom[axis] + delta * static_cast<float>(t));
        }
        actor.x = motion.shown[0];
        actor.y = motion.shown[1];
        actor.z = motion.shown[2];
        actor.yaw = motion.turnShown[0];
        actor.headYaw = motion.turnShown[1];
        actor.pitch = motion.turnShown[2];
    }
    for (auto it = motions.begin(); it != motions.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = motions.erase(it);
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
    playerView = snapshot.state == SessionState::Joined ? snapshot.player : PlayerView {};
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
        std::vector<uint8_t> entityPixels = assets->entityTexturePixels();
        entityPixels.resize(entityPixels.size() + size_t(world::SkinSlots) * world::EntityTextureSize * world::EntityTextureSize * 4, 0);
        renderer->uploadEntityTextures(entityPixels.data(), world::EntityTextureSize, assets->entityTextureLayers() + world::SkinSlots);
        for (const auto& [slot, pixels] : skinPixels) {
            renderer->updateEntityTexture(assets->skinLayerBase() + slot, pixels.data());
        }
        if (blockAssets != assets) {
            partMatches.clear();
            animators.clear();
            for (const auto& [name, rendered] : itemIcons) {
                skin.clearDynamic(name);
            }
            itemIcons.clear();
            debugLog("item textures " + std::to_string(assets->itemTextureCount()));
        }
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
    timeState.cameraMedium = snapshot.cameraMedium;
    actorViews = std::move(snapshot.actors);
    hudState = std::move(snapshot.hud);
    for (SkinUpload& skin : session.takeSkinUploads()) {
        if (blockAssets) {
            renderer->updateEntityTexture(blockAssets->skinLayerBase() + skin.slot, skin.pixels.data());
        }
        skinPixels[skin.slot] = std::move(skin.pixels);
        skinRigs[skin.slot] = std::move(skin.rig);
        partMatches.clear();
    }
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
        } else if (key == "fov") {
            menu.setFov(std::clamp(std::atoi(value.c_str()), menu::MinFov, menu::MaxFov));
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
    savedFov = menu.fov();
}

void Client::saveSettings()
{
    std::ofstream file(settingsFile, std::ios::trunc);
    file << "interfaceScale=" << menu.interfaceScale() << '\n';
    file << "renderDistance=" << menu.renderDistance() << '\n';
    file << "maxFps=" << menu.maxFps() << '\n';
    file << "fov=" << menu.fov() << '\n';
    const KeyBindings& current = menu.keyBindings();
    for (size_t i = 0; i < KeyBindings::Count; ++i) {
        file << "key." << KeyBindings::id(i) << '=' << keyName(current.keys[i]) << '\n';
    }
    savedScale = menu.interfaceScale();
    savedBindings = current;
    savedRenderDistance = menu.renderDistance();
    savedMaxFps = menu.maxFps();
    savedFov = menu.fov();
}

}
