#include "mod/Api.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace kestrel::mod;

/**
 * Counts the packets it sees on the network thread, remembers the xuid of the
 * player's own chat for a typed chat line, and can tag incoming chat to show
 * that a changed packet is encoded again.
 */
class AuditFilter : public TypedPacketFilter {
public:
    std::atomic<uint64_t> authInputs { 0 }, inbound { 0 }, texts { 0 }, moves { 0 }, lastInputTick { 0 };
    std::atomic<bool> tagChat { false };

    bool outboundTyped(PacketView& packet) override
    {
        if (auto* input = packet.as<packets::AuthInput>()) {
            ++authInputs;
            lastInputTick = input->tick;
        }
        if (auto* text = packet.as<packets::Text>()) {
            std::lock_guard<std::mutex> guard(mutex);
            xuid = text->xuid;
        }
        return true;
    }

    bool inboundTyped(PacketView& packet) override
    {
        ++inbound;
        if (packet.as<packets::MovePlayer>()) {
            ++moves;
        }
        if (auto* text = packet.as<packets::Text>()) {
            ++texts;
            if (tagChat && text->type == 1) {
                text->message = "[audit] " + text->message;
            }
        }
        return true;
    }

    std::string ownXuid()
    {
        std::lock_guard<std::mutex> guard(mutex);
        return xuid;
    }

private:
    std::mutex mutex;
    std::string xuid;
};

class ApiAuditMod : public Mod {
public:
    ApiAuditMod() : Mod({ .id = "api_audit", .name = "Mod API audit", .version = "1.0.0", .author = "Kestrel", .description = "F10: effects, audio, UI and editable image checks (API 4)" }) { }
    void onEnable() override
    {
        rows.clear();
        check("Services available", ui().supported() && textures().supported() && particles().supported() && audio().supported());
        resetTexture();
        on<KeyPressEvent>([this](KeyPressEvent& e) {
            if (e.key == Key::F10) { ui().open("audit"); e.cancel(); }
        });
        on<HudRenderEvent>([this](HudRenderEvent& e) {
            textures().draw(e.canvas, texture, { e.canvas.width() - 88, 24, 64, 64 });
            e.canvas.text("API audit - F10", e.canvas.width() - 150, 94, white);
            e.canvas.text(particles().active(particle) ? "Particle: active" : "Particle: stopped", 16, 24, white);
            e.canvas.text(audio().playing(sound) ? "Sound: playing" : "Sound: stopped", 16, 40, white);
            if (auto spot = hud().project(markerTop())) {
                e.canvas.text("project()", spot->first, spot->second, { 255, 220, 80, 255 });
            }
        });
        on<WorldRenderEvent>([this](WorldRenderEvent& e) {
            drawPrimitives(e.painter);
        });
        on<UiRenderEvent>([this](UiRenderEvent& e) { render(e); });
        on<BlockChangeEvent>([this](BlockChangeEvent& e) {
            ++blockChanges;
            lastBlockChange = e.oldName + " -> " + e.newName + (e.predicted ? " (predicted)" : "");
        });
        on<ChunkLoadEvent>([this](ChunkLoadEvent&) {
            ++chunkLoads;
        });
        on<ChunkUnloadEvent>([this](ChunkUnloadEvent&) {
            ++chunkUnloads;
        });
        on<PlayerTickEvent>([this](PlayerTickEvent& e) {
            ++playerTicks;
            lastTick = e.tick;
        });
        on<BlockBreakProgressEvent>([this](BlockBreakProgressEvent& e) {
            ++breakEvents;
            if (e.finished || e.aborted) {
                lastBreak = std::string(e.finished ? "finished " : "aborted ") + std::to_string(e.position.x) + " " + std::to_string(e.position.y) + " " + std::to_string(e.position.z);
            }
        });
        on<MovementEvent>([this](MovementEvent& e) {
            e.startGlide = std::exchange(glideRequested, false);
            e.swimDown = swimDown;
        });
        on<ContainerContentEvent>([this](ContainerContentEvent& e) {
            ++containerEvents;
            lastContainer = "type " + std::to_string(e.containerType) + ", " + std::to_string(e.slots.size()) + " slots";
        });
        filter = std::make_shared<AuditFilter>();
        network().addTypedFilter(filter);
        setupInfrastructure();
    }
private:
    static constexpr Color white { 255, 255, 255, 255 };
    TextureHandle texture = 0, imported = 0;
    ParticleHandle particle = 0;
    SoundHandle sound = 0;
    std::vector<std::pair<std::string, bool>> rows;
    std::string input = "Edit me", filename = "image.png", status = "Ready";
    float volume = 0.25f;
    int page = 0, clicks = 0;
    bool red = false;
    uint64_t blockChanges = 0, chunkLoads = 0, chunkUnloads = 0, playerTicks = 0, lastTick = 0;
    std::string lastBlockChange;
    uint64_t breakEvents = 0;
    std::string lastBreak;
    bool glideRequested = false, swimDown = false, breakingTarget = false, usingItem = false;
    std::shared_ptr<AuditFilter> filter;
    uint64_t containerEvents = 0;
    std::string lastContainer;
    void inventoryChecks()
    {
        int filled = player().findItem([](const ItemStack& item) {
            return !item.empty();
        });
        check("Find the first filled slot", filled >= -1 && filled < Player::InventorySize);
        check("findItem refusing everything finds nothing", player().findItem([](const ItemStack&) {
            return false;
        }) == -1);
        int tool = player().bestToolFor(markerBlock());
        check("Best tool is a hotbar slot or none", tool >= -1 && tool < Player::HotbarSize);
        check("Invalid clicks rejected", !player().clickSlot(Player::SlotsInventory, 99, Player::ClickLeft) && !player().clickSlot(42, 0, Player::ClickLeft)
            && !player().clickSlot(Player::SlotsInventory, 0, 7));
        check("Invalid moves rejected", !player().moveItem(0, 0, 1) && !player().moveItem(-1, 3, 1) && !player().moveItem(0, 36, 1));
        check("Invalid hotbar swaps rejected", !player().swapHotbar(3, 9) && !player().swapHotbar(36, 0));
        int empty = -1;
        for (int slot = Player::HotbarSize; slot < Player::InventorySize && empty < 0; ++slot) {
            if (player().inventory(slot).empty()) {
                empty = slot;
            }
        }
        if (filled >= 0 && empty >= 0) {
            check("Move one item to an empty slot queued", player().moveItem(filled, empty, 1));
        }
        auto view = player().openContainerContents();
        check("Closed container has no slots", view.open || view.slots.empty());
        if (view.open) {
            check("Open container: " + std::to_string(view.slots.size()) + " slots", !view.slots.empty());
        }
        if (containerEvents > 0) {
            check("Container content events arrive: " + lastContainer, true);
        }
    }
    void openTargetContainer()
    {
        auto target = world().targetBlock();
        if (!target) {
            status = "Look at a container first";
            return;
        }
        check("Open target container queued", player().openContainer(target->position));
    }
    void networkChecks()
    {
        check("Ping known in a world", !player().inWorld() || network().ping() >= 0);
        check("Outbound AuthInput typed", filter->authInputs > 0 && filter->lastInputTick > 0);
        check("Inbound packets reach the typed filter", filter->inbound > 0);
        status = "Ping " + std::to_string(network().ping()) + " ms, " + std::to_string(filter->texts.load()) + " texts, " + std::to_string(filter->moves.load()) + " moves";
    }
    void sendTypedChat()
    {
        packets::Text text;
        text.type = 1;
        text.source = player().name();
        text.message = "Typed packet audit";
        text.xuid = filter->ownXuid();
        PacketView view;
        view.id = packets::Text::Id;
        view.data = text;
        network().sendTyped(view);
        status = "Typed chat queued";
    }
    BlockPos markerBlock() const
    {
        auto feet = player().position();
        return { int32_t(std::floor(feet.x)), int32_t(std::floor(feet.y)) - 1, int32_t(std::floor(feet.z)) };
    }
    Vec3 markerTop() const
    {
        auto block = markerBlock();
        return { block.x + 0.5, block.y + 2.5, block.z + 0.5 };
    }
    void drawPrimitives(WorldPainter& painter)
    {
        if (!player().inWorld()) {
            return;
        }
        auto block = markerBlock();
        Vec3 min { double(block.x), double(block.y), double(block.z) };
        Vec3 max { block.x + 1.0, block.y + 1.0, block.z + 1.0 };
        painter.wireBox(min, max, { 80, 220, 255, 255 }, 0.03f, true);
        painter.filledBox({ min.x + 3, min.y, min.z }, { max.x + 3, max.y, max.z }, { 255, 80, 80, 90 });
        std::vector<std::pair<Vec3, Vec3>> stem;
        stem.emplace_back(Vec3 { min.x + 0.5, max.y, min.z + 0.5 }, markerTop());
        painter.lines(stem, { 255, 220, 80, 255 }, 0.05f);
        painter.text3d(markerTop(), "API 4 primitives\nthrough walls", white, 1.0f, true);
    }
    void worldChecks()
    {
        auto feet = player().position();
        BlockPos below { int32_t(std::floor(feet.x)), int32_t(std::floor(feet.y)) - 1, int32_t(std::floor(feet.z)) };
        auto props = world().properties(below);
        check("Block properties below the player", props.has_value() && !props->air);
        check("Collision below the player", !world().collision(below).empty() == (props && props->solid));
        check("Outline below the player", !world().outline(below).empty() || (props && props->liquid));
        auto solid = world().findBlocksMatching([](const BlockProps& block) {
            return block.solid;
        }, feet, 4, 8);
        check("Find solid blocks nearby", !solid.empty());
        auto tick = player().tickPosition();
        auto box = player().boundingBox();
        check("Bounding box holds the tick position", box.min.y == tick.y && box.min.x < tick.x && box.max.x > tick.x);
        auto velocity = player().velocity();
        check("Velocity is finite", std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z));
        check("Fall distance is not negative", player().fallDistance() >= 0.0f);
        check("Not in water and lava at once", !(player().inWater() && player().inLava()));
        check("Gliding needs to be off the ground", !(player().gliding() && player().onGround()));
        status = std::string("Climbable: ") + (player().onClimbable() ? "yes" : "no") + ", collided: " + (player().collidedHorizontally() ? "h" : "-") + (player().collidedVertically() ? "v" : "-");
        auto abilities = player().abilities();
        check("Abilities agree with flying", abilities.flying == player().flying() && abilities.walkSpeed > 0.0f && abilities.flySpeed > 0.0f && (!abilities.flying || abilities.mayFly));
        check("Player tick events arrive", playerTicks > 0 && lastTick > 0);
        check("Chunk load events arrive", chunkLoads > 0);
        check("Chunk unloads never outnumber loads", chunkUnloads <= chunkLoads);
        if (blockChanges > 0) {
            check("Block change seen: " + lastBlockChange, true);
        }
        int ticks = world().breakTicks(below);
        check("Break ticks estimated below the player", ticks == -1 || ticks >= 1);
        check("Air never breaks", world().breakTicks({ below.x, below.y + 1, below.z }) == -1 || (props && props->solid));
        auto path = player().predictPath(20);
        check("Predicted path covers every tick", path.size() == 20);
        if (!path.empty()) {
            double dx = path.front().x - tick.x, dy = path.front().y - tick.y, dz = path.front().z - tick.z;
            check("Predicted path starts at the player", std::sqrt(dx * dx + dy * dy + dz * dz) < 4.0);
        }
        check("Breaking progress within 0 and 1", player().breakingProgress() >= 0.0f && player().breakingProgress() <= 1.0f);
        if (breakEvents > 0) {
            check("Break progress events arrive" + (lastBreak.empty() ? std::string() : ": " + lastBreak), true);
        }
    }
    void toggleBreaking()
    {
        if (breakingTarget) {
            player().stopBreaking();
            breakingTarget = false;
            status = "Stopped breaking";
            return;
        }
        auto target = world().targetBlock();
        if (!target) {
            status = "Look at a block first";
            return;
        }
        player().startBreaking(target->position, 1);
        breakingTarget = true;
        status = "Breaking for " + std::to_string(world().breakTicks(target->position)) + " ticks";
    }
    void toggleItemUse()
    {
        usingItem = !usingItem;
        if (usingItem) {
            player().useItem();
        } else {
            player().releaseUse();
        }
        status = usingItem ? "Using held item" : "Released held item";
    }
    void attackNearest()
    {
        auto target = world().nearestEntity(player().eyePosition(), 6.0, [this](const Entity& entity) {
            return entity.runtimeId != player().runtimeId();
        });
        if (target) {
            player().attackEntity(target->runtimeId);
        }
        status = target ? "Attacked " + target->identifier : "Nothing to attack";
    }
    void setupInfrastructure()
    {
        ui().addSettings({
            { .key = "audit.notify", .label = "Notify when settings change", .kind = SettingSpec::Kind::Toggle, .defaultValue = "true" },
            { .key = "audit.volume", .label = "Sound volume", .kind = SettingSpec::Kind::Range, .minimum = 0.0, .maximum = 4.0, .step = 0.05, .defaultValue = "0.25" },
            { .key = "audit.page", .label = "First page", .kind = SettingSpec::Kind::Choice, .choices = { "Controls", "Textures", "Effects / audio", "Results" }, .defaultValue = "Controls" },
            { .key = "audit.key", .label = "Remembered key", .kind = SettingSpec::Kind::Key, .defaultValue = "F10" },
            { .key = "audit.note", .label = "Note", .kind = SettingSpec::Kind::Text },
        });
        applySettings();
        on<ConfigReloadEvent>([this](ConfigReloadEvent& e) {
            if (!e.targets(info().id)) {
                return;
            }
            applySettings();
            if (config().getBool("audit.notify", true)) {
                hud().notify("Audit settings saved", 2.0);
            }
        });
        CommandSpec spec { .name = "audit", .description = "Runs the settings, async and service checks", .usage = "<settings|async|services>" };
        spec.complete = [](const CommandContext& context) {
            return context.args.size() == 1 ? std::vector<std::string> { "settings", "async", "services" } : std::vector<std::string> {};
        };
        command(std::move(spec), [this](CommandContext& context) {
            const std::string& group = context.arg(0);
            if (group == "settings") {
                settingsChecks();
            } else if (group == "async") {
                asyncChecks();
            } else if (group == "services") {
                serviceChecks();
            } else {
                throw CommandError("Unknown group " + group);
            }
            context.reply(status);
        });
    }
    void applySettings()
    {
        volume = static_cast<float>(config().getDouble("audit.volume", 0.25));
        const char* pages[] = { "Controls", "Textures", "Effects / audio", "Results" };
        std::string first = config().getString("audit.page", "Controls");
        for (int i = 0; i < 4; ++i) {
            if (first == pages[i]) {
                page = i;
            }
        }
    }
    void settingsChecks()
    {
        check("Settings page values: volume " + config().getString("audit.volume", "0.25") + ", key " + config().getString("audit.key", "F10"), true);
        bool rejected = false;
        try {
            ui().addSettings({ { .key = "", .kind = SettingSpec::Kind::Toggle } });
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check("Setting without a key rejected", rejected);
        hud().notify("Settings checks done", 3.0);
        hud().notify("Notifications stack", 3.0);
    }
    void asyncChecks()
    {
        auto mainThread = std::this_thread::get_id();
        auto workerThread = std::make_shared<std::thread::id>();
        scheduler().async([workerThread] {
            *workerThread = std::this_thread::get_id();
        }, [this, workerThread, mainThread] {
            check("Async task ran on a worker thread", *workerThread != mainThread);
            check("Async onDone ran on the main thread", std::this_thread::get_id() == mainThread);
            hud().notify("Async task finished", 3.0);
        });
        Subscription cancelled = scheduler().async([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }, [this] {
            check("Cancelled async never reports back", false);
        });
        cancelled.cancel();
        check("Cancelled async is inactive", !cancelled.active());
    }
    void serviceChecks()
    {
        auto counter = std::make_shared<int>(41);
        context().provide("api_audit.counter", counter);
        auto found = context().service<int>("api_audit.counter");
        check("Service round trip", found && ++*found == 42 && *counter == 42);
        context().provideService("api_audit.counter", nullptr);
        check("Withdrawn service is gone", !context().findService("api_audit.counter"));
        check("Unknown service is null", !context().service<int>("api_audit.missing"));
    }
    static Image solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b)
    {
        Image image { w, h, std::vector<uint8_t>(size_t(w) * h * 4, 255) };
        for (size_t i = 0; i < image.pixels.size(); i += 4) {
            image.pixels[i] = r; image.pixels[i + 1] = g; image.pixels[i + 2] = b;
        }
        return image;
    }
    void check(std::string name, bool ok)
    {
        rows.emplace_back(name, ok);
        status = std::string(ok ? "PASS: " : "FAIL: ") + name;
        std::ofstream file(context().dataDirectory() / "results.tsv", std::ios::app);
        file << (ok ? "PASS\t" : "FAIL\t") << name << '\n';
    }
    void resetTexture()
    {
        textures().destroy(texture);
        auto image = solid(16, 16, 20, 100, 40);
        for (uint32_t y = 0; y < 16; ++y) for (uint32_t x = 0; x < 16; ++x)
            if ((x + y) % 2) { auto i = (y * 16 + x) * 4; image.pixels[i] = 240; image.pixels[i + 1] = 160; }
        texture = textures().create(std::move(image));
    }
    Vec3 front() const
    {
        constexpr double rad = 3.141592653589793 / 180;
        auto p = player().eyePosition(); auto r = player().rotation();
        p.x -= 3 * std::sin(r.yaw * rad) * std::cos(r.pitch * rad);
        p.y -= 3 * std::sin(r.pitch * rad);
        p.z += 3 * std::cos(r.yaw * rad) * std::cos(r.pitch * rad);
        return p;
    }
    void textureChecks()
    {
        auto h = textures().create(solid(4, 4, 10, 20, 30));
        check("Create / info / read", h && textures().info(h).width == 4 && textures().read(h).pixels == solid(4, 4, 10, 20, 30).pixels);
        auto copy = textures().read(h);
        if (!copy.pixels.empty()) copy.pixels[0] = 99;
        auto original = textures().read(h);
        check("Read returns independent pixels", !original.pixels.empty() && original.pixels[0] == 10);
        check("Full update and resize", textures().update(h, solid(8, 2, 40, 50, 60)) && textures().info(h).height == 2);
        check("Region update", textures().updateRegion(h, 2, 1, solid(2, 1, 200, 0, 0)) && textures().read(h).pixels[40] == 200);
        check("Invalid region rejected", !textures().updateRegion(h, 8, 0, solid(1, 1, 0, 0, 0)));
        check("Malformed image rejected", !textures().create({ 2, 2, { 1 } }));
        check("Destroy / stale handle", textures().destroy(h) && !textures().info(h).valid && !textures().update(h, solid(1, 1, 0, 0, 0)));
        check("Missing import rejected", !textures().load(context().dataDirectory() / "missing.png"));
        for (auto name : { "image.png", "image.jpg", "image.tga" }) {
            auto path = context().dataDirectory() / name;
            auto loaded = textures().load(path);
            check(std::string("Import ") + name, loaded && textures().info(loaded).width == 32);
            std::ifstream file(path, std::ios::binary);
            std::vector<uint8_t> bytes { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
            auto decoded = textures().decode(bytes);
            check(std::string("Memory decode ") + name, decoded && textures().read(decoded).pixels == textures().read(loaded).pixels);
            textures().destroy(loaded); textures().destroy(decoded);
        }
        check("Malformed encoded image rejected", !textures().decode(std::vector<uint8_t> { 1, 2, 3 }));
    }
    void invalidChecks()
    {
        auto nan = std::numeric_limits<double>::quiet_NaN();
        check("Invalid particle handle rejected", !particles().move(0, front()) && !particles().remove(0));
        check("Invalid audio handle rejected", !audio().stop(0) && !audio().setVolume(0, 1) && !audio().setPosition(0, front()));
        check("Nonfinite particle position rejected", !particles().spawn({ .identifier = "minecraft:evoker_spell", .position = { nan, 0, 0 } }));
        check("Invalid sound volume rejected", !audio().play({ .name = "random.pop", .volume = -1 }));
        check("Invalid sound pitch rejected", !audio().play({ .name = "random.pop", .pitch = 0 }));
        check("Invalid screen ID rejected", !ui().open(""));
        check("Unknown screen is closed", !ui().isOpen("missing") && !ui().close("missing"));
    }
    void render(UiRenderEvent& e)
    {
        auto& c = e.canvas; auto& u = e.controls;
        float x = (c.width() - 580) / 2, y = (c.height() - 420) / 2;
        c.fill({ x, y, 580, 420 }, { 35, 39, 45, 245 });
        c.textCentered("Mod API 4 - live audit", { x, y + 8, 580, 20 }, white);
        if (e.id == "nested") {
            c.textCentered("Nested modal screen - parent retained", { x, y + 90, 580, 30 }, white);
            if (u.button("back", "Back to parent", { x + 180, y + 150, 220, 28 })) {
                check("Close nested / parent retained", ui().close("nested") && ui().isOpen("audit"));
            }
            return;
        }
        if (e.id != "audit") return;
        const char* tabs[] = { "Controls", "Textures", "Effects / audio", "Results" };
        for (int i = 0; i < 4; ++i)
            if (u.button(std::string("tab") + std::to_string(i), tabs[i], { x + 12 + i * 140.0f, y + 36, 136, 26 })) page = i;
        auto button = [&](const char* id, const std::string& label, int col, int row) {
            return u.button(id, label, { x + 20 + col * 270.0f, y + 80 + row * 38.0f, 260, 28 });
        };
        if (page == 0) {
            if (u.textField("text", { x + 20, y + 82, 540, 28 }, input, "UTF-8 text", 128)) status = "Text: " + input;
            if (u.slider("slider", { x + 20, y + 128, 540, 20 }, volume, 0, 4, 0.05f)) {
                if (audio().playing(sound)) check("Live volume", audio().setVolume(sound, volume));
            }
            c.text("Slider: " + std::to_string(volume), x + 20, y + 155, white);
            if (button("count", "Clicks: " + std::to_string(clicks), 0, 3)) { ++clicks; status = "Button activated: " + std::to_string(clicks); }
            u.button("disabled", "Disabled button", { x + 290, y + 194, 260, 28 }, false);
            if (button("focus", "Focus text field", 0, 4)) { u.focus("text"); status = "Focus requested"; }
            if (button("nested", "Open nested screen", 1, 4)) check("Open / isOpen", ui().open("nested") && ui().isOpen("nested"));
            auto tool = [&](const char* id, const std::string& label, int col) {
                return u.button(id, label, { x + 20 + col * 109.0f, y + 266, 105, 22 });
            };
            if (tool("invchecks", "Inventory checks", 0)) inventoryChecks();
            if (tool("opencont", "Open target", 1)) openTargetContainer();
            if (tool("closecont", "Close container", 2)) {
                player().closeContainer();
                status = "Close requested";
            }
            if (tool("netchecks", "Network checks", 3)) networkChecks();
            if (tool("tagchat", filter->tagChat ? "Tag chat on" : "Tag chat off", 4)) {
                filter->tagChat = !filter->tagChat;
                sendTypedChat();
            }
            c.text(u.focused("text") ? "Text has focus" : "Text not focused", x + 20, y + 292, white);
            c.text("Tab / Shift+Tab, arrows, Ctrl+A, Enter, Escape", x + 20, y + 322, white);
        } else if (page == 1) {
            textures().draw(c, texture, { x + 30, y + 80, 112, 112 });
            if (imported) {
                textures().draw(c, imported, { x + 170, y + 80, 112, 112 });
                textures().draw(c, imported, { x + 310, y + 80, 112, 112 }, { 255, 100, 100, 180 });
                c.setClip({ x + 450, y + 80, 45, 112 });
                textures().draw(c, imported, { x + 450, y + 80, 112, 112 }); c.clearClip();
            }
            u.textField("filename", { x + 20, y + 205, 540, 26 }, filename, "image.png", 256);
            if (button("paint", "Patch centre", 0, 5)) {
                red = !red; check("Visible centre patch", textures().updateRegion(texture, 4, 4, solid(8, 8, red ? 220 : 40, 40, red ? 40 : 220)));
            }
            if (button("import", "Import file", 1, 5)) {
                auto h = textures().load(context().dataDirectory() / filename);
                if (h) { textures().destroy(imported); imported = h; }
                status = h ? "Imported: " + filename : "Rejected; previous image retained";
            }
            if (button("checks", "Run texture checks", 0, 6)) textureChecks();
            if (button("cleartex", "Clear all textures / recreate", 1, 6)) {
                auto old = texture; textures().clear(); imported = 0;
                check("Clear invalidates handles", !textures().info(old).valid); resetTexture();
            }
        } else if (page == 2) {
            bool spawn = button("spawn", "Spawn particle", 0, 0);
            bool follow = button("follow", "Attach to player", 1, 0);
            if (spawn || follow) {
                particles().remove(particle);
                ParticleOptions options { .identifier = "minecraft:evoker_spell", .position = front() };
                if (follow) options.attachedEntity = player().runtimeId();
                particle = particles().spawn(std::move(options));
                check(follow ? "Attached spawn / active" : "Spawn / active", particle && particles().active(particle));
            }
            if (button("movep", "Move particle", 0, 1)) check("Move particle", particles().move(particle, front()));
            if (button("removep", "Remove particle", 1, 1)) check("Remove / inactive", particles().remove(particle) && !particles().active(particle));
            bool positional = button("pos", "Loop positional sound", 0, 2);
            bool flat = button("flat", "Loop flat sound", 1, 2);
            if (positional || flat) {
                audio().stop(sound);
                SoundOptions options { .name = "random.pop", .volume = volume, .loop = true };
                if (positional) options.position = front();
                sound = audio().play(std::move(options)); check(flat ? "Flat loop / playing" : "Positional loop / playing", sound && audio().playing(sound));
                if (flat && sound) check("Flat sound cannot be repositioned", !audio().setPosition(sound, front()));
            }
            if (button("moves", "Move sound", 0, 3)) check("Move positional sound", audio().setPosition(sound, front()));
            if (button("stops", "Stop sound", 1, 3)) check("Stop / not playing", audio().stop(sound) && !audio().playing(sound));
            if (button("once", "One-shot + expiry check", 0, 4)) {
                auto h = audio().play({ .name = "random.pop", .volume = volume });
                check("One-shot started", h && audio().playing(h));
                if (h) after(3, [this, h] { check("One-shot expired", !audio().playing(h)); });
            }
            if (button("stopall", "Clear effects / stop all", 1, 4)) {
                particles().clear(); audio().stopAll();
                check("Clear particles / stopAll", !particles().active(particle) && !audio().playing(sound));
            }
            if (button("invalid", "Invalid input checks", 0, 5)) invalidChecks();
            if (button("world", "World / player checks", 1, 5)) {
                worldChecks();
            }
            auto small = [&](const char* id, const std::string& label, int col) {
                return u.button(id, label, { x + 20 + col * 108.0f, y + 304, 104, 22 });
            };
            if (small("break", breakingTarget ? "Stop break" : "Break target", 0)) toggleBreaking();
            if (small("useitem", usingItem ? "Release item" : "Use item", 1)) toggleItemUse();
            if (small("attack", "Attack nearest", 2)) attackNearest();
            if (small("glide", "Request glide", 3)) { glideRequested = true; status = "Glide requested"; }
            if (small("swim", swimDown ? "Swim down on" : "Swim down off", 4)) swimDown = !swimDown;
            c.text(particles().active(particle) ? "Particle active" : "Particle inactive", x + 20, y + 336, white);
            c.text(audio().playing(sound) ? "Sound playing" : "Sound stopped", x + 290, y + 336, white);
        } else {
            int failed = 0; for (const auto& row : rows) if (!row.second) ++failed;
            c.text(std::to_string(rows.size()) + " recorded checks / " + std::to_string(failed) + " failures", x + 20, y + 78, white);
            auto start = rows.size() > 13 ? rows.size() - 13 : 0;
            for (size_t i = start; i < rows.size(); ++i)
                c.text(std::string(rows[i].second ? "PASS " : "FAIL ") + rows[i].first, x + 20, y + 104 + (i - start) * 17, rows[i].second ? Color { 130, 255, 150, 255 } : Color { 255, 100, 100, 255 });
            if (u.button("infra", "Settings / async / services", { x + 20, y + 326, 260, 24 })) {
                settingsChecks();
                asyncChecks();
                serviceChecks();
            }
        }
        c.text(status, x + 20, y + 360, white);
        if (u.button("close", "Close - F10 to reopen", { x + 160, y + 386, 260, 24 })) ui().close("audit");
    }
};

KESTREL_MOD(ApiAuditMod)
