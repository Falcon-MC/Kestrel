#pragma once

#include "mod/Event.h"
#include "mod/Types.h"

#include <optional>
#include <unordered_map>

namespace kestrel::mod {

using ParticleHandle = uint64_t;
using SoundHandle = uint64_t;

struct ParticleOptions {
    std::string identifier;
    Vec3 position;
    Vec3 direction { 0.0, 1.0, 0.0 };
    std::optional<uint64_t> attachedEntity;
    std::unordered_map<std::string, double> variables;
};

struct SoundOptions {
    std::string name;
    // No position plays a flat sound; otherwise it uses world coordinates.
    std::optional<Vec3> position;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
};

namespace detail {

// Requests use the existing bus so API 3 binaries keep their original vtables.
struct EffectRequest : Event {
    KESTREL_EVENT("kestrel:local_effect_request/v1")
    enum class Kind { Particle, Sound };
    enum class Action { Supported, Create, Active, Move, Volume, Remove, Clear };
    Kind kind = Kind::Particle;
    Action action = Action::Supported;
    uint64_t handle = 0;
    ParticleOptions particle;
    SoundOptions sound;
    Vec3 position;
    float volume = 1.0f;
    bool result = false;
};

class EffectControl {
public:
    EffectControl(EventBus& bus, EffectRequest::Kind kind) : bus(bus), kind(kind) { }

    bool supported() const { return request(EffectRequest::Action::Supported); }

protected:
    bool request(EffectRequest::Action action, uint64_t handle = 0, Vec3 position = {}, float volume = 1.0f) const
    {
        EffectRequest event;
        event.kind = kind;
        event.action = action;
        event.handle = handle;
        event.position = position;
        event.volume = volume;
        bus.post(event);
        return event.result;
    }

    EventBus& bus;
    EffectRequest::Kind kind;
};

}

/** Local pack-defined effects. Main thread only; handles belong to this mod.
 * Zero means creation failed. Unloading the mod or leaving the world removes
 * its effects. On older hosts supported() is false and operations do nothing.
 */
class Particles : public detail::EffectControl {
public:
    explicit Particles(EventBus& bus) : EffectControl(bus, detail::EffectRequest::Kind::Particle) { }

    ParticleHandle spawn(ParticleOptions options) const
    {
        detail::EffectRequest event;
        event.kind = kind;
        event.action = detail::EffectRequest::Action::Create;
        event.particle = std::move(options);
        bus.post(event);
        return event.result ? event.handle : 0;
    }

    bool active(ParticleHandle handle) const { return request(detail::EffectRequest::Action::Active, handle); }
    // Moves emitters, leaving already emitted world-space particles in place.
    // Also detaches the effect from any entity it was following.
    bool move(ParticleHandle handle, Vec3 position) const { return request(detail::EffectRequest::Action::Move, handle, position); }
    // Removes existing particles and child effects immediately.
    bool remove(ParticleHandle handle) const { return request(detail::EffectRequest::Action::Remove, handle); }
    void clear() const { request(detail::EffectRequest::Action::Clear); }
};

/** Named sounds from the active packs. Volume is a multiplier (0..4), pitch
 * is 0.1..4. The game's master/category volumes still apply. Main thread only.
 * Handles belong to this mod and are cleaned up on unload or world changes.
 */
class Audio : public detail::EffectControl {
public:
    explicit Audio(EventBus& bus) : EffectControl(bus, detail::EffectRequest::Kind::Sound) { }

    SoundHandle play(SoundOptions options) const
    {
        detail::EffectRequest event;
        event.kind = kind;
        event.action = detail::EffectRequest::Action::Create;
        event.sound = std::move(options);
        bus.post(event);
        return event.result ? event.handle : 0;
    }

    bool playing(SoundHandle handle) const { return request(detail::EffectRequest::Action::Active, handle); }
    bool stop(SoundHandle handle) const { return request(detail::EffectRequest::Action::Remove, handle); }
    bool setVolume(SoundHandle handle, float volume) const { return request(detail::EffectRequest::Action::Volume, handle, {}, volume); }
    // Only positional sounds can be moved.
    bool setPosition(SoundHandle handle, Vec3 position) const { return request(detail::EffectRequest::Action::Move, handle, position); }
    void stopAll() const { request(detail::EffectRequest::Action::Clear); }
};

}
