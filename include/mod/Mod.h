#pragma once

#include "mod/Context.h"
#include "mod/Events.h"
#include "mod/Effects.h"
#include "mod/Ui.h"
#include "mod/Textures.h"

#include <cassert>
#include <functional>
#include <type_traits>
#include <utility>

namespace kestrel::modding {
class ModSlot;
}

namespace kestrel::mod {

/**
 * Base class of a mod. Override onEnable to register what the mod does;
 * everything registered through the helpers below is undone when the mod
 * unloads, so onDisable is only needed for the mod's own resources.
 *
 * The context is not there yet in the constructor, so keep that for plain
 * member setup.
 */
class Mod {
public:
    explicit Mod(ModInfo info)
        : details(std::move(info))
    {
    }

    virtual ~Mod() = default;

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

    virtual void onEnable() { }
    virtual void onDisable() { }

    // Added in API 4 and kept last so older mods still find everything above.
    /**
     * Ids of the mods this one needs. They start before it, it is refused
     * when one is missing, and it is unloaded whenever one of them is. It is
     * asked once, right after the constructor, before the context exists.
     */
    virtual std::vector<std::string> dependencies() const
    {
        return {};
    }

    const ModInfo& info() const
    {
        return details;
    }

protected:
    ModContext& context() const
    {
        assert(host && "the mod context is only there from onEnable on");
        return *host;
    }

    EventBus& events() const { return context().events(); }
    Chat& chat() const { return context().chat(); }
    Player& player() const { return context().player(); }
    World& world() const { return context().world(); }
    Network& network() const { return context().network(); }
    Input& input() const { return context().input(); }
    Commands& commands() const { return context().commands(); }
    Config& config() const { return context().config(); }
    Logger& log() const { return context().log(); }
    Scheduler& scheduler() const { return context().scheduler(); }
    Shaders& shaders() const { return context().shaders(); }
    Camera& camera() const { return context().camera(); }
    Emotes& emotes() const { return context().emotes(); }
    Hud& hud() const { return context().hud(); }
    Visuals& visuals() const { return context().visuals(); }
    Particles& particles() const { return context().particles(); }
    Audio& audio() const { return context().audio(); }
    Ui& ui() const { return context().ui(); }
    Textures& textures() const { return context().textures(); }

    Subscription emote(EmoteSpec spec)
    {
        return emotes().add(std::move(spec));
    }

    /**
     * Listens with a lambda: on<ChatReceivedEvent>([](ChatReceivedEvent& event) { ... }).
     */
    template <class E, class F>
        requires std::is_invocable_v<F&, E&>
    Subscription on(F&& handler, ListenOptions options = {})
    {
        return events().template on<E>(std::forward<F>(handler), options);
    }

    /**
     * Listens with a member function, the event type comes from its
     * parameter: on(&MyMod::chatReceived).
     */
    template <class C, class E>
    Subscription on(void (C::*method)(E&), ListenOptions options = {})
    {
        static_assert(std::is_base_of_v<Mod, C>, "the method must belong to this mod");
        C* self = static_cast<C*>(this);
        return events().template on<E>([self, method](E& event) { (self->*method)(event); }, options);
    }

    Subscription command(std::string name, std::string description, Commands::Handler handler)
    {
        return commands().add(std::move(name), std::move(description), std::move(handler));
    }

    Subscription command(CommandSpec spec, Commands::Handler handler)
    {
        return commands().add(std::move(spec), std::move(handler));
    }

    Subscription bind(Key key, std::function<void()> action)
    {
        return input().bind(key, std::move(action));
    }

    Subscription bind(KeyBindSpec spec, std::function<void()> action)
    {
        return input().bind(std::move(spec), std::move(action));
    }

    Subscription every(double seconds, Scheduler::Task task)
    {
        return scheduler().every(seconds, std::move(task));
    }

    Subscription after(double seconds, Scheduler::Task task)
    {
        return scheduler().after(seconds, std::move(task));
    }

private:
    friend class kestrel::modding::ModSlot;

    ModInfo details;
    ModContext* host = nullptr;
};

}
