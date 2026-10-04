#pragma once

#include "mod/Camera.h"
#include "mod/Chat.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/Emotes.h"
#include "mod/Event.h"
#include "mod/Input.h"
#include "mod/Logger.h"
#include "mod/Network.h"
#include "mod/Player.h"
#include "mod/Scheduler.h"
#include "mod/Shaders.h"
#include "mod/World.h"

#include <filesystem>
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
};

}
