#include "modding/ModServices.h"

#include "client/DebugLog.h"
#include "client/session/SessionData.h"
#include "mod/Config.h"
#include "menu/Menu.h"
#include "mod/Events.h"
#include "platform/Input.h"
#include "platform/Keys.h"
#include "world/BlockBreaking.h"
#include "world/ItemInfo.h"

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

/**
 * Mods built against API 3 reach effects, screens and textures by posting
 * request events; they go to the same stores the API 4 services use.
 */
void EventBusService::post(mod::Event& event)
{
    if (event.type() == TextureRequest::Type) {
        host.textures.process(owner, static_cast<TextureRequest&>(event));
        return;
    }
    if (event.type() == UiRequest::Type) {
        host.ui.process(owner, static_cast<UiRequest&>(event));
        return;
    }
    if (event.type() == EffectRequest::Type) {
        host.effects.process(owner, static_cast<EffectRequest&>(event));
        return;
    }
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

int32_t PlayerService::maxDurability(const std::string& identifier) const
{
    return world::itemMaxDurability(identifier);
}

mod::Vec3 PlayerService::tickPosition() const
{
    const std::array<double, 3>& feet = host.snapshot.player.current;
    return { feet[0], feet[1], feet[2] };
}

mod::Vec3 PlayerService::velocity() const
{
    const std::array<float, 3>& motion = host.snapshot.player.velocity;
    return { motion[0], motion[1], motion[2] };
}

mod::Box PlayerService::boundingBox() const
{
    const PlayerView& view = host.snapshot.player;
    double half = view.bbWidth * 0.5;
    return { { view.current[0] - half, view.current[1], view.current[2] - half }, { view.current[0] + half, view.current[1] + view.bbHeight, view.current[2] + half } };
}

float PlayerService::fallDistance() const
{
    return host.snapshot.player.fallDistance;
}

bool PlayerService::inWater() const
{
    return host.snapshot.player.inWater;
}

bool PlayerService::inLava() const
{
    return host.snapshot.player.inLava;
}

bool PlayerService::gliding() const
{
    return host.snapshot.player.gliding;
}

bool PlayerService::onClimbable() const
{
    return host.snapshot.player.onClimbable;
}

bool PlayerService::collidedHorizontally() const
{
    return host.snapshot.player.horizontalCollision;
}

bool PlayerService::collidedVertically() const
{
    return host.snapshot.player.verticalCollision;
}

mod::Player::Abilities PlayerService::abilities() const
{
    const PlayerView& view = host.snapshot.player;
    return { view.mayFly, view.flying, view.movementSpeed, view.flySpeed };
}

bool PlayerService::clickSlot(int container, int slot, int button)
{
    if (!inWorld() || button < ClickLeft || button > ClickShift) {
        return false;
    }
    const InventoryState& open = host.snapshot.hud.container;
    int local = -1;
    if (container == SlotsInventory && slot >= 0 && slot < InventorySize) {
        local = slot;
    } else if (container == SlotsArmor && slot >= 0 && slot < 4) {
        local = inventory::Armor + slot;
    } else if (container == SlotsOffhand && slot == 0) {
        local = inventory::Offhand;
    } else if (container == SlotsCursor && slot == 0) {
        local = inventory::Cursor;
    } else if (container == SlotsContainer && open.windowId != 0 && slot >= 0 && slot < std::min(open.containerSize, inventory::Ui - inventory::Container)) {
        local = inventory::Container + slot;
    }
    if (local < 0) {
        return false;
    }
    InventoryAction action = button == ClickRight ? InventoryAction::Secondary : button == ClickShift ? InventoryAction::QuickMove : InventoryAction::Primary;
    host.session.requestInventory({ action, local });
    return true;
}

bool PlayerService::moveItem(int fromSlot, int toSlot, int count)
{
    const auto& items = host.snapshot.hud.inventory;
    if (!inWorld() || fromSlot < 0 || fromSlot >= InventorySize || toSlot < 0 || toSlot >= InventorySize || fromSlot == toSlot) {
        return false;
    }
    const HudItem& source = items[static_cast<size_t>(fromSlot)];
    const HudItem& destination = items[static_cast<size_t>(toSlot)];
    if (source.empty() || (!destination.empty() && (destination.identifier != source.identifier || destination.aux != source.aux))) {
        return false;
    }
    InventoryCommand command;
    command.action = InventoryAction::Move;
    command.slot = fromSlot;
    command.value = toSlot;
    command.count = count;
    host.session.requestInventory(std::move(command));
    return true;
}

bool PlayerService::swapHotbar(int slot, int hotbarSlot)
{
    if (!inWorld() || slot < 0 || slot >= InventorySize || hotbarSlot < 0 || hotbarSlot >= HotbarSize || slot == hotbarSlot) {
        return false;
    }
    host.session.requestInventory({ InventoryAction::HotbarSwap, slot, hotbarSlot });
    return true;
}

int PlayerService::findItem(const std::function<bool(const mod::ItemStack&)>& accept) const
{
    if (!accept) {
        return -1;
    }
    const auto& items = host.snapshot.hud.inventory;
    for (int slot = 0; slot < static_cast<int>(items.size()); ++slot) {
        const HudItem& item = items[static_cast<size_t>(slot)];
        if (!item.empty() && accept(itemOf(item))) {
            return slot;
        }
    }
    return -1;
}

/**
 * Rates each hotbar item with the same dig speed the client mines with.
 * Effects and the player's footing slow every item alike, so only the item
 * and its efficiency enchantment decide.
 */
int PlayerService::bestToolFor(const mod::BlockPos& position) const
{
    constexpr int EfficiencyEnchantment = 15;
    const std::shared_ptr<const LoadedBlocks>& area = host.snapshot.loaded;
    if (!joined(host) || !area || !area->loaded(position.x, position.y, position.z) || area->properties(position.x, position.y, position.z).air) {
        return -1;
    }
    std::string name = area->name(position.x, position.y, position.z);
    const auto& items = host.snapshot.hud.inventory;
    auto speedOf = [&](int slot) {
        const HudItem& item = items[static_cast<size_t>(slot)];
        world::MiningConditions conditions;
        conditions.heldItem = item.empty() ? std::string() : item.identifier;
        for (const auto& [id, level] : item.enchantments) {
            if (id == EfficiencyEnchantment) {
                conditions.efficiency = level;
            }
        }
        return world::destroyProgressPerTick(name, conditions);
    };
    int selected = host.snapshot.hud.selectedSlot;
    int best = selected >= 0 && selected < HotbarSize ? selected : 0;
    float bestSpeed = speedOf(best);
    for (int slot = 0; slot < HotbarSize; ++slot) {
        float speed = speedOf(slot);
        if (speed > bestSpeed) {
            best = slot;
            bestSpeed = speed;
        }
    }
    return bestSpeed > 0.0f ? best : -1;
}

/**
 * Clicks the middle of the block's face turned toward the player's eye, the
 * face a player looking at the block from there would hit.
 */
bool PlayerService::openContainer(const mod::BlockPos& position)
{
    const std::shared_ptr<const LoadedBlocks>& area = host.snapshot.loaded;
    if (!inWorld() || !area || !area->loaded(position.x, position.y, position.z) || area->properties(position.x, position.y, position.z).air) {
        return false;
    }
    mod::Vec3 eye = eyePosition();
    std::array<double, 3> toward { eye.x - (position.x + 0.5), eye.y - (position.y + 0.5), eye.z - (position.z + 0.5) };
    size_t axis = 0;
    for (size_t i = 1; i < 3; ++i) {
        if (std::abs(toward[i]) > std::abs(toward[axis])) {
            axis = i;
        }
    }
    static constexpr int32_t Faces[3][2] = { { 4, 5 }, { 0, 1 }, { 2, 3 } };
    bool positive = toward[axis] > 0.0;
    std::array<double, 3> click { 0.5, 0.5, 0.5 };
    click[axis] = positive ? 1.0 : 0.0;
    host.session.requestUseOn({ position.x, position.y, position.z }, Faces[axis][positive ? 1 : 0], click);
    return true;
}

mod::ContainerView PlayerService::openContainerContents() const
{
    mod::ContainerView view;
    const InventoryState& open = host.snapshot.hud.container;
    if (!inWorld() || open.windowId == 0) {
        return view;
    }
    view.open = true;
    view.type = static_cast<int>(open.type);
    view.windowId = open.windowId;
    view.position = { open.blockPosition[0], open.blockPosition[1], open.blockPosition[2] };
    int size = std::clamp(open.containerSize, 0, inventory::Ui - inventory::Container);
    view.slots.reserve(static_cast<size_t>(size));
    for (int slot = 0; slot < size; ++slot) {
        view.slots.push_back({ slot, itemOf(open.slots[static_cast<size_t>(inventory::Container + slot)]) });
    }
    return view;
}

void PlayerService::closeContainer()
{
    if (host.snapshot.hud.container.windowId == 0) {
        return;
    }
    if (host.menu.inventoryOpen()) {
        host.menu.inventoryPanel().close();
        return;
    }
    host.session.requestInventory({ InventoryAction::Close });
}

void PlayerService::startBreaking(const mod::BlockPos& position, int face)
{
    if (face >= 0 && face <= 5) {
        host.session.requestBreaking({ position.x, position.y, position.z }, face);
    }
}

void PlayerService::stopBreaking()
{
    host.session.cancelBreaking();
}

std::optional<mod::BlockPos> PlayerService::breakingTarget() const
{
    const std::optional<std::array<int32_t, 3>>& cell = host.snapshot.breakingCell;
    if (!joined(host) || !cell) {
        return std::nullopt;
    }
    return mod::BlockPos { (*cell)[0], (*cell)[1], (*cell)[2] };
}

float PlayerService::breakingProgress() const
{
    return joined(host) && host.snapshot.breakingCell ? host.snapshot.breakingProgress : 0.0f;
}

void PlayerService::useOn(const mod::BlockPos& position, int face, const mod::Vec3& clickPoint)
{
    host.session.requestUseOn({ position.x, position.y, position.z }, face, { clickPoint.x, clickPoint.y, clickPoint.z });
}

void PlayerService::useItem()
{
    host.session.requestItemUse(true);
}

void PlayerService::releaseUse()
{
    host.session.requestItemUse(false);
}

void PlayerService::attackEntity(uint64_t runtimeId)
{
    host.session.requestAttack(runtimeId);
}

void PlayerService::setAttackHeld(bool held)
{
    host.session.setModAttackHeld(held);
}

void PlayerService::setUseHeld(bool held)
{
    host.session.setModUseHeld(held);
}

/**
 * Steps a copy of the motion state the last movement tick published, with
 * scratch space of its own, against the loaded world the snapshot shares.
 * The session's own state is never touched, and the requests a mod makes for
 * one tick are left out of the repeated input.
 */
std::vector<mod::Vec3> PlayerService::predictPath(int ticks) const
{
    constexpr int MaxPredictedTicks = 200;
    std::vector<mod::Vec3> path;
    const std::shared_ptr<const LoadedBlocks>& area = host.snapshot.loaded;
    if (!inWorld() || !host.snapshot.motion || !area || ticks <= 0) {
        return path;
    }
    PlayerMotion copy = host.snapshot.motion->detached();
    MotionInput input = host.snapshot.motionInput;
    input.riptide = 0;
    input.startGlide = false;
    input.stopGlide = false;
    input.startFlying = false;
    input.stopFlying = false;
    PlayerMotion::CellLookup lookup = [&area](int32_t x, int32_t y, int32_t z) {
        return area->motionCell(x, y, z);
    };
    int count = std::min(ticks, MaxPredictedTicks);
    path.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        MotionTick tick = copy.step(input, lookup);
        float eyeY = tick.position.y + session::EyeHeight;
        copy.anchor({ tick.position.x, eyeY - session::EyeHeight, tick.position.z });
        path.push_back({ tick.position.x, eyeY - session::EyeHeight, tick.position.z });
    }
    return path;
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

std::string WorldService::serverEndpoint() const
{
    return host.snapshot.endpoint;
}

std::vector<mod::BossBar> WorldService::bossBars() const
{
    std::vector<mod::BossBar> bars;
    if (!joined(host)) {
        return bars;
    }
    for (const BossBarView& bar : host.snapshot.hud.bossBars) {
        bars.push_back({ bar.title, bar.progress, bar.color });
    }
    return bars;
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

mod::Subscription NetworkService::addTypedFilter(std::shared_ptr<mod::TypedPacketFilter> filter)
{
    return host.packets->addTyped(owner, std::move(filter));
}

void NetworkService::sendTyped(const mod::PacketView& packet)
{
    if (joined(host)) {
        host.packets->sendTyped(packet);
    }
}

int NetworkService::ping() const
{
    return joined(host) ? host.session.latencyMs() : -1;
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

CommandService::CommandService(HostState& host, size_t owner, long api)
    : host(host)
    , owner(owner)
    , api(api)
{
}

mod::Subscription CommandService::add(mod::CommandSpec spec, Handler handler)
{
    if (spec.name.empty() || spec.name.find(' ') != std::string::npos) {
        throw std::invalid_argument("A command name is one word, got \"" + spec.name + "\"");
    }
    if (api < 4) {
        mod::CommandSpec known;
        known.name = spec.name;
        known.description = spec.description;
        known.usage = spec.usage;
        known.aliases = spec.aliases;
        return host.commands.add(owner, std::move(known), std::move(handler));
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

mod::Subscription SchedulerService::async(Task task, Task onDone)
{
    return host.scheduler.async(owner, std::move(task), std::move(onDone));
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

namespace {

mod::Box boxOf(const world::CollisionBox& box)
{
    return { { box.minX, box.minY, box.minZ }, { box.maxX, box.maxY, box.maxZ } };
}

mod::BlockProps propsOf(const LoadedBlocks::Properties& properties)
{
    mod::BlockProps props;
    props.air = properties.air;
    props.solid = properties.solid;
    props.fullCube = properties.fullCube;
    props.liquid = properties.liquid;
    props.water = properties.water;
    props.lava = properties.lava;
    props.climbable = properties.climbable;
    props.hazard = properties.hazard;
    props.replaceable = properties.replaceable;
    props.gravity = properties.gravity;
    props.liquidLevel = properties.liquidLevel;
    props.hardness = properties.hardness;
    props.friction = properties.friction;
    return props;
}

}

std::vector<mod::Box> WorldService::collision(const mod::BlockPos& position) const
{
    std::vector<mod::Box> boxes;
    const std::shared_ptr<const LoadedBlocks>& area = host.snapshot.loaded;
    if (!joined(host) || !area || !area->loaded(position.x, position.y, position.z)) {
        return boxes;
    }
    for (const world::CollisionBox& box : area->collision(position.x, position.y, position.z)) {
        boxes.push_back(boxOf(box));
    }
    return boxes;
}

std::vector<mod::Box> WorldService::outline(const mod::BlockPos& position) const
{
    const std::shared_ptr<const LoadedBlocks>& area = host.snapshot.loaded;
    if (!joined(host) || !area || !area->loaded(position.x, position.y, position.z)) {
        return {};
    }
    std::optional<world::CollisionBox> box = area->outline(position.x, position.y, position.z);
    if (!box) {
        return {};
    }
    return { boxOf(*box) };
}

std::optional<mod::BlockProps> WorldService::properties(const mod::BlockPos& position) const
{
    const std::shared_ptr<const LoadedBlocks>& area = host.snapshot.loaded;
    if (!joined(host) || !area || !area->loaded(position.x, position.y, position.z)) {
        return std::nullopt;
    }
    return propsOf(area->properties(position.x, position.y, position.z));
}

/**
 * Walks the loaded blocks within radius of the center, asking accept about
 * each block state once, at the first block of that state it meets.
 */
std::vector<mod::FoundBlock> WorldService::findBlocksMatching(const std::function<bool(const mod::BlockProps&)>& accept, const mod::Vec3& center, double radius, size_t limit) const
{
    std::vector<mod::FoundBlock> found;
    const std::shared_ptr<const LoadedBlocks>& area = host.snapshot.loaded;
    if (!joined(host) || !area || !area->assets || !accept || radius <= 0.0 || limit == 0) {
        return found;
    }
    std::unordered_map<uint32_t, bool> verdicts;

    struct Candidate {
        double distance = 0.0;
        mod::BlockPos position;
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
        for (uint32_t y = 0; y < 16; ++y) {
            for (uint32_t z = 0; z < 16; ++z) {
                for (uint32_t x = 0; x < 16; ++x) {
                    mod::BlockPos position { key.x * 16 + int32_t(x), key.y * 16 + int32_t(y), key.z * 16 + int32_t(z) };
                    double dx = position.x + 0.5 - center.x;
                    double dy = position.y + 0.5 - center.y;
                    double dz = position.z + 0.5 - center.z;
                    double distance = dx * dx + dy * dy + dz * dz;
                    if (distance > reach) {
                        continue;
                    }
                    uint32_t value = subChunk->runtimeId(0, x, y, z);
                    auto verdict = verdicts.find(value);
                    if (verdict == verdicts.end()) {
                        verdict = verdicts.emplace(value, accept(propsOf(area->properties(position.x, position.y, position.z)))).first;
                    }
                    if (verdict->second) {
                        candidates.push_back({ distance, position });
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
        found.push_back({ candidates[i].position, area->name(candidates[i].position.x, candidates[i].position.y, candidates[i].position.z) });
    }
    return found;
}

/**
 * Counts the hit that starts the break, then the ticks the dig speed needs
 * to fill the progress, with the conditions the last breaking tick used.
 */
int WorldService::breakTicks(const mod::BlockPos& position) const
{
    constexpr int32_t SurvivalMode = 0;
    constexpr int32_t CreativeMode = 1;
    constexpr double MaxTicks = 1.0e6;
    const std::shared_ptr<const LoadedBlocks>& area = host.snapshot.loaded;
    int32_t gameType = host.snapshot.hud.gameType;
    if (!joined(host) || !area || !area->loaded(position.x, position.y, position.z) || (gameType != SurvivalMode && gameType != CreativeMode)) {
        return -1;
    }
    std::string name = area->name(position.x, position.y, position.z);
    if (name == "minecraft:air") {
        return -1;
    }
    const world::MiningConditions& conditions = host.snapshot.mining;
    if (gameType == CreativeMode) {
        return world::preventsCreativeBreaking(conditions.heldItem) ? -1 : 1;
    }
    float speed = world::destroyProgressPerTick(name, conditions);
    if (speed <= 0.0f) {
        return -1;
    }
    if (speed >= 1.0f) {
        return 1;
    }
    return static_cast<int>(std::min(std::ceil(1.0 / double(speed)), MaxTicks)) + 1;
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

float CameraService::fieldOfView() const
{
    return host.viewFieldOfView;
}

namespace {

/**
 * A mod's value, or nothing when it is the game's own default anyway, so a
 * mod setting things back to normal stops overriding other mods.
 */
std::optional<float> unlessDefault(float value, float fallback, float low, float high)
{
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    value = std::clamp(value, low, high);
    return value == fallback ? std::nullopt : std::optional<float>(value);
}

bool emptyRequest(const VisualRequest& request)
{
    return !request.time && !request.rain && !request.thunder && !request.fogScale && !request.brightness && !request.hurtCamera
        && !request.hitColor && !request.glintStrength && !request.glintSpeed && !request.itemPhysics && !request.swingDuration
        && !request.heldOffset && !request.heldScale && !request.nametagScale && !request.ownNametag && !request.interfaceScale;
}

}

VisualsService::VisualsService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

template <class Change>
void VisualsService::change(Change&& apply)
{
    VisualRequest& request = host.visuals[owner];
    apply(request);
    if (emptyRequest(request)) {
        host.visuals.erase(owner);
    }
}

void VisualsService::setTime(std::optional<int64_t> ticks)
{
    change([&](VisualRequest& request) { request.time = ticks; });
}

void VisualsService::setWeather(std::optional<float> rain, std::optional<float> thunder)
{
    auto level = [](std::optional<float> value) -> std::optional<float> {
        if (!value || !std::isfinite(*value)) {
            return std::nullopt;
        }
        return std::clamp(*value, 0.0f, 1.0f);
    };
    change([&](VisualRequest& request) {
        request.rain = level(rain);
        request.thunder = level(thunder);
    });
}

void VisualsService::setFogScale(float scale)
{
    change([&](VisualRequest& request) { request.fogScale = unlessDefault(scale, 1.0f, 0.1f, 1000.0f); });
}

void VisualsService::setBrightness(float amount)
{
    change([&](VisualRequest& request) { request.brightness = unlessDefault(amount, 0.0f, 0.0f, 1.0f); });
}

void VisualsService::setHurtCamera(float scale)
{
    change([&](VisualRequest& request) { request.hurtCamera = unlessDefault(scale, 1.0f, 0.0f, 1.0f); });
}

void VisualsService::setHitColor(std::optional<mod::Color> color)
{
    change([&](VisualRequest& request) { request.hitColor = color; });
}

void VisualsService::setGlint(std::optional<int> strength, std::optional<int> speed)
{
    change([&](VisualRequest& request) {
        request.glintStrength = strength ? std::optional<int>(std::clamp(*strength, 0, 100)) : std::nullopt;
        request.glintSpeed = speed ? std::optional<int>(std::clamp(*speed, 0, 400)) : std::nullopt;
    });
}

void VisualsService::setItemPhysics(bool enabled)
{
    change([&](VisualRequest& request) { request.itemPhysics = enabled ? std::optional<bool>(true) : std::nullopt; });
}

void VisualsService::setSwingDuration(float scale)
{
    change([&](VisualRequest& request) { request.swingDuration = unlessDefault(scale, 1.0f, 0.25f, 4.0f); });
}

void VisualsService::setHeldItem(mod::Vec3 offset, float scale)
{
    bool moved = std::isfinite(offset.x) && std::isfinite(offset.y) && std::isfinite(offset.z) && !(offset == mod::Vec3 {});
    change([&](VisualRequest& request) {
        request.heldOffset = moved ? std::optional<mod::Vec3>(mod::Vec3 { std::clamp(offset.x, -2.0, 2.0), std::clamp(offset.y, -2.0, 2.0), std::clamp(offset.z, -2.0, 2.0) }) : std::nullopt;
        request.heldScale = unlessDefault(scale, 1.0f, 0.1f, 3.0f);
    });
}

void VisualsService::setNametags(float scale, bool showOwn)
{
    change([&](VisualRequest& request) {
        request.nametagScale = unlessDefault(scale, 1.0f, 0.25f, 4.0f);
        request.ownNametag = showOwn ? std::optional<bool>(true) : std::nullopt;
    });
}

void VisualsService::setInterfaceScale(std::optional<float> scale)
{
    change([&](VisualRequest& request) {
        request.interfaceScale = scale && std::isfinite(*scale) ? std::optional<float>(std::clamp(*scale, 0.25f, 4.0f)) : std::nullopt;
    });
}

}

namespace kestrel::modding {

HudService::HudService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

void HudService::setHidden(mod::HudElement element, bool hidden)
{
    uint32_t bit = 1u << static_cast<uint32_t>(element);
    uint32_t& mask = host.hiddenHud[owner];
    mask = hidden ? mask | bit : mask & ~bit;
    if (mask == 0) {
        host.hiddenHud.erase(owner);
    }
}

bool HudService::hidden(mod::HudElement element) const
{
    auto found = host.hiddenHud.find(owner);
    return found != host.hiddenHud.end() && ((found->second >> static_cast<uint32_t>(element)) & 1u) != 0;
}

std::optional<std::pair<float, float>> HudService::project(const mod::Vec3& world) const
{
    if (!host.worldViewProjection || host.interfaceWidth <= 0.0f || host.interfaceHeight <= 0.0f) {
        return std::nullopt;
    }
    const std::array<float, 16>& matrix = *host.worldViewProjection;
    const double relative[3] = { world.x - host.worldCamera.x, world.y - host.worldCamera.y, world.z - host.worldCamera.z };
    double clip[4] {};
    for (size_t row = 0; row < 4; ++row) {
        clip[row] = matrix[12 + row];
        for (size_t column = 0; column < 3; ++column) {
            clip[row] += matrix[column * 4 + row] * relative[column];
        }
    }
    if (!(clip[3] > 1e-6) || !std::isfinite(clip[0]) || !std::isfinite(clip[1])) {
        return std::nullopt;
    }
    double x = (clip[0] / clip[3] + 1.0) * 0.5 * host.interfaceWidth;
    double y = (1.0 - clip[1] / clip[3]) * 0.5 * host.interfaceHeight;
    return std::pair<float, float> { static_cast<float>(x), static_cast<float>(y) };
}

/**
 * Keeps the text to a few hundred bytes, cut before a whole UTF-8 sequence,
 * and only the newest notifications, so a mod in a loop cannot fill the
 * screen.
 */
void HudService::notify(std::string_view text, double seconds)
{
    constexpr size_t MaxNoticeBytes = 256;
    constexpr size_t MaxNotices = 6;
    constexpr double MaxNoticeSeconds = 60.0;
    if (text.empty() || !std::isfinite(seconds) || seconds <= 0.0) {
        return;
    }
    size_t end = std::min(text.size(), MaxNoticeBytes);
    if (end < text.size()) {
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
            --end;
        }
    }
    host.notices.push_back({ owner, std::string(text.substr(0, end)), host.seconds + std::min(seconds, MaxNoticeSeconds) });
    if (host.notices.size() > MaxNotices) {
        host.notices.erase(host.notices.begin());
    }
}

EmoteService::EmoteService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

mod::Subscription EmoteService::add(mod::EmoteSpec spec)
{
    return host.emotes.add(owner, std::move(spec), host.errors);
}

void EmoteService::play(std::string_view id)
{
    host.emotes.requestPlay(std::string(id));
}

void EmoteService::stop()
{
    host.emotes.requestStop();
}

std::optional<std::string> EmoteService::playing() const
{
    return host.emotes.playing;
}

ParticleService::ParticleService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

bool ParticleService::request(EffectRequest::Action action, mod::ParticleHandle handle, mod::Vec3 position) const
{
    EffectRequest request;
    request.kind = EffectRequest::Kind::Particle;
    request.action = action;
    request.handle = handle;
    request.position = position;
    host.effects.process(owner, request);
    return request.result;
}

bool ParticleService::supported() const
{
    return request(EffectRequest::Action::Supported);
}

mod::ParticleHandle ParticleService::spawn(mod::ParticleOptions options)
{
    EffectRequest request;
    request.kind = EffectRequest::Kind::Particle;
    request.action = EffectRequest::Action::Create;
    request.particle = std::move(options);
    host.effects.process(owner, request);
    return request.result ? request.handle : 0;
}

bool ParticleService::active(mod::ParticleHandle handle) const
{
    return request(EffectRequest::Action::Active, handle);
}

bool ParticleService::move(mod::ParticleHandle handle, mod::Vec3 position)
{
    return request(EffectRequest::Action::Move, handle, position);
}

bool ParticleService::remove(mod::ParticleHandle handle)
{
    return request(EffectRequest::Action::Remove, handle);
}

void ParticleService::clear()
{
    request(EffectRequest::Action::Clear);
}

AudioService::AudioService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

bool AudioService::request(EffectRequest::Action action, mod::SoundHandle handle, mod::Vec3 position, float volume) const
{
    EffectRequest request;
    request.kind = EffectRequest::Kind::Sound;
    request.action = action;
    request.handle = handle;
    request.position = position;
    request.volume = volume;
    host.effects.process(owner, request);
    return request.result;
}

bool AudioService::supported() const
{
    return request(EffectRequest::Action::Supported);
}

mod::SoundHandle AudioService::play(mod::SoundOptions options)
{
    EffectRequest request;
    request.kind = EffectRequest::Kind::Sound;
    request.action = EffectRequest::Action::Create;
    request.sound = std::move(options);
    host.effects.process(owner, request);
    return request.result ? request.handle : 0;
}

bool AudioService::playing(mod::SoundHandle handle) const
{
    return request(EffectRequest::Action::Active, handle);
}

bool AudioService::stop(mod::SoundHandle handle)
{
    return request(EffectRequest::Action::Remove, handle);
}

bool AudioService::setVolume(mod::SoundHandle handle, float volume)
{
    return request(EffectRequest::Action::Volume, handle, {}, volume);
}

bool AudioService::setPosition(mod::SoundHandle handle, mod::Vec3 position)
{
    return request(EffectRequest::Action::Move, handle, position);
}

void AudioService::stopAll()
{
    request(EffectRequest::Action::Clear);
}

UiService::UiService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

bool UiService::request(UiRequest::Action action, std::string_view id) const
{
    UiRequest request;
    request.action = action;
    request.id = id;
    host.ui.process(owner, request);
    return request.result;
}

bool UiService::supported() const
{
    return request(UiRequest::Action::Supported, {});
}

bool UiService::open(std::string_view id)
{
    return request(UiRequest::Action::Open, id);
}

bool UiService::close(std::string_view id)
{
    return request(UiRequest::Action::Close, id);
}

bool UiService::isOpen(std::string_view id) const
{
    return request(UiRequest::Action::IsOpen, id);
}

void UiService::addSettings(std::vector<mod::SettingSpec> settings)
{
    constexpr size_t MaxSettings = 128;
    constexpr size_t MaxKeyBytes = 128;
    if (settings.size() > MaxSettings) {
        throw std::invalid_argument("A settings page holds at most " + std::to_string(MaxSettings) + " settings");
    }
    std::set<std::string, std::less<>> keys;
    for (const mod::SettingSpec& setting : settings) {
        if (setting.key.empty() || setting.key.size() > MaxKeyBytes) {
            throw std::invalid_argument("A setting needs a key of 1 to " + std::to_string(MaxKeyBytes) + " bytes");
        }
        if (!keys.insert(setting.key).second) {
            throw std::invalid_argument("The setting key \"" + setting.key + "\" is used twice");
        }
        bool bounded = std::isfinite(setting.minimum) && std::isfinite(setting.maximum) && setting.maximum > setting.minimum
            && std::isfinite(setting.step) && setting.step >= 0.0;
        if (setting.kind == mod::SettingSpec::Kind::Range && !bounded) {
            throw std::invalid_argument("The range \"" + setting.key + "\" needs finite bounds, minimum below maximum, and a step of 0 or more");
        }
        if (setting.kind == mod::SettingSpec::Kind::Choice && setting.choices.empty()) {
            throw std::invalid_argument("The choice \"" + setting.key + "\" has no choices");
        }
    }
    if (settings.empty()) {
        host.settings.erase(owner);
        return;
    }
    host.settings[owner] = std::move(settings);
}

TextureService::TextureService(HostState& host, size_t owner)
    : host(host)
    , owner(owner)
{
}

bool TextureService::request(TextureRequest::Action action, mod::TextureHandle handle) const
{
    TextureRequest request;
    request.action = action;
    request.handle = handle;
    host.textures.process(owner, request);
    return request.result;
}

bool TextureService::write(TextureRequest::Action action, mod::TextureHandle handle, mod::Image image, uint32_t x, uint32_t y)
{
    TextureRequest request;
    request.action = action;
    request.handle = handle;
    request.image = std::move(image);
    request.x = x;
    request.y = y;
    host.textures.process(owner, request);
    return request.result;
}

bool TextureService::supported() const
{
    return request(TextureRequest::Action::Supported);
}

mod::TextureHandle TextureService::load(const std::filesystem::path& path)
{
    TextureRequest request;
    request.action = TextureRequest::Action::Load;
    request.path = path;
    host.textures.process(owner, request);
    return request.result ? request.handle : 0;
}

mod::TextureHandle TextureService::decode(std::span<const uint8_t> encoded)
{
    TextureRequest request;
    request.action = TextureRequest::Action::Decode;
    request.encoded = encoded;
    host.textures.process(owner, request);
    return request.result ? request.handle : 0;
}

mod::TextureHandle TextureService::create(mod::Image image)
{
    TextureRequest request;
    request.action = TextureRequest::Action::Create;
    request.image = std::move(image);
    host.textures.process(owner, request);
    return request.result ? request.handle : 0;
}

mod::TextureInfo TextureService::info(mod::TextureHandle handle) const
{
    TextureRequest request;
    request.action = TextureRequest::Action::Info;
    request.handle = handle;
    host.textures.process(owner, request);
    return { request.image.width, request.image.height, request.result };
}

mod::Image TextureService::read(mod::TextureHandle handle) const
{
    TextureRequest request;
    request.action = TextureRequest::Action::Read;
    request.handle = handle;
    host.textures.process(owner, request);
    return request.result ? std::move(request.image) : mod::Image {};
}

bool TextureService::update(mod::TextureHandle handle, mod::Image image)
{
    return write(TextureRequest::Action::Update, handle, std::move(image), 0, 0);
}

bool TextureService::updateRegion(mod::TextureHandle handle, uint32_t x, uint32_t y, mod::Image image)
{
    return write(TextureRequest::Action::Patch, handle, std::move(image), x, y);
}

bool TextureService::draw(mod::Canvas& canvas, mod::TextureHandle handle, mod::Rect rect, mod::Color tint)
{
    TextureRequest request;
    request.action = TextureRequest::Action::Draw;
    request.handle = handle;
    request.canvas = &canvas;
    request.rect = rect;
    request.tint = tint;
    host.textures.process(owner, request);
    return request.result;
}

bool TextureService::destroy(mod::TextureHandle handle)
{
    return request(TextureRequest::Action::Destroy, handle);
}

void TextureService::clear()
{
    request(TextureRequest::Action::Clear);
}

}
