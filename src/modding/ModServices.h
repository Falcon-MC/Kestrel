#pragma once

#include "HostState.h"

#include "mod/Context.h"

namespace kestrel::modding {

/**
 * The services one mod sees. Each carries the mod's owner number, so
 * whatever it registers can be taken away when the mod unloads.
 */
class EventBusService final : public mod::EventBus {
public:
    EventBusService(HostState& host, size_t owner);

    mod::Subscription subscribe(std::string_view type, Handler handler, mod::ListenOptions options) override;
    void post(mod::Event& event) override;

private:
    HostState& host;
    size_t owner;
};

class ChatService final : public mod::Chat {
public:
    explicit ChatService(HostState& host);

    void send(std::string_view text) override;
    void print(std::string_view text) override;
    void toast(std::string_view title, std::string_view content) override;
    void title(std::string_view title, std::string_view subtitle) override;
    void actionbar(std::string_view text) override;
    std::vector<std::string> history() const override;

private:
    HostState& host;
};

class PlayerService final : public mod::Player {
public:
    explicit PlayerService(HostState& host);

    bool inWorld() const override;
    std::string name() const override;
    uint64_t runtimeId() const override;
    mod::Vec3 position() const override;
    mod::Vec3 eyePosition() const override;
    mod::Rotation rotation() const override;
    void setRotation(mod::Rotation rotation) override;
    float health() const override;
    float maxHealth() const override;
    float absorption() const override;
    float hunger() const override;
    float saturation() const override;
    int level() const override;
    float experience() const override;
    int air() const override;
    std::string gameMode() const override;
    int dimension() const override;
    bool dead() const override;
    bool onGround() const override;
    bool sneaking() const override;
    bool sprinting() const override;
    bool swimming() const override;
    bool flying() const override;
    int selectedSlot() const override;
    void selectSlot(int slot) override;
    mod::ItemStack inventory(int slot) const override;
    mod::ItemStack armor(int slot) const override;
    mod::ItemStack offhand() const override;
    std::vector<mod::StatusEffect> effects() const override;
    void attack() override;
    void use() override;
    void pickBlock(bool withData) override;
    void dropHeld(bool wholeStack) override;
    void respawn() override;

private:
    HostState& host;
};

class WorldService final : public mod::World {
public:
    WorldService(HostState& host, size_t owner);

    mod::ConnectionState state() const override;
    std::string serverName() const override;
    std::string serverAddress() const override;
    std::string levelName() const override;
    int dimension() const override;
    int64_t time() const override;
    float rain() const override;
    float thunder() const override;
    std::vector<mod::Entity> entities() const override;
    std::optional<mod::Entity> entity(uint64_t runtimeId) const override;
    std::optional<mod::TargetBlock> targetBlock() const override;
    std::vector<std::string> players() const override;
    mod::Sidebar sidebar() const override;
    mod::Environment environment() const override;
    bool isLoaded(const mod::BlockPos& position) const override;
    std::optional<mod::BlockInfo> block(const mod::BlockPos& position) const override;
    std::optional<mod::RaycastHit> raycast(const mod::Vec3& from, const mod::Vec3& direction, double reach, bool entities) const override;
    void setBlockHidden(std::string_view name, bool hidden) override;
    void clearHiddenBlocks() override;
    std::vector<std::string> blockNames() const override;
    void setVisibleBlocks(const std::vector<std::string>& names) override;
    std::vector<mod::FoundBlock> findBlocks(const std::vector<std::string>& names, const mod::Vec3& center, double radius, size_t limit) const override;

private:
    HostState& host;
    size_t owner;
};

class CameraService final : public mod::Camera {
public:
    CameraService(HostState& host, size_t owner);

    mod::Vec3 position() const override;
    mod::Rotation rotation() const override;
    void detach(const mod::Vec3& position, mod::Rotation rotation) override;
    void attach() override;
    bool detached() const override;
    void setFovScale(float scale) override;

private:
    HostState& host;
    size_t owner;
};

class NetworkService final : public mod::Network {
public:
    NetworkService(HostState& host, size_t owner);

    void connect(std::string_view address, std::string_view name) override;
    void disconnect() override;
    void sendRaw(std::string payload) override;
    void answerForm(uint32_t id, std::optional<std::string> json) override;
    mod::Subscription addFilter(std::shared_ptr<mod::PacketFilter> filter) override;
    std::string packetName(int id) const override;
    std::string_view gameVersion() const override;

private:
    HostState& host;
    size_t owner;
};

class InputService final : public mod::Input {
public:
    InputService(HostState& host, size_t owner, mod::Config& config);

    bool isHeld(mod::Key key) const override;
    bool inGame() const override;
    float mouseX() const override;
    float mouseY() const override;
    void setCursorFree(bool free) override;
    bool cursorFree() const override;
    mod::Subscription bind(mod::Key key, std::function<void()> action) override;
    mod::Subscription bind(mod::KeyBindSpec spec, std::function<void()> action) override;

private:
    HostState& host;
    size_t owner;
    mod::Config& config;
};

class CommandService final : public mod::Commands {
public:
    CommandService(HostState& host, size_t owner);

    using mod::Commands::add;
    mod::Subscription add(mod::CommandSpec spec, Handler handler) override;

private:
    HostState& host;
    size_t owner;
};

class LoggerService final : public mod::Logger {
public:
    explicit LoggerService(std::string id);

    void log(Level level, std::string_view message) override;

private:
    std::string id;
};

class SchedulerService final : public mod::Scheduler {
public:
    SchedulerService(HostState& host, size_t owner);

    mod::Subscription after(double seconds, Task task) override;
    mod::Subscription every(double seconds, Task task) override;
    void post(Task task) override;

private:
    HostState& host;
    size_t owner;
};

class ShaderService final : public mod::Shaders {
public:
    ShaderService(HostState& host, size_t owner);

    std::shared_ptr<mod::Shader> create(const mod::ShaderSource& source) override;
    std::shared_ptr<mod::Shader> createPost(const mod::ShaderSource& source) override;
    std::string_view backend() const override;

private:
    HostState& host;
    size_t owner;
};

mod::ItemStack itemOf(const HudItem& item);

}
