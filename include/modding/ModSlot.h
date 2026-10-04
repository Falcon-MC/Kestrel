#pragma once

#include "modding/ModConfig.h"
#include "modding/ModServices.h"

#include "mod/Mod.h"
#include "platform/Library.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace kestrel::modding {

/**
 * One loaded mod: its library, the instance it made and the context it is
 * given. Tearing it down undoes everything the mod registered before the
 * library goes away, since the handlers live in its code.
 */
class ModSlot final : public mod::ModContext {
public:
    static std::unique_ptr<ModSlot> load(const std::filesystem::path& file, HostState& host, size_t owner, std::string& error);

    ~ModSlot() override;

    const mod::ModInfo& info() const
    {
        return instance->info();
    }

    size_t owner() const
    {
        return id;
    }

    ModConfig& configStore()
    {
        return settings;
    }

    /**
     * Runs onEnable; false when it threw, with everything it registered
     * already taken back.
     */
    bool enable();

    mod::EventBus& events() override;
    mod::Chat& chat() override;
    mod::Player& player() override;
    mod::World& world() override;
    mod::Network& network() override;
    mod::Input& input() override;
    mod::Commands& commands() override;
    mod::Config& config() override;
    mod::Logger& log() override;
    mod::Scheduler& scheduler() override;
    mod::Shaders& shaders() override;
    mod::Camera& camera() override;
    std::filesystem::path dataDirectory() override;
    std::vector<mod::ModInfo> loadedMods() const override;

private:
    using Destroy = void (*)(mod::Mod*);

    ModSlot(platform::Library library, HostState& host, size_t owner, mod::Mod* instance, Destroy destroy);

    void releaseAll();

    platform::Library library;
    HostState& host;
    size_t id;
    mod::Mod* instance;
    Destroy destroy;
    bool enabled = false;
    EventBusService eventService;
    ChatService chatService;
    PlayerService playerService;
    WorldService worldService;
    NetworkService networkService;
    CommandService commandService;
    LoggerService loggerService;
    SchedulerService schedulerService;
    ShaderService shaderService;
    ModConfig settings;
    InputService inputService;
    CameraService cameraService;
};

/**
 * Mod ids name folders, so they stay to lowercase letters, digits, dots,
 * dashes and underscores.
 */
bool validModId(std::string_view id);

}
