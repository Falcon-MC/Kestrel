#pragma once

#include "mod/Camera.h"
#include "mod/Chat.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/Emotes.h"
#include "mod/Effects.h"
#include "mod/Ui.h"
#include "mod/Textures.h"
#include "mod/Event.h"
#include "mod/Hud.h"
#include "mod/Input.h"
#include "mod/Logger.h"
#include "mod/Network.h"
#include "mod/Player.h"
#include "mod/Scheduler.h"
#include "mod/Shaders.h"
#include "mod/Visuals.h"
#include "mod/World.h"

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace kestrel::mod {

/**
 * Everything the client offers one mod. Each mod gets its own context, which
 * is how the client knows what to clean up when the mod goes away. Only use
 * it from the main thread, except Scheduler::post and packet filters.
 */
class ModContext {
public:
    virtual ~ModContext() = default;

    virtual EventBus& events() = 0;
    virtual Chat& chat() = 0;
    virtual Player& player() = 0;
    virtual World& world() = 0;
    virtual Network& network() = 0;
    virtual Input& input() = 0;
    virtual Commands& commands() = 0;
    virtual Config& config() = 0;
    virtual Logger& log() = 0;
    virtual Scheduler& scheduler() = 0;
    virtual Shaders& shaders() = 0;

    /**
     * mods/<id>/, created on first use, for any files the mod keeps.
     */
    virtual std::filesystem::path dataDirectory() = 0;

    /**
     * Every loaded mod, this one included.
     */
    virtual std::vector<ModInfo> loadedMods() const = 0;

    // Added in API 3 and kept last so older mods still find everything above.
    virtual Camera& camera() = 0;
    virtual Emotes& emotes() = 0;
    virtual Hud& hud() = 0;
    virtual Visuals& visuals() = 0;

    // Added in API 4 and kept last so older mods still find everything above.
    virtual Particles& particles() = 0;
    virtual Audio& audio() = 0;
    virtual Ui& ui() = 0;
    virtual Textures& textures() = 0;

    /**
     * Shares service with other mods under id until this mod unloads, or
     * until it provides null under the same id. An id another mod provides
     * cannot be taken (std::invalid_argument). A mod that keeps a service
     * should name its provider in Mod::dependencies, so it is unloaded first
     * and lets go of the object before the provider's code goes away.
     */
    virtual void provideService(std::string_view id, std::shared_ptr<void> service) = 0;

    /**
     * The service some mod provides under id, null when none does.
     */
    virtual std::shared_ptr<void> findService(std::string_view id) const = 0;

    template <class T>
    void provide(std::string_view id, std::shared_ptr<T> service)
    {
        provideService(id, std::move(service));
    }

    /**
     * The service under id as a T, null when none is provided. Nothing checks
     * the type, both mods must agree on T.
     */
    template <class T>
    std::shared_ptr<T> service(std::string_view id) const
    {
        return std::static_pointer_cast<T>(findService(id));
    }
};

}
