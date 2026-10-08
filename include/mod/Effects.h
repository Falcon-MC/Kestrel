#pragma once

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

/** Local pack-defined effects. Main thread only; handles belong to this mod.
 * Zero means creation failed. Unloading the mod or leaving the world removes
 * its effects.
 */
class Particles {
public:
    virtual ~Particles() = default;

    virtual bool supported() const = 0;
    virtual ParticleHandle spawn(ParticleOptions options) = 0;
    virtual bool active(ParticleHandle handle) const = 0;
    // Moves emitters, leaving already emitted world-space particles in place.
    // Also detaches the effect from any entity it was following.
    virtual bool move(ParticleHandle handle, Vec3 position) = 0;
    // Removes existing particles and child effects immediately.
    virtual bool remove(ParticleHandle handle) = 0;
    virtual void clear() = 0;
};

/** Named sounds from the active packs. Volume is a multiplier (0..4), pitch
 * is 0.1..4. The game's master/category volumes still apply. Main thread only.
 * Handles belong to this mod and are cleaned up on unload or world changes.
 */
class Audio {
public:
    virtual ~Audio() = default;

    virtual bool supported() const = 0;
    virtual SoundHandle play(SoundOptions options) = 0;
    virtual bool playing(SoundHandle handle) const = 0;
    virtual bool stop(SoundHandle handle) = 0;
    virtual bool setVolume(SoundHandle handle, float volume) = 0;
    // Only positional sounds can be moved.
    virtual bool setPosition(SoundHandle handle, Vec3 position) = 0;
    virtual void stopAll() = 0;
};

}
