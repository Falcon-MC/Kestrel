#include "ModServices.h"

#include "client/DebugLog.h"
#include "mod/Config.h"
#include "menu/Menu.h"
#include "mod/Events.h"
#include "platform/Input.h"
#include "platform/Keys.h"

#include "Protocol/MinecraftPacketIds.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <stdexcept>

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

WorldService::WorldService(HostState& host)
    : host(host)
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
    return host.input ? host.input->mouseX : -1.0f;
}

float InputService::mouseY() const
{
    return host.input ? host.input->mouseY : -1.0f;
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

}
