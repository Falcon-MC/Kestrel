#pragma once

#include "world/MolangScript.h"
#include "world/PackSource.h"
#include "world/ServerPack.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kestrel::world {

/**
 * A Molang value of a particle component, either a constant or an
 * expression run against the emitter or particle it belongs to.
 */
using ParticleExpression = molang::Script;

/**
 * Where an emitter puts new particles, and in which direction they start
 * moving: from its point, on or inside a sphere, box or disc, or over the
 * box of the entity it is attached to.
 */
struct ParticleShape {
    enum class Kind { Point, Sphere, Box, Disc, EntityBox };
    enum class Direction { Outwards, Inwards, Custom };

    Kind kind = Kind::Point;
    std::array<ParticleExpression, 3> offset {};
    ParticleExpression radius;
    std::array<ParticleExpression, 3> halfDimensions {};
    std::array<ParticleExpression, 3> planeNormal {};
    bool surfaceOnly = false;
    Direction direction = Direction::Outwards;
    std::array<ParticleExpression, 3> customDirection {};
};

/**
 * How an emitter releases particles over its life: all at once, steadily, or
 * never on its own, and how long it lives and loops.
 */
struct ParticleEmitterRules {
    enum class Rate { Instant, Steady, Manual };
    enum class Lifetime { Once, Looping, Expression };

    Rate rate = Rate::Instant;
    ParticleExpression numParticles;
    ParticleExpression spawnRate;
    ParticleExpression maxParticles;

    Lifetime lifetime = Lifetime::Once;
    ParticleExpression activeTime;
    ParticleExpression sleepTime;
    ParticleExpression activation;
    ParticleExpression expiration;

    bool localPosition = false;
    bool localRotation = false;
    bool localVelocity = false;

    std::vector<ParticleExpression> initialization;
    std::vector<ParticleExpression> perUpdate;
};

/**
 * How a particle moves: a dynamic motion integrating acceleration and drag,
 * or a parametric one placing it from expressions of its age.
 */
struct ParticleMotion {
    bool parametric = false;
    std::array<ParticleExpression, 3> linearAcceleration {};
    ParticleExpression linearDragCoefficient;
    ParticleExpression rotationAcceleration;
    ParticleExpression rotationDragCoefficient;
    std::array<ParticleExpression, 3> relativePosition {};
    std::array<ParticleExpression, 3> direction {};
    ParticleExpression rotation;

    ParticleExpression initialSpeed;
    std::array<ParticleExpression, 3> initialSpeedVector {};
    bool initialSpeedIsVector = false;
    ParticleExpression initialRotation;
    ParticleExpression initialRotationRate;

    bool collides = false;
    ParticleExpression collisionEnabled;
    float collisionRadius = 0.0f;
    float collisionDrag = 0.0f;
    float coefficientOfRestitution = 0.0f;
    bool expireOnContact = false;
};

/**
 * How long a particle lives and what ends it early. The kill plane is
 * a * x + b * y + c * z + d = 0 around the emitter; a particle crossing it
 * dies.
 */
struct ParticleLifetime {
    ParticleExpression maxLifetime;
    ParticleExpression expiration;
    std::vector<std::string> expireInBlocks;
    std::vector<std::string> expireOutsideBlocks;
    bool hasKillPlane = false;
    std::array<float, 4> killPlane {};
};

/**
 * One thing an event does: start another effect where it fires, bound to the
 * emitter's entity or not, and run an expression on the emitter or particle
 * that fired it.
 */
struct ParticleEventAction {
    enum class Type { Emitter, EmitterBound, Particle, ParticleWithVelocity };

    std::string effect;
    Type type = Type::Emitter;
    ParticleExpression expression;
};

/**
 * A named event of an effect: actions that all run, and actions of which one
 * is picked at random by weight.
 */
struct ParticleEvent {
    std::vector<ParticleEventAction> sequence;
    std::vector<std::pair<double, ParticleEventAction>> randomized;
};

/**
 * Timed lists of event names: each entry fires once the age passes its time.
 */
using ParticleTimeline = std::vector<std::pair<double, std::vector<std::string>>>;

/**
 * The events an effect defines and when they fire: when the emitter starts
 * and ends and along its timeline, when a particle is born and dies and
 * along its own timeline, and when a particle hits a block at least as fast
 * as a minimum speed.
 */
struct ParticleEvents {
    std::unordered_map<std::string, ParticleEvent> events;
    std::vector<std::string> emitterCreation;
    std::vector<std::string> emitterExpiration;
    ParticleTimeline emitterTimeline;
    std::vector<std::string> particleCreation;
    std::vector<std::string> particleExpiration;
    ParticleTimeline particleTimeline;
    std::vector<std::pair<std::string, float>> collision;

    bool empty() const
    {
        return events.empty();
    }
};

/**
 * How a particle is drawn: a camera facing quad of a given size, cut out of
 * the effect texture by a fixed rectangle or a flipbook, tinted by a color
 * or a gradient over an expression.
 */
struct ParticleAppearance {
    enum class Facing {
        RotateXyz,
        RotateY,
        LookatXyz,
        LookatY,
        LookatDirection,
        DirectionX,
        DirectionY,
        DirectionZ,
        EmitterTransformXy,
        EmitterTransformXz,
        EmitterTransformYz,
    };

    /**
     * Where the direction the lookat_direction and direction_* facings use
     * comes from: the particle's velocity once it moves faster than the
     * threshold (the last direction is kept below it), or an expression.
     */
    enum class DirectionMode { DeriveFromVelocity, Custom };

    std::array<ParticleExpression, 2> size {};
    Facing facing = Facing::RotateXyz;
    DirectionMode directionMode = DirectionMode::DeriveFromVelocity;
    std::array<ParticleExpression, 3> customDirection {};
    float minSpeedThreshold = 0.01f;

    float textureWidth = 1.0f;
    float textureHeight = 1.0f;
    std::array<ParticleExpression, 2> uv {};
    std::array<ParticleExpression, 2> uvSize {};

    bool flipbook = false;
    std::array<ParticleExpression, 2> flipbookBase {};
    std::array<ParticleExpression, 2> flipbookSize {};
    std::array<ParticleExpression, 2> flipbookStep {};
    ParticleExpression framesPerSecond;
    ParticleExpression maxFrame;
    bool stretchToLifetime = false;
    bool loop = false;

    bool tinted = false;
    std::array<ParticleExpression, 4> color {};
    bool gradient = false;
    ParticleExpression gradientInterpolant;
    std::vector<std::pair<float, std::array<ParticleExpression, 4>>> gradientStops;

    bool lit = false;
};

/**
 * Which material an effect draws with, from its render_parameters.
 */
enum class ParticleMaterial {
    AlphaTest,
    Blend,
    Add,
};

/**
 * One effect of a particles/*.json file, every component the game reads
 * turned into the rules above. Components the loader does not know are
 * listed in unsupported so they can be reported.
 */
struct ParticleEffect {
    std::string identifier;
    std::string texture;
    ParticleMaterial material = ParticleMaterial::AlphaTest;
    ParticleEmitterRules emitter;
    ParticleShape shape;
    ParticleMotion motion;
    ParticleLifetime lifetime;
    ParticleAppearance appearance;
    ParticleEvents events;
    std::unordered_map<std::string, std::string> curves;
    std::vector<std::string> unsupported;
};

/**
 * Every particle effect of the game and the server packs, a pack's effect
 * replacing the game's one of the same identifier.
 */
class ParticleLibrary {
public:
    /**
     * Reads the game's effects from the "particles" archive of the game
     * (archiveEntries and readArchived), then the particles/*.json files of
     * the packs, the first pack of the stack winning as it does for every
     * other resource.
     */
    void load(PackSource& game, const std::vector<std::shared_ptr<const PackFiles>>& packs);

    const ParticleEffect* find(const std::string& identifier) const;

    size_t size() const
    {
        return effects.size();
    }

    const std::unordered_map<std::string, ParticleEffect>& all() const
    {
        return effects;
    }

private:
    std::unordered_map<std::string, ParticleEffect> effects;
};

/**
 * A request to start an effect: its identifier, where, facing which way,
 * the Molang variables it starts with, and the runtime id of the entity it
 * follows when it is attached to one.
 */
struct ParticleSpawn {
    std::string identifier;
    std::array<double, 3> position {};
    std::array<float, 3> direction { 0.0f, 1.0f, 0.0f };
    std::unordered_map<std::string, double> variables;
    std::optional<uint64_t> attachedActor;
};

/**
 * What the simulation asks of the world it runs in: the collision boxes
 * around a point, the block name at a cell, the light there and where an
 * attached entity is now.
 */
class ParticleWorld {
public:
    virtual ~ParticleWorld() = default;

    /**
     * Collision boxes of the blocks touching the box from low to high, in
     * world coordinates, each as minX, minY, minZ, maxX, maxY, maxZ.
     */
    virtual std::vector<std::array<double, 6>> collisionBoxes(const std::array<double, 3>& low, const std::array<double, 3>& high) const = 0;
    virtual std::string blockName(int32_t x, int32_t y, int32_t z) const = 0;

    /**
     * Block light in the low nibble and sky light in the high one.
     */
    virtual uint8_t light(int32_t x, int32_t y, int32_t z) const = 0;
    virtual std::optional<std::array<double, 3>> actorPosition(uint64_t runtimeId) const = 0;
};

/**
 * One particle ready to draw: its center in world coordinates, the two half
 * extents of its quad already turned to face the camera, the texture it
 * samples, its UV rectangle in that texture from 0 to 1, its color and the
 * material it draws with.
 */
struct ParticleQuad {
    std::array<double, 3> center {};
    std::array<float, 3> right {};
    std::array<float, 3> up {};
    std::string texture;
    std::array<float, 4> uv {};
    std::array<float, 4> color { 1.0f, 1.0f, 1.0f, 1.0f };
    uint8_t light = 0xFF;
    ParticleMaterial material = ParticleMaterial::AlphaTest;
};

/**
 * The camera the particles face this frame.
 */
struct ParticleCamera {
    std::array<double, 3> position {};
    std::array<float, 3> forward { 0.0f, 0.0f, 1.0f };
    std::array<float, 3> up { 0.0f, 1.0f, 0.0f };
};

/**
 * Every live emitter and particle. It owns no world and no GPU state: the
 * client feeds it spawns and ticks it, then turns its quads into draws.
 */
class ParticleSystem {
public:
    explicit ParticleSystem(const ParticleLibrary& library);
    ~ParticleSystem();

    /**
     * Starts an effect; an identifier the library does not know is ignored.
     */
    void spawn(const ParticleSpawn& request);
    uint64_t spawnTracked(const ParticleSpawn& request);
    bool active(uint64_t handle) const;
    bool move(uint64_t handle, const std::array<double, 3>& position);
    bool remove(uint64_t handle);

    /**
     * Stops every emitter attached to the entity, letting its particles
     * finish their life.
     */
    void detach(uint64_t runtimeId);
    void clear();

    /**
     * Advances every emitter and particle by seconds of game time.
     */
    void tick(double seconds, const ParticleWorld& world);

    /**
     * Appends the quads of every live particle, facing the camera.
     */
    void collect(const ParticleCamera& camera, std::vector<ParticleQuad>& out) const;

    size_t liveParticles() const;

private:
    struct State;

    bool start(const ParticleSpawn& request, int depth, uint64_t group = 0);

    const ParticleLibrary& library;
    std::unique_ptr<State> state;
};

}
