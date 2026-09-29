#include "modding/ModManager.h"

#include "HostState.h"
#include "ModSlot.h"
#include "Painters.h"

#include "client/ChatText.h"
#include "client/DebugLog.h"
#include "menu/Menu.h"
#include "mod/Events.h"
#include "platform/Input.h"
#include "platform/Library.h"
#include "ui/Localization.h"

#include "Protocol/MinecraftPacketIds.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace kestrel::modding {

namespace {

constexpr double TickSeconds = 0.05;
constexpr int MaxTicksPerFrame = 10;

mod::ChatKind chatKind(ChatMessage::Kind kind)
{
    switch (kind) {
    case ChatMessage::Kind::Chat:
        return mod::ChatKind::Chat;
    case ChatMessage::Kind::Whisper:
    case ChatMessage::Kind::WhisperJson:
        return mod::ChatKind::Whisper;
    case ChatMessage::Kind::Announcement:
    case ChatMessage::Kind::AnnouncementJson:
        return mod::ChatKind::Announcement;
    case ChatMessage::Kind::Popup:
    case ChatMessage::Kind::JukeboxPopup:
        return mod::ChatKind::Popup;
    case ChatMessage::Kind::Tip:
        return mod::ChatKind::Tip;
    default:
        return mod::ChatKind::System;
    }
}

// General 4x4 inverse by cofactors, in double so the far plane survives.
bool invert(const std::array<float, 16>& matrix, std::array<float, 16>& out)
{
    std::array<double, 16> m {};
    std::copy(matrix.begin(), matrix.end(), m.begin());
    std::array<double, 16> inv {};
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    double determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (determinant == 0.0) {
        return false;
    }
    for (size_t i = 0; i < 16; ++i) {
        out[i] = static_cast<float>(inv[i] / determinant);
    }
    return true;
}

std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

}

ModManager::ModManager(Session& session, menu::Menu& menu, Renderer& renderer, ClientBridge bridge)
    : started(secondsNow())
{
    ErrorSink errors = [this](size_t owner, std::string_view what) { reportError(owner, what); };
    // Packet filters throw on the network thread; the report waits for the main one.
    ErrorSink threadErrors = [this](size_t owner, std::string_view what) {
        host->scheduler.post(HostOwner, [this, owner, message = std::string(what)] { reportError(owner, message); });
    };
    host = std::make_unique<HostState>(session, menu, renderer, std::move(bridge), std::move(errors), std::move(threadErrors));
    host->seconds = started;
    hostChat = std::make_unique<ChatService>(*host);
}

ModManager::~ModManager()
{
    host->packets->observe(false, false);
    while (!slots.empty()) {
        slots.pop_back();
    }
    host->events.release(HostOwner);
    host->commands.release(HostOwner);
    host->keyBinds.release(HostOwner);
}

std::vector<menu::ModKeyBind> ModManager::listedKeyBinds() const
{
    std::vector<menu::ModKeyBind> binds;
    for (const KeyBindRegistry::Listed& entry : host->keyBinds.list()) {
        binds.push_back({ entry.id, entry.label, entry.key });
    }
    return binds;
}

bool ModManager::setKeyBind(const std::string& id, Key key)
{
    return host->keyBinds.set(id, key);
}

void ModManager::loadFolder(const std::filesystem::path& folder)
{
    host->root = folder;
    std::error_code error;
    std::filesystem::create_directories(folder, error);

    std::vector<std::filesystem::path> files;
    std::string extension = platform::Library::extension();
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (entry.is_regular_file(error) && lowercase(entry.path().extension().string()) == extension) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& file : files) {
        std::string reason;
        std::unique_ptr<ModSlot> slot = ModSlot::load(file, *host, slots.size() + 1, reason);
        if (!slot) {
            debugLog("mods: skipped " + file.filename().string() + ", " + reason);
            std::fprintf(stderr, "Kestrel skipped the mod %s: %s\n", file.filename().string().c_str(), reason.c_str());
            continue;
        }
        bool taken = std::any_of(slots.begin(), slots.end(), [&](const auto& other) { return other->info().id == slot->info().id; });
        if (taken) {
            debugLog("mods: skipped " + file.filename().string() + ", another mod already uses the id " + slot->info().id);
            continue;
        }
        debugLog("mods: loaded " + slot->info().id + " " + slot->info().version + " from " + file.filename().string());
        host->loaded.push_back(slot->info());
        slots.push_back(std::move(slot));
    }
    if (slots.empty()) {
        return;
    }

    host->session.setPacketHook(host->packets);
    registerBuiltins();
    for (auto slot = slots.begin(); slot != slots.end();) {
        if ((*slot)->enable()) {
            ++slot;
            continue;
        }
        std::string id = (*slot)->info().id;
        std::erase_if(host->loaded, [&](const mod::ModInfo& info) { return info.id == id; });
        slot = slots.erase(slot);
    }
}

void ModManager::handleInput(InputState& input, bool inGame)
{
    host->input = &input;
    host->inGame = inGame;
    if (slots.empty()) {
        return;
    }
    if (input.pressedKey != Key::None) {
        mod::KeyPressEvent event;
        event.key = input.pressedKey;
        event.inGame = inGame;
        host->events.dispatch(event);
        if (event.isCancelled()) {
            switch (input.pressedKey) {
            case Key::Escape:
                input.escape = false;
                break;
            case Key::Enter:
                input.enter = false;
                break;
            case Key::Tab:
                input.tab = false;
                break;
            case Key::Backspace:
                input.backspace = false;
                break;
            default:
                break;
            }
            input.pressedKey = Key::None;
        }
    }
    auto click = [&](bool& pressed, mod::MouseButton button) {
        if (!pressed) {
            return;
        }
        mod::MouseClickEvent event;
        event.button = button;
        event.x = input.mouseX;
        event.y = input.mouseY;
        event.inGame = inGame;
        host->events.dispatch(event);
        if (event.isCancelled()) {
            pressed = false;
        }
    };
    click(input.mousePressed, mod::MouseButton::Left);
    click(input.rightMousePressed, mod::MouseButton::Right);
    click(input.middleMousePressed, mod::MouseButton::Middle);
}

void ModManager::observe(const SessionSnapshot& snapshot)
{
    if (slots.empty()) {
        return;
    }
    host->snapshot = snapshot;
    if (snapshot.state != lastState) {
        mod::ConnectionStateEvent event;
        event.previous = static_cast<mod::ConnectionState>(lastState);
        event.current = static_cast<mod::ConnectionState>(snapshot.state);
        SessionState previous = std::exchange(lastState, snapshot.state);
        host->events.dispatch(event);
        if (previous == SessionState::Joined) {
            mod::DisconnectEvent left;
            left.reason = snapshot.error;
            host->events.dispatch(left);
        }
    }
    if (snapshot.state != SessionState::Joined) {
        lastDead = false;
        return;
    }
    if (snapshot.joinCount != lastJoin) {
        lastJoin = snapshot.joinCount;
        lastDimension = snapshot.dimension;
        mod::JoinEvent event;
        event.server = snapshot.name;
        event.address = snapshot.target;
        host->events.dispatch(event);
    }
    if (snapshot.dimension != lastDimension) {
        mod::DimensionChangeEvent event;
        event.previous = std::exchange(lastDimension, snapshot.dimension);
        event.current = snapshot.dimension;
        host->events.dispatch(event);
    }
    if (snapshot.dead != lastDead) {
        lastDead = snapshot.dead;
        if (snapshot.dead) {
            mod::DeathEvent event;
            if (!snapshot.deathCause.empty()) {
                event.message = ui::Localization::shared().translateMessage(snapshot.deathCause, snapshot.deathParameters);
            }
            host->events.dispatch(event);
        } else {
            mod::RespawnEvent event;
            host->events.dispatch(event);
        }
    }
}

void ModManager::update(float deltaSeconds)
{
    if (slots.empty()) {
        return;
    }
    double now = secondsNow();
    host->seconds = now;
    host->shaders.clear();
    drainPackets();

    mod::FrameEvent frame;
    frame.deltaSeconds = deltaSeconds;
    host->events.dispatch(frame);

    tickClock += deltaSeconds;
    for (int i = 0; tickClock >= TickSeconds && i < MaxTicksPerFrame; ++i) {
        tickClock -= TickSeconds;
        mod::TickEvent tick;
        tick.tick = ++ticks;
        host->events.dispatch(tick);
    }
    tickClock = std::min(tickClock, TickSeconds);

    host->scheduler.run(now, host->errors);
}

void ModManager::adjustMovement(MotionInput& input)
{
    if (slots.empty() || !host->events.listening(mod::MovementEvent::Type)) {
        return;
    }
    mod::MovementEvent event;
    event.forward = input.forward;
    event.sideways = input.sideways;
    event.jump = input.jump;
    event.sneak = input.sneak;
    event.sprint = input.sprint;
    host->events.dispatch(event);
    input.forward = std::clamp(event.forward, -1.0f, 1.0f);
    input.sideways = std::clamp(event.sideways, -1.0f, 1.0f);
    input.jump = event.jump;
    input.sneak = event.sneak;
    input.sprint = event.sprint;
}

bool ModManager::receiveChat(const ChatMessage& message, std::string& text)
{
    if (slots.empty()) {
        return true;
    }
    mod::ChatReceivedEvent event;
    event.kind = chatKind(message.kind);
    event.source = message.source;
    event.raw = message.message;
    event.text = text;
    host->events.dispatch(event);
    if (event.isCancelled()) {
        return false;
    }
    text = std::move(event.text);
    return true;
}

bool ModManager::sendChat(std::string& text)
{
    if (slots.empty()) {
        return true;
    }
    if (host->commands.execute(text, *hostChat, host->errors)) {
        return false;
    }
    mod::ChatSendEvent event;
    event.text = text;
    host->events.dispatch(event);
    if (event.isCancelled()) {
        return false;
    }
    text = std::move(event.text);
    return !text.empty();
}

bool ModManager::filterTitle(TitleRequest& request)
{
    if (slots.empty() || (request.kind != TitleRequest::Kind::Title && request.kind != TitleRequest::Kind::Subtitle)) {
        return true;
    }
    mod::TitleEvent event;
    event.subtitle = request.kind == TitleRequest::Kind::Subtitle;
    event.text = request.json ? rawText(request.text) : request.text;
    std::string shown = event.text;
    host->events.dispatch(event);
    if (event.isCancelled()) {
        return false;
    }
    if (event.text != shown) {
        request.text = std::move(event.text);
        request.json = false;
    }
    return true;
}

bool ModManager::filterActionbar(ActionbarText& actionbar)
{
    if (slots.empty()) {
        return true;
    }
    mod::ActionbarEvent event;
    event.text = actionbar.json ? rawText(actionbar.text) : actionbar.text;
    host->events.dispatch(event);
    if (event.isCancelled()) {
        return false;
    }
    actionbar.text = std::move(event.text);
    actionbar.json = false;
    return true;
}

bool ModManager::filterToast(ToastRequest& toast)
{
    if (slots.empty()) {
        return true;
    }
    mod::ToastEvent event;
    event.title = toast.title;
    event.content = toast.content;
    host->events.dispatch(event);
    if (event.isCancelled()) {
        return false;
    }
    toast.title = std::move(event.title);
    toast.content = std::move(event.content);
    return true;
}

bool ModManager::filterForm(const FormRequest& form)
{
    if (slots.empty() || form.close) {
        return true;
    }
    mod::FormEvent event;
    event.id = form.id;
    event.json = form.data;
    host->events.dispatch(event);
    return !event.isCancelled();
}

void ModManager::drawHud(ui::Context& context, float width, float height, bool screenOpen)
{
    if (slots.empty() || !host->events.listening(mod::HudRenderEvent::Type)) {
        return;
    }
    hudWidth = width;
    hudHeight = height;
    context.setOrigin(0.0f, 0.0f);
    context.clearLayer();
    context.clearClip();
    UiCanvas canvas(context, host->shaders, width, height);
    mod::HudRenderEvent event(canvas);
    event.screenOpen = screenOpen;
    host->events.dispatch(event);
    context.clearClip();
}

void ModManager::drawWorld(const std::array<float, 16>& viewProjection, const mod::Vec3& camera)
{
    if (slots.empty() || !host->events.listening(mod::WorldRenderEvent::Type)) {
        return;
    }
    WorldCanvas painter(host->shaders, camera);
    mod::WorldRenderEvent event(painter);
    host->events.dispatch(event);
    host->shaders.submit(CustomLayer::World, viewProjection, static_cast<float>(host->seconds - started));
}

void ModManager::drawPost(const std::array<float, 16>& viewProjection)
{
    if (slots.empty() || !host->events.listening(mod::PostProcessEvent::Type) || !host->shaders.supportsPost()) {
        return;
    }
    std::array<float, 16> inverse {};
    if (!invert(viewProjection, inverse)) {
        return;
    }
    PostPasses chain(host->shaders);
    mod::PostProcessEvent event(chain);
    host->events.dispatch(event);
    host->shaders.submitPost(inverse, static_cast<float>(host->seconds - started));
}

void ModManager::setEnvironment(const mod::Environment& environment)
{
    host->environment = environment;
}

void ModManager::drawScreen(CustomLayer layer)
{
    if (slots.empty() || hudWidth <= 0.0f || hudHeight <= 0.0f) {
        return;
    }
    // Interface units to clip space with y up, column major.
    std::array<float, 16> transform {};
    transform[0] = 2.0f / hudWidth;
    transform[5] = -2.0f / hudHeight;
    transform[10] = 1.0f;
    transform[12] = -1.0f;
    transform[13] = 1.0f;
    transform[15] = 1.0f;
    host->shaders.submit(layer, transform, static_cast<float>(host->seconds - started));
}

void ModManager::reportError(size_t owner, std::string_view what)
{
    std::string name = modName(owner);
    std::string line = "mod " + name + " error: " + std::string(what);
    debugLog(line);
    std::fprintf(stderr, "%s\n", line.c_str());
    // Once per mod, a handler that throws every frame would bury the chat.
    if (warned.insert(owner).second) {
        host->menu.addChatLine("§c" + name + " ran into an error: " + std::string(what) + " (more in debug.txt)");
    }
}

void ModManager::registerBuiltins()
{
    host->commands.add(HostOwner, { "help", "Lists the mod commands", "", {} }, [this](mod::CommandContext& context) {
        context.reply("§eMod commands:");
        for (const auto& entry : host->commands.list()) {
            std::string line = "§7" + std::string(1, mod::Commands::Prefix) + entry->spec.name;
            if (!entry->spec.usage.empty()) {
                line += " " + entry->spec.usage;
            }
            if (!entry->spec.description.empty()) {
                line += "§f - " + entry->spec.description;
            }
            context.reply(line);
        }
    });
    host->commands.add(HostOwner, { "mods", "Lists the loaded mods", "", {} }, [this](mod::CommandContext& context) {
        context.reply("§e" + std::to_string(host->loaded.size()) + " mods loaded:");
        for (const mod::ModInfo& info : host->loaded) {
            std::string line = "§a" + (info.name.empty() ? info.id : info.name) + " §7" + info.version;
            if (!info.author.empty()) {
                line += " by " + info.author;
            }
            context.reply(line);
        }
    });
}

void ModManager::drainPackets()
{
    bool inbound = host->events.listening(mod::PacketReceivedEvent::Type);
    bool outbound = host->events.listening(mod::PacketSentEvent::Type);
    host->packets->observe(inbound, outbound);
    if (!inbound && !outbound) {
        return;
    }
    for (ObservedPacket& packet : host->packets->takeObserved()) {
        const char* name = toString(static_cast<MinecraftPacketIds>(packet.id));
        if (packet.outbound) {
            mod::PacketSentEvent event;
            event.id = packet.id;
            event.name = name ? name : std::to_string(packet.id);
            event.payload = std::move(packet.payload);
            host->events.dispatch(event);
        } else {
            mod::PacketReceivedEvent event;
            event.id = packet.id;
            event.name = name ? name : std::to_string(packet.id);
            event.payload = std::move(packet.payload);
            host->events.dispatch(event);
        }
    }
}

std::string ModManager::modName(size_t owner) const
{
    for (const auto& slot : slots) {
        if (slot->owner() == owner) {
            return slot->info().name.empty() ? slot->info().id : slot->info().name;
        }
    }
    return owner == HostOwner ? "Kestrel" : "mod #" + std::to_string(owner);
}

}
