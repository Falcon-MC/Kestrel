#include "ModServices.h"

#include "client/DebugLog.h"
#include "mod/Config.h"
#include "menu/Menu.h"
#include "mod/Events.h"
#include "platform/Input.h"
#include "platform/Keys.h"

#include "Protocol/MinecraftPacketIds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace kestrel::modding {

namespace {

mod::Entity entityOf(const ActorView& actor)
{
    mod::Entity entity;
    entity.runtimeId = actor.runtimeId;
    entity.identifier = actor.identifier;
    entity.name = actor.name;
    entity.position = { actor.x, actor.y, actor.z };
    entity.rotation = { actor.yaw, actor.pitch };
    entity.headYaw = actor.headYaw;
    entity.width = actor.width;
    entity.height = actor.height;
    entity.scale = actor.scale;
    entity.onGround = actor.onGround;
    entity.armor = actor.armor;
    entity.item = itemOf(actor.item);
    return entity;
}

bool joined(const HostState& host)
{
    return host.snapshot.state == SessionState::Joined;
}

}

mod::ItemStack itemOf(const HudItem& item)
{
    mod::ItemStack stack;
    stack.identifier = item.identifier;
    stack.count = item.count;
    stack.aux = item.aux;
    stack.damage = item.damage;
    stack.customName = item.customName;
    stack.lore = item.lore;
    stack.enchanted = item.enchanted;
    return stack;
}

EventBusService::EventBusService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

mod::Subscription EventBusService::subscribe(std::string_view type, Handler handler, mod::ListenOptions options)
{
    return host.events.subscribe(owner, type, std::move(handler), options);
}

void EventBusService::post(mod::Event& event)
{
    host.events.dispatch(event);
}

ChatService::ChatService(HostState& host)
    : host(host)
{
}

void ChatService::send(std::string_view text)
{
    if (!text.empty()) {
        host.session.sendChat(std::string(text));
    }
}

void ChatService::print(std::string_view text)
{
    host.menu.addChatLine(std::string(text));
}

void ChatService::toast(std::string_view title, std::string_view content)
{
    host.menu.pushToast(std::string(title), std::string(content));
}

void ChatService::title(std::string_view title, std::string_view subtitle)
{
    if (host.bridge.showTitle) {
        host.bridge.showTitle(std::string(title), std::string(subtitle));
    }
}

void ChatService::actionbar(std::string_view text)
{
    if (host.bridge.showActionbar) {
        host.bridge.showActionbar(std::string(text));
    }
}

std::vector<std::string> ChatService::history() const
{
    return host.menu.chatLog();
}

PlayerService::PlayerService(HostState& host)
    : host(host)
{
}

bool PlayerService::inWorld() const
{
    return joined(host) && host.snapshot.player.active;
}

std::string PlayerService::name() const
{
    return host.snapshot.displayName;
}

uint64_t PlayerService::runtimeId() const
{
    return host.snapshot.localRuntimeId;
}

mod::Vec3 PlayerService::position() const
{
    mod::Vec3 eye = eyePosition();
    if (inWorld()) {
        eye.y -= host.snapshot.player.eyeHeight();
    }
    return eye;
}

mod::Vec3 PlayerService::eyePosition() const
{
    return host.bridge.eyePosition ? host.bridge.eyePosition() : mod::Vec3 {};
}

mod::Rotation PlayerService::rotation() const
{
    return host.bridge.rotation ? host.bridge.rotation() : mod::Rotation {};
}

void PlayerService::setRotation(mod::Rotation rotation)
{
    if (host.bridge.setRotation) {
        host.bridge.setRotation(rotation);
    }
}

float PlayerService::health() const
{
    return host.snapshot.hud.health;
}

float PlayerService::maxHealth() const
{
    return host.snapshot.hud.maxHealth;
}

float PlayerService::absorption() const
{
    return host.snapshot.hud.absorption;
}

float PlayerService::hunger() const
{
    return host.snapshot.hud.hunger;
}

float PlayerService::saturation() const
{
    return host.snapshot.hud.saturation;
}

int PlayerService::level() const
{
    return host.snapshot.hud.level;
}

float PlayerService::experience() const
{
    return host.snapshot.hud.experience;
}

int PlayerService::air() const
{
    return host.snapshot.hud.air;
}

std::string PlayerService::gameMode() const
{
    return host.snapshot.gameMode;
}

int PlayerService::dimension() const
{
    return host.snapshot.dimension;
}

bool PlayerService::dead() const
{
    return joined(host) && host.snapshot.dead;
}

bool PlayerService::onGround() const
{
    return host.snapshot.player.onGround;
}

bool PlayerService::sneaking() const
{
    return host.snapshot.player.sneaking;
}

bool PlayerService::sprinting() const
{
    return host.snapshot.player.sprinting;
}

bool PlayerService::swimming() const
{
    return host.snapshot.player.swimming;
}

bool PlayerService::flying() const
{
    return host.snapshot.player.flying;
}

int PlayerService::selectedSlot() const
{
    return host.snapshot.hud.selectedSlot;
}

void PlayerService::selectSlot(int slot)
{
    if (slot >= 0 && slot < HotbarSize) {
        host.session.selectHotbarSlot(slot);
    }
}

mod::ItemStack PlayerService::inventory(int slot) const
{
    const auto& items = host.snapshot.hud.inventory;
    return slot >= 0 && slot < static_cast<int>(items.size()) ? itemOf(items[static_cast<size_t>(slot)]) : mod::ItemStack {};
}

mod::ItemStack PlayerService::armor(int slot) const
{
    const auto& items = host.snapshot.hud.armor;
    return slot >= 0 && slot < static_cast<int>(items.size()) ? itemOf(items[static_cast<size_t>(slot)]) : mod::ItemStack {};
}

mod::ItemStack PlayerService::offhand() const
{
    return itemOf(host.snapshot.hud.offhand);
}

std::vector<mod::StatusEffect> PlayerService::effects() const
{
    std::vector<mod::StatusEffect> list;
    double now = secondsNow();
    for (const HudEffect& effect : host.snapshot.hud.effects) {
        list.push_back({ effect.id, effect.amplifier, effect.expires < 0.0 ? -1.0 : std::max(effect.expires - now, 0.0), effect.ambient });
    }
    return list;
}

void PlayerService::attack()
{
    host.session.requestInteraction(false);
}

void PlayerService::use()
{
    host.session.requestInteraction(true);
}

void PlayerService::pickBlock(bool withData)
{
    host.session.requestPickBlock(withData);
}

void PlayerService::dropHeld(bool wholeStack)
{
    host.session.requestInventory({ InventoryAction::Drop, host.snapshot.hud.selectedSlot, 0, wholeStack, {} });
}

void PlayerService::respawn()
{
    host.session.requestRespawn();
}

WorldService::WorldService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

mod::ConnectionState WorldService::state() const
{
    return static_cast<mod::ConnectionState>(host.snapshot.state);
}

std::string WorldService::serverName() const
{
    return host.snapshot.name;
}

std::string WorldService::serverAddress() const
{
    return host.snapshot.target;
}

std::string WorldService::levelName() const
{
    return host.snapshot.levelName;
}

int WorldService::dimension() const
{
    return host.snapshot.dimension;
}

int64_t WorldService::time() const
{
    return static_cast<int64_t>(currentWorldTime(host.snapshot));
}

float WorldService::rain() const
{
    return host.snapshot.rainLevel;
}

float WorldService::thunder() const
{
    return host.snapshot.thunderLevel;
}

std::vector<mod::Entity> WorldService::entities() const
{
    std::vector<mod::Entity> list;
    if (!joined(host)) {
        return list;
    }
    list.reserve(host.snapshot.actors.size());
    for (const ActorView& actor : host.snapshot.actors) {
        list.push_back(entityOf(actor));
    }
    return list;
}

std::optional<mod::Entity> WorldService::entity(uint64_t runtimeId) const
{
    if (!joined(host)) {
        return std::nullopt;
    }
    for (const ActorView& actor : host.snapshot.actors) {
        if (actor.runtimeId == runtimeId) {
            return entityOf(actor);
        }
    }
    return std::nullopt;
}

std::optional<mod::TargetBlock> WorldService::targetBlock() const
{
    if (!joined(host) || !host.snapshot.targetBlock) {
        return std::nullopt;
    }
    const TargetBlock& target = *host.snapshot.targetBlock;
    return mod::TargetBlock { { target.cell[0], target.cell[1], target.cell[2] }, target.name, target.states };
}

std::vector<std::string> WorldService::players() const
{
    return joined(host) ? host.snapshot.players : std::vector<std::string> {};
}

mod::Sidebar WorldService::sidebar() const
{
    const SidebarView& view = host.snapshot.sidebar;
    return { view.visible && joined(host), view.title, view.lines };
}

mod::Environment WorldService::environment() const
{
    return host.environment;
}

NetworkService::NetworkService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

void NetworkService::connect(std::string_view address, std::string_view name)
{
    if (host.bridge.connect && !address.empty()) {
        host.bridge.connect(std::string(name.empty() ? address : name), std::string(address));
    }
}

void NetworkService::disconnect()
{
    host.session.disconnect();
}

void NetworkService::sendRaw(std::string payload)
{
    host.session.sendRawPacket(std::move(payload));
}

void NetworkService::answerForm(uint32_t id, std::optional<std::string> json)
{
    host.session.answerForm(id, std::move(json), false);
}

mod::Subscription NetworkService::addFilter(std::shared_ptr<mod::PacketFilter> filter)
{
    return host.packets->add(owner, std::move(filter));
}

std::string NetworkService::packetName(int id) const
{
    const char* name = toString(static_cast<MinecraftPacketIds>(id));
    return name ? name : std::to_string(id);
}

std::string_view NetworkService::gameVersion() const
{
    return Session::gameVersion();
}

InputService::InputService(HostState& host, size_t owner, mod::Config& config)
    : host(host)
    , owner(owner)
    , config(config)
{
}

bool InputService::isHeld(mod::Key key) const
{
    return host.input && host.input->isHeld(key);
}

bool InputService::inGame() const
{
    return host.inGame;
}

float InputService::mouseX() const
{
    return host.input ? host.input->mouseX / host.uiScale : -1.0f;
}

float InputService::mouseY() const
{
    return host.input ? host.input->mouseY / host.uiScale : -1.0f;
}

void InputService::setCursorFree(bool free)
{
    if (free) {
        host.cursorOwners.insert(owner);
    } else {
        host.cursorOwners.erase(owner);
    }
}

bool InputService::cursorFree() const
{
    return host.cursorOwners.count(owner) != 0;
}

mod::Subscription InputService::bind(mod::Key key, std::function<void()> action)
{
    return host.events.subscribe(owner, mod::KeyPressEvent::Type, [key, action = std::move(action)](mod::Event& event) {
        auto& press = static_cast<mod::KeyPressEvent&>(event);
        if (press.inGame && press.key == key) {
            press.cancel();
            action();
        }
    }, {});
}

mod::Subscription InputService::bind(mod::KeyBindSpec spec, std::function<void()> action)
{
    if (spec.id.empty() || spec.id.find(' ') != std::string::npos || spec.id.find(':') != std::string::npos) {
        throw std::invalid_argument("A bind id is one word without ':', got \"" + spec.id + "\"");
    }
    Key key = spec.defaultKey;
    if (std::optional<std::string> saved = config.find("bind." + spec.id)) {
        Key named = keyFromName(*saved);
        if (named != Key::None) {
            key = named;
        }
    }
    std::string modId;
    std::string modName;
    if (owner > 0 && owner <= host.loaded.size()) {
        modId = host.loaded[owner - 1].id;
        modName = host.loaded[owner - 1].name;
    }
    std::string bindId = spec.id;
    host.keyBinds.add(owner, std::move(modId), std::move(modName), spec, key, &config);
    return host.events.subscribe(owner, mod::KeyPressEvent::Type, [this, bindId = std::move(bindId), action = std::move(action)](mod::Event& event) {
        auto& press = static_cast<mod::KeyPressEvent&>(event);
        if (press.inGame && press.key == host.keyBinds.current(owner, bindId) && press.key != Key::None) {
            press.cancel();
            action();
        }
    }, {});
}

CommandService::CommandService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

mod::Subscription CommandService::add(mod::CommandSpec spec, Handler handler)
{
    if (spec.name.empty() || spec.name.find(' ') != std::string::npos) {
        throw std::invalid_argument("A command name is one word, got \"" + spec.name + "\"");
    }
    return host.commands.add(owner, std::move(spec), std::move(handler));
}

LoggerService::LoggerService(std::string id)
    : id(std::move(id))
{
}

void LoggerService::log(Level level, std::string_view message)
{
    const char* label = level == Level::Error ? "error" : level == Level::Warning ? "warning" : "info";
    std::string line = "mod " + id + " " + label + ": " + std::string(message);
    debugLog(line);
    std::fprintf(stderr, "%s\n", line.c_str());
}

SchedulerService::SchedulerService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

mod::Subscription SchedulerService::after(double seconds, Task task)
{
    return host.scheduler.schedule(owner, host.seconds, seconds, false, std::move(task));
}

mod::Subscription SchedulerService::every(double seconds, Task task)
{
    return host.scheduler.schedule(owner, host.seconds, seconds, true, std::move(task));
}

void SchedulerService::post(Task task)
{
    host.scheduler.post(owner, std::move(task));
}

ShaderService::ShaderService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

std::shared_ptr<mod::Shader> ShaderService::create(const mod::ShaderSource& source)
{
    return host.shaders.create(owner, source, false);
}

std::shared_ptr<mod::Shader> ShaderService::createPost(const mod::ShaderSource& source)
{
    return host.shaders.create(owner, source, true);
}

std::string_view ShaderService::backend() const
{
    return host.shaders.backend();
}


bool WorldService::isLoaded(const mod::BlockPos& position) const
{
    const std::shared_ptr<const LoadedBlocks>& area = host.snapshot.loaded;
    return joined(host) && area && area->loaded(position.x, position.y, position.z);
}

std::optional<mod::BlockInfo> WorldService::block(const mod::BlockPos& position) const
{
    const std::shared_ptr<const LoadedBlocks>& area = host.snapshot.loaded;
    if (!joined(host) || !area || !area->loaded(position.x, position.y, position.z)) {
        return std::nullopt;
    }
    return mod::BlockInfo { position, area->name(position.x, position.y, position.z), area->states(position.x, position.y, position.z) };
}

/**
 * Walks the blocks the ray crosses one by one and stops at the first that
 * catches it, then checks the entities in front of it.
 */
std::optional<mod::RaycastHit> WorldService::raycast(const mod::Vec3& from, const mod::Vec3& direction, double reach, bool entities) const
{
    const std::shared_ptr<const LoadedBlocks>& area = host.snapshot.loaded;
    double length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (!joined(host) || length <= 0.0 || reach <= 0.0) {
        return std::nullopt;
    }
    std::array<double, 3> origin { from.x, from.y, from.z };
    std::array<double, 3> dir { direction.x / length, direction.y / length, direction.z / length };
    std::optional<mod::RaycastHit> best;
    if (area) {
        std::array<int32_t, 3> cell {};
        std::array<int32_t, 3> step {};
        std::array<double, 3> next {};
        std::array<double, 3> delta {};
        for (size_t axis = 0; axis < 3; ++axis) {
            cell[axis] = static_cast<int32_t>(std::floor(origin[axis]));
            step[axis] = dir[axis] > 0.0 ? 1 : (dir[axis] < 0.0 ? -1 : 0);
            delta[axis] = dir[axis] != 0.0 ? std::abs(1.0 / dir[axis]) : 1e30;
            double boundary = dir[axis] > 0.0 ? double(cell[axis] + 1) - origin[axis] : origin[axis] - double(cell[axis]);
            next[axis] = dir[axis] != 0.0 ? boundary * delta[axis] : 1e30;
        }
        static constexpr int Faces[3][2] = { { 4, 5 }, { 0, 1 }, { 2, 3 } };
        double travelled = 0.0;
        int enteredAxis = -1;
        while (travelled <= reach) {
            if (area->selectable(cell[0], cell[1], cell[2])) {
                mod::RaycastHit hit;
                hit.kind = mod::RaycastHit::Kind::Block;
                hit.distance = travelled;
                hit.point = { origin[0] + dir[0] * travelled, origin[1] + dir[1] * travelled, origin[2] + dir[2] * travelled };
                hit.block = { cell[0], cell[1], cell[2] };
                hit.face = enteredAxis < 0 ? 1 : Faces[enteredAxis][dir[enteredAxis] < 0.0 ? 1 : 0];
                hit.name = area->name(cell[0], cell[1], cell[2]);
                best = hit;
                break;
            }
            int axis = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2) : (next[1] < next[2] ? 1 : 2);
            travelled = next[axis];
            next[axis] += delta[axis];
            cell[axis] += step[axis];
            enteredAxis = axis;
        }
    }
    if (!entities) {
        return best;
    }
    double limit = best ? best->distance : reach;
    for (const ActorView& actor : host.snapshot.actors) {
        double half = (actor.width > 0.0f ? actor.width : 0.6f) * actor.scale * 0.5;
        double height = (actor.height > 0.0f ? actor.height : 1.8f) * actor.scale;
        std::array<double, 3> low { actor.x - half, actor.y, actor.z - half };
        std::array<double, 3> high { actor.x + half, actor.y + height, actor.z + half };
        double enter = 0.0;
        double leave = limit;
        bool crosses = true;
        for (size_t axis = 0; axis < 3 && crosses; ++axis) {
            if (std::abs(dir[axis]) < 1e-12) {
                crosses = origin[axis] >= low[axis] && origin[axis] <= high[axis];
                continue;
            }
            double a = (low[axis] - origin[axis]) / dir[axis];
            double b = (high[axis] - origin[axis]) / dir[axis];
            enter = std::max(enter, std::min(a, b));
            leave = std::min(leave, std::max(a, b));
            crosses = enter <= leave;
        }
        if (!crosses || enter > limit || enter <= 0.0) {
            continue;
        }
        mod::RaycastHit hit;
        hit.kind = mod::RaycastHit::Kind::Entity;
        hit.distance = enter;
        hit.point = { origin[0] + dir[0] * enter, origin[1] + dir[1] * enter, origin[2] + dir[2] * enter };
        hit.entity = actor.runtimeId;
        hit.name = actor.identifier;
        limit = enter;
        best = hit;
    }
    return best;
}

void WorldService::setBlockHidden(std::string_view name, bool hidden)
{
    std::string qualified(name);
    if (qualified.find(':') == std::string::npos) {
        qualified = "minecraft:" + qualified;
    }
    std::set<std::string>& names = host.hiddenBlocks[owner];
    bool changed = hidden ? names.insert(qualified).second : names.erase(qualified) != 0;
    if (names.empty()) {
        host.hiddenBlocks.erase(owner);
    }
    host.hiddenChanged |= changed;
}

void WorldService::clearHiddenBlocks()
{
    if (host.hiddenBlocks.erase(owner)) {
        host.hiddenChanged = true;
    }
}

std::vector<std::string> WorldService::blockNames() const
{
    const std::shared_ptr<const LoadedBlocks>& area = host.snapshot.loaded;
    if (!area || !area->assets) {
        return {};
    }
    return area->assets->blockNames();
}

void WorldService::setVisibleBlocks(const std::vector<std::string>& names)
{
    std::set<std::string> qualified;
    for (const std::string& name : names) {
        qualified.insert(name.find(':') == std::string::npos ? "minecraft:" + name : name);
    }
    auto found = host.visibleBlocks.find(owner);
    if (qualified.empty()) {
        if (found != host.visibleBlocks.end()) {
            host.visibleBlocks.erase(found);
            host.hiddenChanged = true;
        }
        return;
    }
    if (found != host.visibleBlocks.end() && found->second == qualified) {
        return;
    }
    host.visibleBlocks[owner] = std::move(qualified);
    host.hiddenChanged = true;
}

/**
 * Looks through the loaded sub-chunks near the center, skipping those whose
 * palettes hold none of the names, and keeps the nearest matches.
 */
std::vector<mod::FoundBlock> WorldService::findBlocks(const std::vector<std::string>& names, const mod::Vec3& center, double radius, size_t limit) const
{
    std::vector<mod::FoundBlock> found;
    const std::shared_ptr<const LoadedBlocks>& area = host.snapshot.loaded;
    if (!joined(host) || !area || !area->assets || names.empty() || radius <= 0.0 || limit == 0) {
        return found;
    }
    std::set<std::string> wanted;
    for (const std::string& name : names) {
        wanted.insert(name.find(':') == std::string::npos ? "minecraft:" + name : name);
    }
    std::unordered_map<uint32_t, const std::string*> matches;
    auto match = [&](uint32_t value) -> const std::string* {
        auto cached = matches.find(value);
        if (cached != matches.end()) {
            return cached->second;
        }
        const std::string* result = nullptr;
        if (value != world::ImplicitAir) {
            std::string name = area->assets->blockName(value, area->ids.hashed, area->ids.sequential.get());
            if (name.find(':') == std::string::npos) {
                name = "minecraft:" + name;
            }
            auto hit = wanted.find(name);
            result = hit == wanted.end() ? nullptr : &*hit;
        }
        matches.emplace(value, result);
        return result;
    };

    struct Candidate {
        double distance = 0.0;
        mod::BlockPos position;
        const std::string* name = nullptr;
    };
    std::vector<Candidate> candidates;
    double reach = radius * radius;
    double subChunkReach = (radius + 14.0) * (radius + 14.0);
    for (const auto& [key, subChunk] : area->subChunks) {
        if (!subChunk || subChunk->storages().empty()) {
            continue;
        }
        double cx = key.x * 16.0 + 8.0 - center.x;
        double cy = key.y * 16.0 + 8.0 - center.y;
        double cz = key.z * 16.0 + 8.0 - center.z;
        if (cx * cx + cy * cy + cz * cz > subChunkReach) {
            continue;
        }
        bool present = false;
        for (uint32_t value : subChunk->storages().front().palette()) {
            if (match(value)) {
                present = true;
                break;
            }
        }
        if (!present) {
            continue;
        }
        for (uint32_t y = 0; y < 16; ++y) {
            for (uint32_t z = 0; z < 16; ++z) {
                for (uint32_t x = 0; x < 16; ++x) {
                    const std::string* name = match(subChunk->runtimeId(0, x, y, z));
                    if (!name) {
                        continue;
                    }
                    mod::BlockPos position { key.x * 16 + int32_t(x), key.y * 16 + int32_t(y), key.z * 16 + int32_t(z) };
                    double dx = position.x + 0.5 - center.x;
                    double dy = position.y + 0.5 - center.y;
                    double dz = position.z + 0.5 - center.z;
                    double distance = dx * dx + dy * dy + dz * dz;
                    if (distance <= reach) {
                        candidates.push_back({ distance, position, name });
                    }
                }
            }
        }
    }
    size_t kept = std::min(limit, candidates.size());
    std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(kept), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.distance < b.distance;
    });
    found.reserve(kept);
    for (size_t i = 0; i < kept; ++i) {
        found.push_back({ candidates[i].position, *candidates[i].name });
    }
    return found;
}

CameraService::CameraService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

mod::Vec3 CameraService::position() const
{
    return host.viewPosition;
}

mod::Rotation CameraService::rotation() const
{
    return host.viewRotation;
}

void CameraService::detach(const mod::Vec3& position, mod::Rotation rotation)
{
    CameraRequest& request = host.cameras[owner];
    request.detached = true;
    request.position = position;
    request.rotation = rotation;
}

void CameraService::attach()
{
    auto found = host.cameras.find(owner);
    if (found == host.cameras.end()) {
        return;
    }
    found->second.detached = false;
    if (found->second.fovScale == 1.0f) {
        host.cameras.erase(found);
    }
}

bool CameraService::detached() const
{
    auto found = host.cameras.find(owner);
    return found != host.cameras.end() && found->second.detached;
}

void CameraService::setFovScale(float scale)
{
    CameraRequest& request = host.cameras[owner];
    request.fovScale = std::isfinite(scale) ? std::clamp(scale, 0.05f, 3.0f) : 1.0f;
    if (!request.detached && request.fovScale == 1.0f) {
        host.cameras.erase(owner);
    }
}

}
