#include "modding/ModManager.h"

#include "modding/HostState.h"
#include "modding/ModDependencies.h"
#include "modding/ModSlot.h"
#include "modding/Painters.h"

#include "client/ChatText.h"
#include "client/DebugLog.h"
#include "menu/Menu.h"
#include "mod/Events.h"
#include "platform/Input.h"
#include "platform/Library.h"
#include "platform/Shell.h"
#include "ui/Localization.h"

#include "Protocol/MinecraftPacketIds.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <utility>

namespace kestrel::modding {

namespace {

constexpr double TickSeconds = 0.05;
constexpr size_t MaxErrorsPerWindow = 10;
constexpr double ErrorWindowSeconds = 60.0;
constexpr double NoticeFadeSeconds = 0.5;
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

std::string utf8(char32_t codepoint)
{
    std::string text;
    if (codepoint < 0x80) {
        text += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        text += static_cast<char>(0xC0 | (codepoint >> 6));
        text += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        text += static_cast<char>(0xE0 | (codepoint >> 12));
        text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        text += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        text += static_cast<char>(0xF0 | (codepoint >> 18));
        text += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        text += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    return text;
}

mod::ScreenKind dialogKind(menu::Dialog dialog)
{
    switch (dialog) {
    case menu::Dialog::Pause:
        return mod::ScreenKind::Pause;
    case menu::Dialog::Chat:
        return mod::ScreenKind::Chat;
    case menu::Dialog::Death:
        return mod::ScreenKind::Death;
    case menu::Dialog::SignIn:
        return mod::ScreenKind::SignIn;
    case menu::Dialog::Connecting:
        return mod::ScreenKind::Connecting;
    case menu::Dialog::ConnectionError:
        return mod::ScreenKind::ConnectionError;
    default:
        return mod::ScreenKind::Dialog;
    }
}

mod::ScreenKind screenKind(menu::Screen screen)
{
    switch (screen) {
    case menu::Screen::Play:
        return mod::ScreenKind::Play;
    case menu::Screen::Settings:
        return mod::ScreenKind::Settings;
    case menu::Screen::ServerForm:
        return mod::ScreenKind::ServerForm;
    case menu::Screen::Marketplace:
        return mod::ScreenKind::Marketplace;
    case menu::Screen::DressingRoom:
        return mod::ScreenKind::DressingRoom;
    case menu::Screen::Profile:
        return mod::ScreenKind::Profile;
    default:
        return mod::ScreenKind::Title;
    }
}

mod::Vec3 positionOf(const ActorView& actor)
{
    return { actor.x, actor.y, actor.z };
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
    loadDisabled();
    host->session.setPacketHook(host->packets);
    registerBuiltins();
    scan();
}

/**
 * Picks up the libraries added to the folder since the last look, loading
 * the ones not turned off, and lets go of the ones taken out of it.
 */
void ModManager::scan()
{
    std::error_code error;
    std::vector<std::filesystem::path> files;
    std::string extension = platform::Library::extension();
    for (const auto& entry : std::filesystem::directory_iterator(host->root, error)) {
        if (entry.is_regular_file(error) && lowercase(entry.path().extension().string()) == extension) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (auto gone = records.begin(); gone != records.end();) {
        if (std::find(files.begin(), files.end(), gone->file) != files.end()) {
            ++gone;
            continue;
        }
        unloadRecord(*gone);
        gone = records.erase(gone);
    }
    std::vector<std::string> fresh;
    for (const std::filesystem::path& file : files) {
        if (record(file.filename().string())) {
            continue;
        }
        Record added;
        added.file = file;
        added.enabled = !disabled.contains(file.filename().string());
        records.push_back(std::move(added));
        if (records.back().enabled) {
            fresh.push_back(file.filename().string());
        }
    }
    startRecords(fresh);
}

void ModManager::loadRecord(Record& entry)
{
    startRecords({ entry.file.filename().string() });
}

/**
 * Opens the libraries of these records first and starts them in dependency
 * order, so whatever a mod needs is running before its onEnable. A mod whose
 * dependency is missing, failed or part of a cycle is refused and its
 * library let go.
 */
void ModManager::startRecords(const std::vector<std::string>& files)
{
    std::vector<std::pair<std::string, std::unique_ptr<ModSlot>>> opened;
    for (const std::string& file : files) {
        Record* entry = record(file);
        if (!entry || entry->owner != 0) {
            continue;
        }
        if (std::unique_ptr<ModSlot> loaded = openRecord(*entry)) {
            opened.emplace_back(file, std::move(loaded));
        }
    }
    std::vector<DependencyNode> nodes;
    for (const auto& [file, loaded] : opened) {
        nodes.push_back({ loaded->info().id, loaded->dependencies() });
    }
    std::set<std::string> running;
    for (const mod::ModInfo& info : host->loaded) {
        running.insert(info.id);
    }
    DependencyPlan plan = planDependencies(nodes, running);
    for (const auto& [index, reason] : plan.refused) {
        const std::string& file = opened[index].first;
        if (Record* entry = record(file)) {
            entry->info = opened[index].second->info();
            entry->dependencies = opened[index].second->dependencies();
            entry->error = reason;
        }
        debugLog("mods: skipped " + file + ", " + reason);
        std::fprintf(stderr, "Kestrel skipped the mod %s: %s\n", file.c_str(), reason.c_str());
    }
    for (size_t index : plan.order) {
        if (Record* entry = record(opened[index].first)) {
            startRecord(*entry, std::move(opened[index].second));
        }
    }
}

std::unique_ptr<ModSlot> ModManager::openRecord(Record& entry)
{
    std::string name = entry.file.filename().string();
    std::string reason;
    std::unique_ptr<ModSlot> loaded = ModSlot::load(entry.file, *host, nextOwner++, reason);
    if (!loaded) {
        entry.error = reason;
        debugLog("mods: skipped " + name + ", " + reason);
        std::fprintf(stderr, "Kestrel skipped the mod %s: %s\n", name.c_str(), reason.c_str());
    }
    return loaded;
}

void ModManager::startRecord(Record& entry, std::unique_ptr<ModSlot> loaded)
{
    std::string name = entry.file.filename().string();
    entry.info = loaded->info();
    entry.dependencies = loaded->dependencies();
    bool taken = std::any_of(slots.begin(), slots.end(), [&](const auto& other) { return other->info().id == entry.info.id; });
    if (taken) {
        entry.error = "another mod already uses the id " + entry.info.id;
        debugLog("mods: skipped " + name + ", " + entry.error);
        return;
    }
    for (const std::string& dependency : entry.dependencies) {
        bool present = std::any_of(host->loaded.begin(), host->loaded.end(), [&](const mod::ModInfo& info) { return info.id == dependency; });
        if (!present) {
            entry.error = "it needs the mod " + dependency + ", which is missing or could not start";
            debugLog("mods: skipped " + name + ", " + entry.error);
            return;
        }
    }
    host->loaded.push_back(entry.info);
    if (!loaded->enable()) {
        std::erase_if(host->loaded, [&](const mod::ModInfo& info) { return info.id == entry.info.id; });
        entry.error = "it failed to start, see debug.txt";
        return;
    }
    debugLog("mods: loaded " + entry.info.id + " " + entry.info.version + " from " + name);
    entry.error.clear();
    entry.owner = loaded->owner();
    slots.push_back(std::move(loaded));
}

std::vector<std::string> ModManager::unloadRecord(Record& entry)
{
    std::vector<std::string> stopped;
    if (entry.owner == 0) {
        return stopped;
    }
    size_t owner = entry.owner;
    entry.owner = 0;
    for (Record& other : records) {
        if (other.owner == 0 || std::find(other.dependencies.begin(), other.dependencies.end(), entry.info.id) == other.dependencies.end()) {
            continue;
        }
        std::vector<std::string> nested = unloadRecord(other);
        other.error = "its dependency " + entry.info.id + " was unloaded";
        stopped.push_back(other.file.filename().string());
        stopped.insert(stopped.end(), nested.begin(), nested.end());
    }
    std::erase_if(host->loaded, [&](const mod::ModInfo& info) { return info.id == entry.info.id; });
    std::erase_if(slots, [owner](const std::unique_ptr<ModSlot>& running) { return running->owner() == owner; });
    warned.erase(owner);
    errorTimes.erase(owner);
    settingsScroll.erase(owner);
    debugLog("mods: unloaded " + entry.info.id + " from " + entry.file.filename().string());
    return stopped;
}

/**
 * Turns off the mods reportError found failing too often. They stay turned
 * on in disabled.txt, so the next start or a reload tries them again.
 */
void ModManager::stopFaulted()
{
    std::set<size_t> failing = std::exchange(faulted, {});
    for (Record& entry : records) {
        if (entry.owner == 0 || !failing.contains(entry.owner)) {
            continue;
        }
        unloadRecord(entry);
        entry.error = "it was turned off after " + std::to_string(MaxErrorsPerWindow + 1) + " errors in " + std::to_string(static_cast<int>(ErrorWindowSeconds)) + " seconds, see debug.txt";
    }
}

void ModManager::openSettings(size_t owner)
{
    if (!host->settings.contains(owner)) {
        return;
    }
    UiRequest request;
    request.action = UiRequest::Action::Open;
    request.id = std::string(SettingsScreenId);
    host->ui.process(owner, request);
    settingsScroll.erase(owner);
    settingsListening.clear();
}

/**
 * Carries out what the Mods settings page asked since the last frame. A
 * changed setting reloads its mod so the mod reads it again.
 */
void ModManager::applyActions()
{
    stopFaulted();
    std::vector<menu::ModAction> actions = std::exchange(pending, {});
    for (const menu::ModAction& action : actions) {
        if (action.kind == menu::ModAction::Kind::Rescan) {
            scan();
            continue;
        }
        if (action.kind == menu::ModAction::Kind::OpenFolder) {
            platform::openUrl(host->root.string());
            continue;
        }
        if (action.kind == menu::ModAction::Kind::ReloadConfigs) {
            reloadConfigs(action.file);
            continue;
        }
        Record* entry = record(action.file);
        if (!entry) {
            continue;
        }
        switch (action.kind) {
        case menu::ModAction::Kind::Enable:
            entry->enabled = true;
            disabled.erase(action.file);
            saveDisabled();
            loadRecord(*entry);
            break;
        case menu::ModAction::Kind::Disable:
            entry->enabled = false;
            entry->error.clear();
            disabled.insert(action.file);
            saveDisabled();
            unloadRecord(*entry);
            break;
        case menu::ModAction::Kind::Reload: {
            std::vector<std::string> restart = unloadRecord(*entry);
            if (entry->enabled) {
                restart.insert(restart.begin(), action.file);
            }
            startRecords(restart);
            break;
        }
        case menu::ModAction::Kind::Remove: {
            unloadRecord(*entry);
            std::error_code error;
            std::filesystem::remove(entry->file, error);
            if (error) {
                entry->error = "couldn't delete the file: " + error.message();
                break;
            }
            disabled.erase(action.file);
            saveDisabled();
            std::erase_if(records, [&](const Record& other) { return other.file.filename().string() == action.file; });
            break;
        }
        case menu::ModAction::Kind::SetConfig:
            if (ModSlot* running = slot(entry->owner)) {
                running->configStore().put(action.key, action.value);
                running->configStore().save();
                std::vector<std::string> restart = unloadRecord(*entry);
                restart.insert(restart.begin(), action.file);
                startRecords(restart);
            }
            break;
        case menu::ModAction::Kind::OpenSettings:
            if (entry->owner != 0) {
                openSettings(entry->owner);
            }
            break;
        default:
            break;
        }
    }
}

void ModManager::loadDisabled()
{
    disabled.clear();
    std::ifstream in(host->root / "disabled.txt");
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty()) {
            disabled.insert(line);
        }
    }
}

void ModManager::saveDisabled() const
{
    std::ofstream out(host->root / "disabled.txt", std::ios::trunc);
    for (const std::string& file : disabled) {
        out << file << '\n';
    }
}

ModManager::Record* ModManager::record(const std::string& file)
{
    for (Record& entry : records) {
        if (entry.file.filename().string() == file) {
            return &entry;
        }
    }
    return nullptr;
}

ModSlot* ModManager::slot(size_t owner) const
{
    for (const auto& running : slots) {
        if (running->owner() == owner) {
            return running.get();
        }
    }
    return nullptr;
}

std::vector<menu::ModEntry> ModManager::listedMods() const
{
    std::vector<menu::ModEntry> entries;
    for (const Record& entry : records) {
        menu::ModEntry listed;
        listed.file = entry.file.filename().string();
        listed.id = entry.info.id;
        listed.name = entry.info.name;
        listed.version = entry.info.version;
        listed.author = entry.info.author;
        listed.description = entry.info.description;
        listed.enabled = entry.enabled;
        listed.loaded = entry.owner != 0;
        listed.error = entry.error;
        listed.hasSettings = entry.owner != 0 && host->settings.contains(entry.owner);
        if (ModSlot* running = slot(entry.owner)) {
            for (const std::string& key : running->configStore().keys()) {
                listed.config.emplace_back(key, running->configStore().find(key).value_or(std::string()));
            }
        }
        entries.push_back(std::move(listed));
    }
    return entries;
}

void ModManager::request(menu::ModAction action)
{
    pending.push_back(std::move(action));
}

menu::CommandHints ModManager::completeCommand(std::string_view draft)
{
    menu::CommandHints hints;
    if (slots.empty()) {
        return hints;
    }
    CommandRegistry::Completions found = host->commands.complete(draft, *hostChat, host->errors);
    hints.replaceFrom = found.replaceFrom;
    hints.usage = std::move(found.usage);
    for (auto& [text, description] : found.suggestions) {
        hints.suggestions.push_back({ std::move(text), std::move(description) });
    }
    return hints;
}

bool ModManager::wantsCursor() const
{
    return host->ui.open() || !host->cursorOwners.empty();
}

void ModManager::restoreInput(InputState& input)
{
    host->ui.restoreInput(input);
}

void ModManager::captureUiInput(InputState& input, float scale)
{
    if (host->ui.open()) host->ui.capture(input, scale);
}

bool ModManager::uiOpen() const
{
    return host->ui.open();
}

bool ModManager::hidesHud(mod::HudElement element) const
{
    uint32_t bit = 1u << static_cast<uint32_t>(element);
    return std::any_of(host->hiddenHud.begin(), host->hiddenHud.end(), [bit](const auto& entry) { return (entry.second & bit) != 0; });
}

std::optional<CameraRequest> ModManager::cameraView() const
{
    for (const auto& [owner, request] : host->cameras) {
        if (request.detached) {
            return request;
        }
    }
    return std::nullopt;
}

float ModManager::fovScale() const
{
    float scale = 1.0f;
    for (const auto& [owner, request] : host->cameras) {
        scale *= request.fovScale;
    }
    return std::clamp(scale, 0.05f, 3.0f);
}

void ModManager::setView(const mod::Vec3& position, mod::Rotation rotation, float fieldOfView)
{
    host->viewPosition = position;
    host->viewRotation = rotation;
    host->viewFieldOfView = fieldOfView;
}

VisualRequest ModManager::visuals() const
{
    VisualRequest merged;
    auto take = [](auto& into, const auto& from) {
        if (!into && from) {
            into = from;
        }
    };
    // owners count up as mods load, so the map walks them oldest first
    for (const auto& [owner, request] : host->visuals) {
        take(merged.time, request.time);
        take(merged.rain, request.rain);
        take(merged.thunder, request.thunder);
        take(merged.fogScale, request.fogScale);
        take(merged.brightness, request.brightness);
        take(merged.hurtCamera, request.hurtCamera);
        take(merged.hitColor, request.hitColor);
        take(merged.glintStrength, request.glintStrength);
        take(merged.glintSpeed, request.glintSpeed);
        take(merged.itemPhysics, request.itemPhysics);
        take(merged.swingDuration, request.swingDuration);
        take(merged.heldOffset, request.heldOffset);
        take(merged.heldScale, request.heldScale);
        take(merged.nametagScale, request.nametagScale);
        take(merged.ownNametag, request.ownNametag);
        take(merged.interfaceScale, request.interfaceScale);
    }
    return merged;
}

std::optional<BlockFilter> ModManager::takeHiddenBlocks()
{
    if (!host->hiddenChanged) {
        return std::nullopt;
    }
    host->hiddenChanged = false;
    BlockFilter filter;
    filter.visibleOnly = !host->visibleBlocks.empty();
    for (const auto& [owner, names] : filter.visibleOnly ? host->visibleBlocks : host->hiddenBlocks) {
        filter.names.insert(names.begin(), names.end());
    }
    return filter;
}

void ModManager::handleInput(InputState& input, bool inGame, float uiScale)
{
    host->input = &input;
    host->inGame = inGame;
    host->uiScale = uiScale > 0.0f ? uiScale : 1.0f;
    if (slots.empty()) {
        return;
    }
    auto modal = host->ui.top();
    if (modal) inGame = host->inGame = false;
    auto dispatch = [&](mod::Event& event) {
        if (modal) {
            if (host->ui.top() == modal) host->events.dispatchTo(modal->owner, event);
        } else host->events.dispatch(event);
    };
    if (input.pressedKey != Key::None) {
        mod::KeyPressEvent event;
        event.key = input.pressedKey;
        event.inGame = inGame;
        dispatch(event);
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
    if (input.releasedKey != Key::None) {
        mod::KeyReleaseEvent event;
        event.key = input.releasedKey;
        event.inGame = inGame;
        dispatch(event);
    }
    if (!modal || host->ui.top() == modal) dispatchText(input);
    else input.text.clear();
    auto click = [&](bool& pressed, mod::MouseButton button) {
        if (!pressed) {
            return;
        }
        mod::MouseClickEvent event;
        event.button = button;
        event.x = input.mouseX / host->uiScale;
        event.y = input.mouseY / host->uiScale;
        event.inGame = inGame;
        dispatch(event);
        if (event.isCancelled()) {
            pressed = false;
        }
    };
    float mouseX = input.mouseX / host->uiScale;
    float mouseY = input.mouseY / host->uiScale;
    if (mouseX != lastMouseX || mouseY != lastMouseY) {
        mod::MouseMoveEvent event;
        event.x = mouseX;
        event.y = mouseY;
        event.deltaX = lastMouseX < 0.0f ? 0.0f : mouseX - lastMouseX;
        event.deltaY = lastMouseY < 0.0f ? 0.0f : mouseY - lastMouseY;
        event.inGame = inGame;
        lastMouseX = mouseX;
        lastMouseY = mouseY;
        dispatch(event);
    }
    click(input.mousePressed, mod::MouseButton::Left);
    click(input.rightMousePressed, mod::MouseButton::Right);
    click(input.middleMousePressed, mod::MouseButton::Middle);
    auto release = [&](bool released, mod::MouseButton button) {
        if (!released) {
            return;
        }
        mod::MouseReleaseEvent event;
        event.button = button;
        event.x = mouseX;
        event.y = mouseY;
        event.inGame = inGame;
        dispatch(event);
    };
    release(input.mouseReleased, mod::MouseButton::Left);
    release(input.rightMouseReleased, mod::MouseButton::Right);
    release(input.middleMouseReleased, mod::MouseButton::Middle);
    if (input.wheel != 0.0f) {
        mod::MouseScrollEvent event;
        event.delta = input.wheel;
        event.x = input.mouseX / host->uiScale;
        event.y = input.mouseY / host->uiScale;
        event.inGame = inGame;
        dispatch(event);
        if (event.isCancelled()) {
            input.wheel = 0.0f;
        }
    }
    if (modal || host->ui.open()) {
        host->ui.capture(input, host->uiScale);
        if (!modal) {
            if (auto screen = host->ui.top()) screen->controls.begin({}, host->uiScale);
        }
    }

}

void ModManager::observe(const SessionSnapshot& snapshot)
{
    if (slots.empty()) {
        seenActors.clear();
        inventorySeen = false;
        seenBlockChangeSerial = snapshot.blockChangeSerial;
        seenPlayerTickSerial = snapshot.playerTickSerial;
        seenBreakProgressSerial = snapshot.breakProgressSerial;
        seenColumns.clear();
        seenLoadedRevision = 0;
        return;
    }
    if (snapshot.state != host->snapshot.state || snapshot.joinCount != host->snapshot.joinCount || snapshot.dimension != host->snapshot.dimension) {
        host->effects.clear();
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
        forgetActors();
        forgetChunks();
        inventorySeen = false;
        seenBlockChangeSerial = snapshot.blockChangeSerial;
        seenPlayerTickSerial = snapshot.playerTickSerial;
        seenBreakProgressSerial = snapshot.breakProgressSerial;
        return;
    }
    if (snapshot.joinCount != lastJoin) {
        forgetActors();
        forgetChunks();
        inventorySeen = false;
        lastJoin = snapshot.joinCount;
        lastDimension = snapshot.dimension;
        mod::JoinEvent event;
        event.server = snapshot.name;
        event.address = snapshot.target;
        host->events.dispatch(event);
    }
    if (snapshot.dimension != lastDimension) {
        forgetActors();
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
    trackActors(snapshot);
    trackInventory(snapshot);
    trackChunks(snapshot);
    trackBlockChanges(snapshot);
    trackPlayerTicks(snapshot);
    trackBreakProgress(snapshot);
    trackContainer(snapshot);
}

/**
 * Sends the block changes the session recorded since the last frame, oldest
 * first; a frame late enough to miss some only sends those still kept.
 */
void ModManager::trackBlockChanges(const SessionSnapshot& snapshot)
{
    uint64_t seen = std::exchange(seenBlockChangeSerial, snapshot.blockChangeSerial);
    if (snapshot.blockChangeSerial <= seen || !snapshot.blockChanges || !host->events.listening(mod::BlockChangeEvent::Type)) {
        return;
    }
    std::shared_ptr<const std::vector<BlockChangeView>> list = snapshot.blockChanges;
    size_t fresh = static_cast<size_t>(std::min<uint64_t>(snapshot.blockChangeSerial - seen, list->size()));
    for (size_t i = list->size() - fresh; i < list->size(); ++i) {
        const BlockChangeView& change = (*list)[i];
        mod::BlockChangeEvent event;
        event.position = { change.position[0], change.position[1], change.position[2] };
        event.oldName = change.oldName;
        event.newName = change.newName;
        event.predicted = change.predicted;
        host->events.dispatch(event);
    }
}

/**
 * Compares the loaded columns with the ones last seen whenever the session
 * publishes a new world, and reports the ones that came and went.
 */
void ModManager::trackChunks(const SessionSnapshot& snapshot)
{
    const std::shared_ptr<const LoadedBlocks>& area = snapshot.loaded;
    if (!area || area->revision == seenLoadedRevision) {
        return;
    }
    if (area->dimension != seenColumnsDimension) {
        forgetChunks();
    }
    seenLoadedRevision = area->revision;
    seenColumnsDimension = area->dimension;
    if (!host->events.listening(mod::ChunkLoadEvent::Type) && !host->events.listening(mod::ChunkUnloadEvent::Type)) {
        seenColumns = area->columns;
        return;
    }
    std::vector<std::array<int32_t, 2>> gone;
    std::vector<std::array<int32_t, 2>> arrived;
    std::set_difference(seenColumns.begin(), seenColumns.end(), area->columns.begin(), area->columns.end(), std::back_inserter(gone));
    std::set_difference(area->columns.begin(), area->columns.end(), seenColumns.begin(), seenColumns.end(), std::back_inserter(arrived));
    seenColumns = area->columns;
    for (const std::array<int32_t, 2>& column : gone) {
        mod::ChunkUnloadEvent event;
        event.x = column[0];
        event.z = column[1];
        event.dimension = area->dimension;
        host->events.dispatch(event);
    }
    for (const std::array<int32_t, 2>& column : arrived) {
        mod::ChunkLoadEvent event;
        event.x = column[0];
        event.z = column[1];
        event.dimension = area->dimension;
        host->events.dispatch(event);
    }
}

void ModManager::forgetChunks()
{
    std::vector<std::array<int32_t, 2>> gone = std::exchange(seenColumns, {});
    seenLoadedRevision = 0;
    for (const std::array<int32_t, 2>& column : gone) {
        mod::ChunkUnloadEvent event;
        event.x = column[0];
        event.z = column[1];
        event.dimension = seenColumnsDimension;
        host->events.dispatch(event);
    }
}

/**
 * Sends one event per movement tick the session ran since the last frame.
 */
void ModManager::trackPlayerTicks(const SessionSnapshot& snapshot)
{
    uint64_t seen = std::exchange(seenPlayerTickSerial, snapshot.playerTickSerial);
    if (snapshot.playerTickSerial <= seen || !snapshot.playerTicks || !host->events.listening(mod::PlayerTickEvent::Type)) {
        return;
    }
    std::shared_ptr<const std::vector<PlayerTickView>> list = snapshot.playerTicks;
    size_t fresh = static_cast<size_t>(std::min<uint64_t>(snapshot.playerTickSerial - seen, list->size()));
    for (size_t i = list->size() - fresh; i < list->size(); ++i) {
        const PlayerTickView& tick = (*list)[i];
        mod::PlayerTickEvent event;
        event.tick = tick.tick;
        event.position = { tick.position[0], tick.position[1], tick.position[2] };
        event.velocity = { tick.velocity[0], tick.velocity[1], tick.velocity[2] };
        event.onGround = tick.onGround;
        host->events.dispatch(event);
    }
}

/**
 * Sends the breaking progress the session recorded since the last frame,
 * oldest first.
 */
void ModManager::trackBreakProgress(const SessionSnapshot& snapshot)
{
    uint64_t seen = std::exchange(seenBreakProgressSerial, snapshot.breakProgressSerial);
    if (snapshot.breakProgressSerial <= seen || !snapshot.breakProgress || !host->events.listening(mod::BlockBreakProgressEvent::Type)) {
        return;
    }
    std::shared_ptr<const std::vector<BreakProgress>> list = snapshot.breakProgress;
    size_t fresh = static_cast<size_t>(std::min<uint64_t>(snapshot.breakProgressSerial - seen, list->size()));
    for (size_t i = list->size() - fresh; i < list->size(); ++i) {
        const BreakProgress& progress = (*list)[i];
        mod::BlockBreakProgressEvent event;
        event.position = { progress.cell[0], progress.cell[1], progress.cell[2] };
        event.progress = progress.progress;
        event.finished = progress.finished;
        event.aborted = progress.aborted;
        host->events.dispatch(event);
    }
}

/**
 * Sends the open container's slots when it opens and whenever one of them
 * holds something else than in the last frame.
 */
void ModManager::trackContainer(const SessionSnapshot& snapshot)
{
    const InventoryState& open = snapshot.hud.container;
    if (open.windowId == 0) {
        seenContainer.clear();
        return;
    }
    size_t size = static_cast<size_t>(std::clamp(open.containerSize, 0, inventory::Ui - inventory::Container));
    auto first = open.slots.begin() + inventory::Container;
    bool reopened = std::exchange(seenContainerOpen, open.openRevision) != open.openRevision;
    if (!reopened && seenContainer.size() == size && std::equal(seenContainer.begin(), seenContainer.end(), first)) {
        return;
    }
    seenContainer.assign(first, first + static_cast<std::ptrdiff_t>(size));
    if (!host->events.listening(mod::ContainerContentEvent::Type)) {
        return;
    }
    mod::ContainerContentEvent event;
    event.containerType = static_cast<int>(open.type);
    event.slots.reserve(size);
    for (const HudItem& item : seenContainer) {
        event.slots.push_back(itemOf(item));
    }
    host->events.dispatch(event);
}

void ModManager::trackActors(const SessionSnapshot& snapshot)
{
    bool spawns = host->events.listening(mod::EntitySpawnEvent::Type);
    std::map<uint64_t, SeenActor> current;
    for (const ActorView& actor : snapshot.actors) {
        SeenActor seen;
        seen.uniqueId = actor.uniqueId;
        seen.identifier = actor.identifier;
        seen.name = actor.name;
        seen.position = positionOf(actor);
        if (spawns && !seenActors.contains(actor.runtimeId)) {
            mod::EntitySpawnEvent event;
            event.runtimeId = actor.runtimeId;
            event.uniqueId = seen.uniqueId;
            event.identifier = seen.identifier;
            event.name = seen.name;
            event.position = seen.position;
            event.isPlayer = seen.identifier == "minecraft:player";
            host->events.dispatch(event);
        }
        current.emplace(actor.runtimeId, std::move(seen));
    }
    for (auto& [runtimeId, seen] : seenActors) {
        if (current.contains(runtimeId)) {
            continue;
        }
        mod::EntityRemoveEvent event;
        event.runtimeId = runtimeId;
        event.uniqueId = seen.uniqueId;
        event.identifier = std::move(seen.identifier);
        event.name = std::move(seen.name);
        event.position = seen.position;
        event.isPlayer = event.identifier == "minecraft:player";
        host->events.dispatch(event);
    }
    seenActors = std::move(current);
}

void ModManager::forgetActors()
{
    std::map<uint64_t, SeenActor> gone = std::exchange(seenActors, {});
    for (auto& [runtimeId, seen] : gone) {
        mod::EntityRemoveEvent event;
        event.runtimeId = runtimeId;
        event.uniqueId = seen.uniqueId;
        event.identifier = std::move(seen.identifier);
        event.name = std::move(seen.name);
        event.position = seen.position;
        event.isPlayer = event.identifier == "minecraft:player";
        host->events.dispatch(event);
    }
}

void ModManager::trackInventory(const SessionSnapshot& snapshot)
{
    const HudState& hud = snapshot.hud;
    const HudItem& cursor = hud.container.slots[inventory::Cursor];
    if (!inventorySeen || !host->events.listening(mod::InventoryChangeEvent::Type)) {
        seenInventory = hud.inventory;
        seenArmor = hud.armor;
        seenOffhand = hud.offhand;
        seenCursor = cursor;
        inventorySeen = true;
        return;
    }
    auto compare = [&](HudItem& seen, const HudItem& now, mod::InventoryKind kind, int slot) {
        if (seen.identifier == now.identifier && seen.count == now.count && seen.aux == now.aux) {
            seen = now;
            return;
        }
        mod::InventoryChangeEvent event;
        event.kind = kind;
        event.slot = slot;
        event.oldIdentifier = seen.empty() ? std::string() : seen.identifier;
        event.oldCount = seen.empty() ? 0 : seen.count;
        event.oldAux = seen.empty() ? 0 : seen.aux;
        event.newIdentifier = now.empty() ? std::string() : now.identifier;
        event.newCount = now.empty() ? 0 : now.count;
        event.newAux = now.empty() ? 0 : now.aux;
        seen = now;
        host->events.dispatch(event);
    };
    for (size_t i = 0; i < seenInventory.size(); ++i) {
        compare(seenInventory[i], hud.inventory[i], mod::InventoryKind::Main, static_cast<int>(i));
    }
    for (size_t i = 0; i < seenArmor.size(); ++i) {
        compare(seenArmor[i], hud.armor[i], mod::InventoryKind::Armor, static_cast<int>(i));
    }
    compare(seenOffhand, hud.offhand, mod::InventoryKind::Offhand, 0);
    compare(seenCursor, cursor, mod::InventoryKind::Cursor, 0);
}

void ModManager::dispatchText(InputState& input)
{
    if (input.text.empty() || !host->events.listening(mod::TextInputEvent::Type)) {
        return;
    }
    std::u32string kept;
    auto screen = host->ui.top();
    for (char32_t codepoint : input.text) {
        if (screen && host->ui.top() != screen) break;
        mod::TextInputEvent event;
        event.codepoint = codepoint;
        event.text = utf8(codepoint);
        event.inGame = host->inGame;
        if (screen) host->events.dispatchTo(screen->owner, event);
        else host->events.dispatch(event);
        if (!event.isCancelled()) {
            kept += codepoint;
        }
    }
    input.text = std::move(kept);
}

/**
 * Works out which screen, dialog or overlay is in front and, when that
 * changed since the last frame, closes the old one for the mods and opens
 * the new one.
 */
void ModManager::trackScreen()
{
    menu::Menu& menu = host->menu;
    mod::ScreenKind current = mod::ScreenKind::None;
    int containerType = -1;
    if (menu.formPanel().active()) {
        current = mod::ScreenKind::Form;
    } else if (menu.inventoryOpen()) {
        ContainerType type = menu.inventoryPanel().state.type;
        current = type == ContainerType::Inventory ? mod::ScreenKind::Inventory : mod::ScreenKind::Container;
        if (current == mod::ScreenKind::Container) {
            containerType = static_cast<int>(type);
        }
    } else if (menu.currentDialog() != menu::Dialog::None) {
        current = dialogKind(menu.currentDialog());
    } else if (menu.socialDrawerOpen()) {
        current = mod::ScreenKind::Social;
    } else if (!menu.worldVisible() || menu.currentScreen() != menu::Screen::Title) {
        current = screenKind(menu.currentScreen());
    }
    if (static_cast<int>(current) == lastScreen && containerType == lastContainerType) {
        return;
    }
    if (lastScreen != static_cast<int>(mod::ScreenKind::None)) {
        mod::ScreenCloseEvent closed;
        closed.screen = static_cast<mod::ScreenKind>(lastScreen);
        closed.containerType = lastContainerType;
        host->events.dispatch(closed);
    }
    lastScreen = static_cast<int>(current);
    lastContainerType = containerType;
    if (current != mod::ScreenKind::None) {
        mod::ScreenOpenEvent opened;
        opened.screen = current;
        opened.containerType = containerType;
        host->events.dispatch(opened);
    }
}

void ModManager::settingsChanged(uint32_t changed, int fov, float guiScale, int renderDistance, const std::string& language)
{
    if (slots.empty() || changed == 0) {
        return;
    }
    mod::SettingsChangedEvent event;
    event.changed = changed;
    event.fov = fov;
    event.guiScale = guiScale;
    event.renderDistance = renderDistance;
    event.language = language;
    host->events.dispatch(event);
}

void ModManager::reloadConfigs(const std::string& modId)
{
    for (const auto& running : slots) {
        if (modId.empty() || running->info().id == modId) {
            running->configStore().load();
        }
    }
    if (slots.empty()) {
        return;
    }
    mod::ConfigReloadEvent event;
    event.modId = modId;
    host->events.dispatch(event);
}

void ModManager::shutdown()
{
    if (std::exchange(shutDown, true) || slots.empty()) {
        return;
    }
    mod::ShutdownEvent event;
    host->events.dispatch(event);
}

void ModManager::update(float deltaSeconds)
{
    applyActions();
    if (slots.empty()) {
        return;
    }
    double now = secondsNow();
    host->seconds = now;
    host->shaders.clear();
    drainPackets();
    trackScreen();

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
    event.rotation = { input.yaw, input.pitch };
    host->events.dispatch(event);
    input.forward = std::clamp(event.forward, -1.0f, 1.0f);
    input.sideways = std::clamp(event.sideways, -1.0f, 1.0f);
    input.jump = event.jump;
    input.sneak = event.sneak;
    input.sprint = event.sprint;
    input.startGlide = event.startGlide;
    input.stopGlide = event.stopGlide;
    input.startFlying = event.startFlying;
    input.stopFlying = event.stopFlying;
    input.swimDown = event.swimDown;
    if (event.overrideRotation && std::isfinite(event.rotation.yaw) && std::isfinite(event.rotation.pitch)) {
        input.yaw = event.rotation.yaw;
        input.pitch = std::clamp(event.rotation.pitch, -90.0f, 90.0f);
        input.rotationOverridden = true;
    }
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
    host->interfaceWidth = width;
    host->interfaceHeight = height;
    bool listening = host->events.listening(mod::HudRenderEvent::Type);
    if (slots.empty() || (!listening && host->notices.empty())) {
        return;
    }
    hudWidth = width;
    hudHeight = height;
    context.setOrigin(0.0f, 0.0f);
    context.clearLayer();
    context.clearClip();
    UiCanvas canvas(context, host->shaders, width, height);
    if (listening) {
        mod::HudRenderEvent event(canvas);
        event.screenOpen = screenOpen;
        host->events.dispatch(event);
        context.clearClip();
    }
    drawNotices(canvas, width);
    context.clearClip();
}

/**
 * The mods' Hud::notify notifications, stacked down the top right corner on
 * a dark strip with a green edge, each fading out over its last half second.
 */
void ModManager::drawNotices(mod::Canvas& canvas, float width)
{
    double now = host->seconds;
    std::erase_if(host->notices, [now](const HudNotice& notice) { return notice.until <= now; });
    float lineHeight = canvas.lineHeight(mod::TextStyle::Ui);
    float y = 6.0f;
    for (const HudNotice& notice : host->notices) {
        float alpha = static_cast<float>(std::clamp((notice.until - now) / NoticeFadeSeconds, 0.0, 1.0));
        float boxWidth = std::min(canvas.measure(notice.text, mod::TextStyle::Ui) + 16.0f, std::max(width * 0.4f, 60.0f));
        mod::Rect box { width - boxWidth - 6.0f, y, boxWidth, lineHeight + 10.0f };
        canvas.fill(box, { 16, 16, 16, static_cast<uint8_t>(210.0f * alpha) });
        canvas.fill({ box.x, box.y, 2.0f, box.h }, { 100, 180, 70, static_cast<uint8_t>(255.0f * alpha) });
        canvas.setClip(box.inset(2.0f));
        canvas.text(notice.text, box.x + 8.0f, box.y + 5.0f, { 255, 255, 255, static_cast<uint8_t>(255.0f * alpha) }, mod::TextStyle::Ui, false);
        canvas.clearClip();
        y += box.h + 4.0f;
    }
}

void ModManager::drawWorld(const std::array<float, 16>& viewProjection, const mod::Vec3& camera, const std::function<void(mod::WorldPainter&)>& client)
{
    host->worldViewProjection = viewProjection;
    host->worldCamera = camera;
    bool modsDraw = !slots.empty() && host->events.listening(mod::WorldRenderEvent::Type);
    if (!modsDraw && !client) {
        return;
    }
    WorldCanvas painter(host->shaders, host->bridge.font, camera, viewProjection);
    if (client) {
        client(painter);
    }
    if (modsDraw) {
        mod::WorldRenderEvent event(painter);
        host->events.dispatch(event);
    }
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
    if (owner == HostOwner || faulted.contains(owner)) {
        return;
    }
    double now = secondsNow();
    std::deque<double>& times = errorTimes[owner];
    times.push_back(now);
    while (!times.empty() && now - times.front() > ErrorWindowSeconds) {
        times.pop_front();
    }
    if (times.size() > MaxErrorsPerWindow) {
        faulted.insert(owner);
        host->menu.addChatLine("§c" + name + " failed " + std::to_string(times.size()) + " times in a minute and is being turned off");
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
    mod::CommandSpec mods { "mods", "Lists the loaded mods, reloads one or opens its settings", "[reload|settings <mod id>]", {} };
    mods.complete = [this](const mod::CommandContext& context) {
        std::vector<std::string> options;
        if (context.args.size() == 1) {
            options = { "reload", "settings" };
        } else if (context.args.size() == 2) {
            for (const Record& entry : records) {
                if (!entry.info.id.empty()) {
                    options.push_back(entry.info.id);
                }
            }
        }
        return options;
    };
    host->commands.add(HostOwner, std::move(mods), [this](mod::CommandContext& context) {
        if (context.args.empty()) {
            context.reply("§e" + std::to_string(host->loaded.size()) + " mods loaded:");
            for (const mod::ModInfo& info : host->loaded) {
                std::string line = "§a" + (info.name.empty() ? info.id : info.name) + " §7" + info.version;
                if (!info.author.empty()) {
                    line += " by " + info.author;
                }
                context.reply(line);
            }
            return;
        }
        const std::string& action = context.arg(0);
        if (action != "reload" && action != "settings") {
            throw mod::CommandError("Unknown action " + action + ", use reload or settings");
        }
        const std::string& id = context.arg(1);
        auto found = std::find_if(records.begin(), records.end(), [&](const Record& entry) { return entry.info.id == id; });
        if (found == records.end()) {
            throw mod::CommandError("No mod has the id " + id);
        }
        if (action == "reload") {
            pending.push_back({ menu::ModAction::Kind::Reload, found->file.filename().string(), {}, {} });
            context.reply("§eReloading " + id);
            return;
        }
        if (found->owner == 0 || !host->settings.contains(found->owner)) {
            throw mod::CommandError(id + " has no settings page");
        }
        pending.push_back({ menu::ModAction::Kind::OpenSettings, found->file.filename().string(), {}, {} });
    });
    mod::CommandSpec reloadConfig { "reloadconfig", "Asks the mods to read their settings again", "[mod id]", {} };
    reloadConfig.complete = [this](const mod::CommandContext& context) {
        std::vector<std::string> ids;
        if (context.args.size() == 1) {
            for (const mod::ModInfo& info : host->loaded) {
                ids.push_back(info.id);
            }
        }
        return ids;
    };
    host->commands.add(HostOwner, std::move(reloadConfig), [this](mod::CommandContext& context) {
        std::string target = context.args.empty() ? std::string() : context.args.front();
        host->scheduler.post(HostOwner, [this, target] { reloadConfigs(target); });
        context.reply(target.empty() ? "§eReloading every mod's config" : "§eReloading the config of " + target);
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

namespace kestrel::modding {

EmoteRegistry& ModManager::emotes()
{
    return host->emotes;
}

}
