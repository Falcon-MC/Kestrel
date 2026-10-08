#pragma once

#include "modding/HostState.h"

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
    int32_t maxDurability(const std::string& identifier) const override;
    mod::Vec3 tickPosition() const override;
    mod::Vec3 velocity() const override;
    mod::Box boundingBox() const override;
    float fallDistance() const override;
    bool inWater() const override;
    bool inLava() const override;
    bool gliding() const override;
    bool onClimbable() const override;
    bool collidedHorizontally() const override;
    bool collidedVertically() const override;
    Abilities abilities() const override;
    bool clickSlot(int container, int slot, int button) override;
    bool moveItem(int fromSlot, int toSlot, int count) override;
    bool swapHotbar(int slot, int hotbarSlot) override;
    int findItem(const std::function<bool(const mod::ItemStack&)>& accept) const override;
    int bestToolFor(const mod::BlockPos& position) const override;
    bool openContainer(const mod::BlockPos& position) override;
    mod::ContainerView openContainerContents() const override;
    void closeContainer() override;
    void startBreaking(const mod::BlockPos& position, int face) override;
    void stopBreaking() override;
    std::optional<mod::BlockPos> breakingTarget() const override;
    float breakingProgress() const override;
    void useOn(const mod::BlockPos& position, int face, const mod::Vec3& clickPoint) override;
    void useItem() override;
    void releaseUse() override;
    void attackEntity(uint64_t runtimeId) override;
    void setAttackHeld(bool held) override;
    void setUseHeld(bool held) override;
    std::vector<mod::Vec3> predictPath(int ticks) const override;

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
    std::vector<mod::BossBar> bossBars() const override;
    std::string serverEndpoint() const override;
    std::vector<mod::Box> collision(const mod::BlockPos& position) const override;
    std::vector<mod::Box> outline(const mod::BlockPos& position) const override;
    std::optional<mod::BlockProps> properties(const mod::BlockPos& position) const override;
    std::vector<mod::FoundBlock> findBlocksMatching(const std::function<bool(const mod::BlockProps&)>& accept, const mod::Vec3& center, double radius, size_t limit) const override;
    int breakTicks(const mod::BlockPos& position) const override;

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
    float fieldOfView() const override;

private:
    HostState& host;
    size_t owner;
};

class VisualsService final : public mod::Visuals {
public:
    VisualsService(HostState& host, size_t owner);

    void setTime(std::optional<int64_t> ticks) override;
    void setWeather(std::optional<float> rain, std::optional<float> thunder) override;
    void setFogScale(float scale) override;
    void setBrightness(float amount) override;
    void setHurtCamera(float scale) override;
    void setHitColor(std::optional<mod::Color> color) override;
    void setGlint(std::optional<int> strength, std::optional<int> speed) override;
    void setItemPhysics(bool enabled) override;
    void setSwingDuration(float scale) override;
    void setHeldItem(mod::Vec3 offset, float scale) override;
    void setNametags(float scale, bool showOwn) override;
    void setInterfaceScale(std::optional<float> scale) override;

private:
    // Applies a change, then forgets the mod once it asks for nothing at all.
    template <class Change>
    void change(Change&& apply);

    HostState& host;
    size_t owner;
};

class HudService final : public mod::Hud {
public:
    HudService(HostState& host, size_t owner);

    void setHidden(mod::HudElement element, bool hidden) override;
    bool hidden(mod::HudElement element) const override;
    std::optional<std::pair<float, float>> project(const mod::Vec3& world) const override;
    void notify(std::string_view text, double seconds) override;

private:
    HostState& host;
    size_t owner;
};

class EmoteService final : public mod::Emotes {
public:
    EmoteService(HostState& host, size_t owner);

    mod::Subscription add(mod::EmoteSpec spec) override;
    void play(std::string_view id) override;
    void stop() override;
    std::optional<std::string> playing() const override;

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
    mod::Subscription addTypedFilter(std::shared_ptr<mod::TypedPacketFilter> filter) override;
    void sendTyped(const mod::PacketView& packet) override;
    int ping() const override;

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
    /**
     * api is the API version the mod was built for: before 4 its
     * CommandSpec ends after aliases, so complete is never read.
     */
    CommandService(HostState& host, size_t owner, long api);

    using mod::Commands::add;
    mod::Subscription add(mod::CommandSpec spec, Handler handler) override;

private:
    HostState& host;
    size_t owner;
    long api;
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
    mod::Subscription async(Task task, Task onDone) override;

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

class ParticleService final : public mod::Particles {
public:
    ParticleService(HostState& host, size_t owner);

    bool supported() const override;
    mod::ParticleHandle spawn(mod::ParticleOptions options) override;
    bool active(mod::ParticleHandle handle) const override;
    bool move(mod::ParticleHandle handle, mod::Vec3 position) override;
    bool remove(mod::ParticleHandle handle) override;
    void clear() override;

private:
    bool request(EffectRequest::Action action, mod::ParticleHandle handle = 0, mod::Vec3 position = {}) const;

    HostState& host;
    size_t owner;
};

class AudioService final : public mod::Audio {
public:
    AudioService(HostState& host, size_t owner);

    bool supported() const override;
    mod::SoundHandle play(mod::SoundOptions options) override;
    bool playing(mod::SoundHandle handle) const override;
    bool stop(mod::SoundHandle handle) override;
    bool setVolume(mod::SoundHandle handle, float volume) override;
    bool setPosition(mod::SoundHandle handle, mod::Vec3 position) override;
    void stopAll() override;

private:
    bool request(EffectRequest::Action action, mod::SoundHandle handle = 0, mod::Vec3 position = {}, float volume = 1.0f) const;

    HostState& host;
    size_t owner;
};

class UiService final : public mod::Ui {
public:
    UiService(HostState& host, size_t owner);

    bool supported() const override;
    bool open(std::string_view id) override;
    bool close(std::string_view id) override;
    bool isOpen(std::string_view id) const override;
    void addSettings(std::vector<mod::SettingSpec> settings) override;

private:
    bool request(UiRequest::Action action, std::string_view id) const;

    HostState& host;
    size_t owner;
};

class TextureService final : public mod::Textures {
public:
    TextureService(HostState& host, size_t owner);

    bool supported() const override;
    mod::TextureHandle load(const std::filesystem::path& path) override;
    mod::TextureHandle decode(std::span<const uint8_t> encoded) override;
    mod::TextureHandle create(mod::Image image) override;
    mod::TextureInfo info(mod::TextureHandle handle) const override;
    mod::Image read(mod::TextureHandle handle) const override;
    bool update(mod::TextureHandle handle, mod::Image image) override;
    bool updateRegion(mod::TextureHandle handle, uint32_t x, uint32_t y, mod::Image image) override;
    bool draw(mod::Canvas& canvas, mod::TextureHandle handle, mod::Rect rect, mod::Color tint) override;
    bool destroy(mod::TextureHandle handle) override;
    void clear() override;

private:
    bool request(TextureRequest::Action action, mod::TextureHandle handle = 0) const;
    bool write(TextureRequest::Action action, mod::TextureHandle handle, mod::Image image, uint32_t x, uint32_t y);

    HostState& host;
    size_t owner;
};

mod::ItemStack itemOf(const HudItem& item);

}
