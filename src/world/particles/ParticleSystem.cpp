#include "world/Particles.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <list>
#include <numbers>
#include <random>
#include <string_view>

namespace kestrel::world {

namespace {

using Vec3 = std::array<double, 3>;
using Variables = std::unordered_map<std::string, double>;

constexpr size_t MaxLiveParticles = 8192;
constexpr size_t MaxEmitters = 2048;
constexpr double MaxStep = 0.1;
constexpr double DefaultActiveTime = 10.0;
constexpr double DefaultMaxParticles = 50.0;
constexpr double DefaultLifetime = 1.0;
constexpr double DefaultNumParticles = 10.0;
constexpr double DefaultSpawnRate = 1.0;
constexpr double DefaultEntityHalfWidth = 0.3;
constexpr double DefaultEntityHalfHeight = 0.9;
constexpr uint8_t FullBright = 0xFF;
constexpr int MaxEventDepth = 3;
constexpr size_t MaxEventSpawnsPerStep = 64;

struct Particle {
    Vec3 position {};
    Vec3 velocity {};
    Vec3 direction { 0.0, 1.0, 0.0 };
    double rotation = 0.0;
    double rotationRate = 0.0;
    double age = 0.0;
    double lifetime = DefaultLifetime;
    double planeSide = 0.0;
    Variables variables;
    uint8_t light = FullBright;
    bool dead = false;
};

struct Emitter {
    uint64_t group = 0;
    const ParticleEffect* effect = nullptr;
    Vec3 position {};
    Vec3 origin {};
    Vec3 attachOffset {};
    std::array<float, 3> direction { 0.0f, 1.0f, 0.0f };
    std::optional<uint64_t> actor;
    Variables variables;
    double age = 0.0;
    double activeTime = DefaultActiveTime;
    double sleepTime = 0.0;
    double sleepAge = 0.0;
    double spawnDebt = 0.0;
    int depth = 0;
    bool sleeping = false;
    bool active = false;
    bool burstDone = false;
    bool stopped = false;
    bool expirationFired = false;
    std::vector<Particle> particles;
};

/**
 * An effect an event started, waiting for the end of the step, and how many
 * events deep it is.
 */
struct EventSpawn {
    uint64_t group = 0;
    ParticleSpawn request;
    int depth = 0;
};

Vec3 add(const Vec3& a, const Vec3& b)
{
    return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
}

Vec3 sub(const Vec3& a, const Vec3& b)
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

Vec3 scale(const Vec3& a, double factor)
{
    return { a[0] * factor, a[1] * factor, a[2] * factor };
}

double dot(const Vec3& a, const Vec3& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

double length(const Vec3& a)
{
    return std::sqrt(dot(a, a));
}

Vec3 normalize(const Vec3& a, const Vec3& fallback = { 0.0, 1.0, 0.0 })
{
    double size = length(a);
    if (size < 1e-9) {
        return fallback;
    }
    return scale(a, 1.0 / size);
}

Vec3 toVec(const std::array<float, 3>& a)
{
    return { a[0], a[1], a[2] };
}

std::array<float, 3> toFloat(const Vec3& a)
{
    return { static_cast<float>(a[0]), static_cast<float>(a[1]), static_cast<float>(a[2]) };
}

Vec3 perpendicular(const Vec3& axis)
{
    Vec3 reference = std::abs(axis[1]) < 0.99 ? Vec3 { 0.0, 1.0, 0.0 } : Vec3 { 1.0, 0.0, 0.0 };
    return normalize(cross(axis, reference));
}

std::string variableKey(const std::string& name)
{
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    for (const char* prefix : { "variable.", "v." }) {
        std::string_view view(prefix);
        if (key.size() > view.size() && key.compare(0, view.size(), view) == 0) {
            return key.substr(view.size());
        }
    }
    return key;
}

std::string blockKey(const std::string& name)
{
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    constexpr std::string_view Namespace = "minecraft:";
    if (key.compare(0, Namespace.size(), Namespace) == 0) {
        return key.substr(Namespace.size());
    }
    return key;
}

bool listsBlock(const std::vector<std::string>& blocks, const std::string& name)
{
    std::string key = blockKey(name);
    for (const std::string& block : blocks) {
        if (blockKey(block) == key) {
            return true;
        }
    }
    return false;
}

}

struct ParticleSystem::State {
    std::list<Emitter> emitters;
    uint64_t nextGroup = 0;
    std::vector<EventSpawn> eventSpawns;
    std::mt19937 random { std::random_device {}() };
    size_t live = 0;

    /**
     * Runs a named event of the emitter's effect where it fired: every action
     * of its sequence and one weighted pick of its randomized actions. The
     * effects they name are queued; sound events have none and do nothing.
     */
    void fireEvent(const Emitter& emitter, const std::string& name, const Vec3& position, const Vec3& velocity, Variables& variables)
    {
        const ParticleEvents& events = emitter.effect->events;
        auto found = events.events.find(name);
        if (found == events.events.end()) {
            return;
        }
        auto act = [&](const ParticleEventAction& action) {
            if (!action.expression.empty()) {
                run(action.expression, variables, 0.0);
            }
            if (action.effect.empty() || emitter.depth >= MaxEventDepth || eventSpawns.size() >= MaxEventSpawnsPerStep) {
                return;
            }
            EventSpawn spawn;
            spawn.group = emitter.group;
            spawn.depth = emitter.depth + 1;
            spawn.request.identifier = action.effect;
            spawn.request.position = position;
            if (action.type == ParticleEventAction::Type::EmitterBound) {
                spawn.request.attachedActor = emitter.actor;
            }
            if (action.type == ParticleEventAction::Type::ParticleWithVelocity && length(velocity) > 1e-9) {
                spawn.request.direction = toFloat(normalize(velocity));
            }
            eventSpawns.push_back(std::move(spawn));
        };
        for (const ParticleEventAction& action : found->second.sequence) {
            act(action);
        }
        const auto& options = found->second.randomized;
        if (!options.empty()) {
            double total = 0.0;
            for (const auto& [weight, action] : options) {
                total += std::max(0.0, weight);
            }
            double pick = uniform(0.0, total);
            for (const auto& [weight, action] : options) {
                pick -= std::max(0.0, weight);
                if (pick <= 0.0) {
                    act(action);
                    break;
                }
            }
        }
    }

    void fireAll(const Emitter& emitter, const std::vector<std::string>& names, const Vec3& position, const Vec3& velocity, Variables& variables)
    {
        for (const std::string& name : names) {
            fireEvent(emitter, name, position, velocity, variables);
        }
    }

    /**
     * Fires the timeline entries whose time falls in [from, to).
     */
    void fireTimeline(const Emitter& emitter, const ParticleTimeline& timeline, double from, double to, const Vec3& position, const Vec3& velocity, Variables& variables)
    {
        for (const auto& [time, names] : timeline) {
            if (time >= from && time < to) {
                fireAll(emitter, names, position, velocity, variables);
            }
        }
    }

    double planeValue(const Emitter& emitter, const Particle& particle) const
    {
        const std::array<float, 4>& plane = emitter.effect->lifetime.killPlane;
        Vec3 relative = emitter.effect->emitter.localPosition ? particle.position : sub(particle.position, emitter.position);
        return plane[0] * relative[0] + plane[1] * relative[1] + plane[2] * relative[2] + plane[3];
    }

    double uniform(double low = 0.0, double high = 1.0)
    {
        return std::uniform_real_distribution<double>(low, high)(random);
    }

    Vec3 randomDirection()
    {
        double z = uniform(-1.0, 1.0);
        double angle = uniform(0.0, 2.0 * std::numbers::pi);
        double radius = std::sqrt(std::max(0.0, 1.0 - z * z));
        return { radius * std::cos(angle), z, radius * std::sin(angle) };
    }

    double run(const ParticleExpression& expression, Variables& variables, double fallback)
    {
        if (expression.empty()) {
            return fallback;
        }
        molang::Scope scope;
        scope.variables = &variables;
        scope.random = uniform();
        return expression.run(scope);
    }

    Vec3 runVector(const std::array<ParticleExpression, 3>& expressions, Variables& variables, double fallback = 0.0)
    {
        return { run(expressions[0], variables, fallback), run(expressions[1], variables, fallback), run(expressions[2], variables, fallback) };
    }

    void runAll(const std::vector<ParticleExpression>& expressions, Variables& variables)
    {
        for (const ParticleExpression& expression : expressions) {
            run(expression, variables, 0.0);
        }
    }

    void randomizeEmitter(Emitter& emitter)
    {
        for (int index = 1; index <= 4; ++index) {
            emitter.variables["emitter_random_" + std::to_string(index)] = uniform();
        }
    }

    void startLoop(Emitter& emitter)
    {
        const ParticleEmitterRules& rules = emitter.effect->emitter;
        emitter.age = 0.0;
        emitter.burstDone = false;
        emitter.spawnDebt = 0.0;
        emitter.sleeping = false;
        randomizeEmitter(emitter);
        emitter.variables["emitter_age"] = 0.0;
        emitter.activeTime = run(rules.activeTime, emitter.variables, DefaultActiveTime);
        emitter.sleepTime = run(rules.sleepTime, emitter.variables, 0.0);
        emitter.variables["emitter_lifetime"] = emitter.activeTime;
    }

    void emitParticle(Emitter& emitter, const ParticleWorld& world)
    {
        if (live >= MaxLiveParticles) {
            return;
        }
        const ParticleEffect& effect = *emitter.effect;
        const ParticleShape& shape = effect.shape;
        Particle particle;
        particle.variables = emitter.variables;
        for (int index = 1; index <= 4; ++index) {
            particle.variables["particle_random_" + std::to_string(index)] = uniform();
        }
        particle.variables["particle_age"] = 0.0;

        Variables& variables = particle.variables;
        Vec3 offset = runVector(shape.offset, variables);
        Vec3 local {};
        Vec3 outward = randomDirection();
        switch (shape.kind) {
        case ParticleShape::Kind::Point:
            break;
        case ParticleShape::Kind::Sphere: {
            double radius = run(shape.radius, variables, 1.0);
            double distance = shape.surfaceOnly ? radius : radius * std::cbrt(uniform());
            local = scale(outward, distance);
            break;
        }
        case ParticleShape::Kind::Box:
        case ParticleShape::Kind::EntityBox: {
            Vec3 half = shape.kind == ParticleShape::Kind::Box
                ? runVector(shape.halfDimensions, variables)
                : Vec3 { DefaultEntityHalfWidth, DefaultEntityHalfHeight, DefaultEntityHalfWidth };
            for (size_t axis = 0; axis < 3; ++axis) {
                local[axis] = uniform(-1.0, 1.0) * half[axis];
            }
            if (shape.surfaceOnly) {
                size_t face = static_cast<size_t>(uniform(0.0, 3.0)) % 3;
                local[face] = uniform() < 0.5 ? -half[face] : half[face];
            }
            if (shape.kind == ParticleShape::Kind::EntityBox) {
                local[1] += DefaultEntityHalfHeight;
            }
            outward = normalize(local, outward);
            break;
        }
        case ParticleShape::Kind::Disc: {
            Vec3 normal = normalize(runVector(shape.planeNormal, variables), { 0.0, 1.0, 0.0 });
            Vec3 tangent = perpendicular(normal);
            Vec3 bitangent = cross(normal, tangent);
            double radius = run(shape.radius, variables, 1.0);
            double distance = shape.surfaceOnly ? radius : radius * std::sqrt(uniform());
            double angle = uniform(0.0, 2.0 * std::numbers::pi);
            local = add(scale(tangent, std::cos(angle) * distance), scale(bitangent, std::sin(angle) * distance));
            outward = normalize(local, outward);
            break;
        }
        }
        if (shape.kind == ParticleShape::Kind::Sphere) {
            outward = normalize(local, outward);
        }

        Vec3 direction = outward;
        if (shape.direction == ParticleShape::Direction::Inwards) {
            direction = scale(outward, -1.0);
        } else if (shape.direction == ParticleShape::Direction::Custom) {
            direction = normalize(runVector(shape.customDirection, variables), { 0.0, 0.0, 0.0 });
        }

        Vec3 spawnOffset = add(offset, local);
        const ParticleMotion& motion = effect.motion;
        if (motion.initialSpeedIsVector) {
            Vec3 speed = runVector(motion.initialSpeedVector, variables);
            particle.velocity = { direction[0] * speed[0], direction[1] * speed[1], direction[2] * speed[2] };
        } else {
            particle.velocity = scale(direction, run(motion.initialSpeed, variables, 0.0));
        }
        particle.direction = direction;
        particle.rotation = run(motion.initialRotation, variables, 0.0);
        particle.rotationRate = run(motion.initialRotationRate, variables, 0.0);
        particle.lifetime = run(effect.lifetime.maxLifetime, variables, DefaultLifetime);
        variables["particle_lifetime"] = particle.lifetime;
        particle.position = effect.emitter.localPosition ? spawnOffset : add(emitter.position, spawnOffset);
        if (motion.parametric) {
            particle.position = effect.emitter.localPosition ? Vec3 {} : emitter.position;
            Vec3 relative = runVector(motion.relativePosition, variables);
            particle.position = add(particle.position, add(spawnOffset, relative));
        }
        particle.light = sampleLight(effect, worldPosition(emitter, particle), world);
        if (effect.lifetime.hasKillPlane) {
            particle.planeSide = planeValue(emitter, particle);
        }
        emitter.particles.push_back(std::move(particle));
        ++live;
        if (!effect.events.particleCreation.empty()) {
            Particle& born = emitter.particles.back();
            fireAll(emitter, effect.events.particleCreation, worldPosition(emitter, born), born.velocity, born.variables);
        }
    }

    Vec3 worldPosition(const Emitter& emitter, const Particle& particle) const
    {
        return emitter.effect->emitter.localPosition ? add(emitter.position, particle.position) : particle.position;
    }

    uint8_t sampleLight(const ParticleEffect& effect, const Vec3& position, const ParticleWorld& world) const
    {
        if (!effect.appearance.lit) {
            return FullBright;
        }
        return world.light(static_cast<int32_t>(std::floor(position[0])), static_cast<int32_t>(std::floor(position[1])), static_cast<int32_t>(std::floor(position[2])));
    }

    void emit(Emitter& emitter, const ParticleWorld& world, double count)
    {
        double cap = run(emitter.effect->emitter.maxParticles, emitter.variables, DefaultMaxParticles);
        size_t limit = cap <= 0.0 ? 0 : static_cast<size_t>(cap);
        int total = static_cast<int>(std::max(0.0, count));
        for (int index = 0; index < total && emitter.particles.size() < limit; ++index) {
            emitParticle(emitter, world);
        }
    }

    void tickEmitter(Emitter& emitter, double dt, const ParticleWorld& world)
    {
        const ParticleEmitterRules& rules = emitter.effect->emitter;
        if (emitter.actor && !emitter.stopped) {
            std::optional<Vec3> actor = world.actorPosition(*emitter.actor);
            if (!actor) {
                emitter.stopped = true;
            } else {
                emitter.position = add(*actor, emitter.attachOffset);
            }
        }
        if (emitter.stopped) {
            return;
        }

        if (emitter.sleeping) {
            emitter.sleepAge += dt;
            if (emitter.sleepAge >= emitter.sleepTime) {
                startLoop(emitter);
                runAll(rules.initialization, emitter.variables);
            }
            return;
        }

        emitter.age += dt;
        emitter.variables["emitter_age"] = emitter.age;
        runAll(rules.perUpdate, emitter.variables);
        if (!emitter.effect->events.emitterTimeline.empty()) {
            fireTimeline(emitter, emitter.effect->events.emitterTimeline, emitter.age - dt, emitter.age, emitter.position, Vec3 {}, emitter.variables);
        }

        bool emitting = false;
        switch (rules.lifetime) {
        case ParticleEmitterRules::Lifetime::Once:
            emitting = emitter.age <= emitter.activeTime || (rules.rate == ParticleEmitterRules::Rate::Instant && !emitter.burstDone);
            if (emitter.age > emitter.activeTime) {
                emitter.stopped = true;
            }
            break;
        case ParticleEmitterRules::Lifetime::Looping:
            emitting = emitter.age <= emitter.activeTime;
            if (!emitting) {
                // Native explosion events own one cycle of these looping definitions.
                if (emitter.effect->identifier == "minecraft:huge_explosion_emitter"
                    || emitter.effect->identifier == "minecraft:huge_explosion_lab_misc_emitter") {
                    emitter.stopped = true;
                } else {
                    emitter.sleeping = true;
                    emitter.sleepAge = 0.0;
                }
            }
            break;
        case ParticleEmitterRules::Lifetime::Expression:
            if (run(rules.expiration, emitter.variables, 0.0) != 0.0) {
                emitter.stopped = true;
                return;
            }
            emitting = run(rules.activation, emitter.variables, 1.0) != 0.0;
            if (emitting && !emitter.active) {
                emitter.burstDone = false;
            }
            break;
        }
        emitter.active = emitting;
        if (!emitting) {
            return;
        }

        switch (rules.rate) {
        case ParticleEmitterRules::Rate::Instant:
            if (!emitter.burstDone) {
                emitter.burstDone = true;
                emit(emitter, world, run(rules.numParticles, emitter.variables, DefaultNumParticles));
            }
            break;
        case ParticleEmitterRules::Rate::Steady: {
            emitter.spawnDebt += run(rules.spawnRate, emitter.variables, DefaultSpawnRate) * dt;
            double whole = std::floor(emitter.spawnDebt);
            emitter.spawnDebt -= whole;
            emit(emitter, world, whole);
            break;
        }
        case ParticleEmitterRules::Rate::Manual:
            break;
        }
    }

    bool collide(Emitter& emitter, Particle& particle, const Vec3& start, Vec3& moved, double dt, const ParticleWorld& world)
    {
        const ParticleMotion& motion = emitter.effect->motion;
        double radius = std::max(0.0, static_cast<double>(motion.collisionRadius));
        Vec3 origin = emitter.effect->emitter.localPosition ? emitter.position : Vec3 {};
        Vec3 from = add(origin, start);
        Vec3 to = add(origin, add(start, moved));
        Vec3 low {};
        Vec3 high {};
        for (size_t axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(from[axis], to[axis]) - radius;
            high[axis] = std::max(from[axis], to[axis]) + radius;
        }
        std::vector<std::array<double, 6>> boxes = world.collisionBoxes(low, high);
        if (boxes.empty()) {
            return false;
        }
        Vec3 position = from;
        double impact = length(particle.velocity);
        bool touched = false;
        for (size_t axis : { size_t { 1 }, size_t { 0 }, size_t { 2 } }) {
            double delta = moved[axis];
            if (delta == 0.0) {
                continue;
            }
            for (const std::array<double, 6>& box : boxes) {
                bool overlaps = true;
                for (size_t other = 0; other < 3; ++other) {
                    if (other == axis) {
                        continue;
                    }
                    if (position[other] + radius <= box[other] || position[other] - radius >= box[other + 3]) {
                        overlaps = false;
                        break;
                    }
                }
                if (!overlaps) {
                    continue;
                }
                if (delta > 0.0 && position[axis] + radius <= box[axis]) {
                    delta = std::min(delta, box[axis] - (position[axis] + radius));
                } else if (delta < 0.0 && position[axis] - radius >= box[axis + 3]) {
                    delta = std::max(delta, box[axis + 3] - (position[axis] - radius));
                }
            }
            if (delta != moved[axis]) {
                touched = true;
                particle.velocity[axis] = -particle.velocity[axis] * motion.coefficientOfRestitution;
            }
            position[axis] += delta;
            moved[axis] = delta;
        }
        if (touched && motion.collisionDrag > 0.0f) {
            double speed = length(particle.velocity);
            if (speed > 0.0) {
                double reduced = std::max(0.0, speed - motion.collisionDrag * dt);
                particle.velocity = scale(particle.velocity, reduced / speed);
            }
        }
        if (touched) {
            for (const auto& [name, minSpeed] : emitter.effect->events.collision) {
                if (impact >= minSpeed) {
                    fireEvent(emitter, name, position, particle.velocity, particle.variables);
                }
            }
        }
        return touched;
    }

    void tickParticle(Emitter& emitter, Particle& particle, double dt, const ParticleWorld& world)
    {
        const ParticleEffect& effect = *emitter.effect;
        const ParticleMotion& motion = effect.motion;
        Variables& variables = particle.variables;
        particle.age += dt;
        variables["particle_age"] = particle.age;
        variables["emitter_age"] = emitter.age;
        variables["emitter_lifetime"] = emitter.activeTime;
        if (particle.age >= particle.lifetime || run(effect.lifetime.expiration, variables, 0.0) != 0.0) {
            particle.dead = true;
            return;
        }
        if (!effect.events.particleTimeline.empty()) {
            fireTimeline(emitter, effect.events.particleTimeline, particle.age - dt, particle.age, worldPosition(emitter, particle), particle.velocity, variables);
        }

        if (motion.parametric) {
            Vec3 base = effect.emitter.localPosition ? Vec3 {} : emitter.origin;
            Vec3 previous = particle.position;
            particle.position = add(base, runVector(motion.relativePosition, variables));
            Vec3 direction = runVector(motion.direction, variables);
            particle.direction = length(direction) > 0.0 ? normalize(direction) : normalize(sub(particle.position, previous), particle.direction);
            particle.velocity = dt > 0.0 ? scale(sub(particle.position, previous), 1.0 / dt) : particle.velocity;
            particle.rotation = run(motion.rotation, variables, particle.rotation);
        } else {
            Vec3 acceleration = runVector(motion.linearAcceleration, variables);
            double drag = run(motion.linearDragCoefficient, variables, 0.0);
            for (size_t axis = 0; axis < 3; ++axis) {
                particle.velocity[axis] += (acceleration[axis] - drag * particle.velocity[axis]) * dt;
            }
            double rotationAcceleration = run(motion.rotationAcceleration, variables, 0.0);
            double rotationDrag = run(motion.rotationDragCoefficient, variables, 0.0);
            particle.rotationRate += (rotationAcceleration - rotationDrag * particle.rotationRate) * dt;
            particle.rotation += particle.rotationRate * dt;

            Vec3 moved = scale(particle.velocity, dt);
            if (motion.collides && run(motion.collisionEnabled, variables, 1.0) != 0.0) {
                if (collide(emitter, particle, particle.position, moved, dt, world) && motion.expireOnContact) {
                    particle.dead = true;
                    return;
                }
            }
            particle.position = add(particle.position, moved);
            double speed = length(particle.velocity);
            if (speed > std::max(1e-9, static_cast<double>(effect.appearance.minSpeedThreshold))) {
                particle.direction = scale(particle.velocity, 1.0 / speed);
            }
        }
        if (effect.appearance.directionMode == ParticleAppearance::DirectionMode::Custom) {
            particle.direction = normalize(runVector(effect.appearance.customDirection, variables), particle.direction);
        }
        if (effect.lifetime.hasKillPlane) {
            double side = planeValue(emitter, particle);
            if ((particle.planeSide > 0.0 && side <= 0.0) || (particle.planeSide < 0.0 && side >= 0.0)) {
                particle.dead = true;
                return;
            }
            if (particle.planeSide == 0.0) {
                particle.planeSide = side;
            }
        }

        Vec3 position = worldPosition(emitter, particle);
        const ParticleLifetime& lifetime = effect.lifetime;
        if (!lifetime.expireInBlocks.empty() || !lifetime.expireOutsideBlocks.empty()) {
            std::string name = world.blockName(static_cast<int32_t>(std::floor(position[0])), static_cast<int32_t>(std::floor(position[1])), static_cast<int32_t>(std::floor(position[2])));
            if (listsBlock(lifetime.expireInBlocks, name)) {
                particle.dead = true;
                return;
            }
            if (!lifetime.expireOutsideBlocks.empty() && !listsBlock(lifetime.expireOutsideBlocks, name)) {
                particle.dead = true;
                return;
            }
        }
        particle.light = sampleLight(effect, position, world);
    }

    void step(double dt, const ParticleWorld& world)
    {
        for (auto it = emitters.begin(); it != emitters.end();) {
            Emitter& emitter = *it;
            const ParticleEvents& events = emitter.effect->events;
            for (Particle& particle : emitter.particles) {
                tickParticle(emitter, particle, dt, world);
                if (particle.dead && !events.particleExpiration.empty()) {
                    fireAll(emitter, events.particleExpiration, worldPosition(emitter, particle), particle.velocity, particle.variables);
                }
            }
            size_t before = emitter.particles.size();
            std::erase_if(emitter.particles, [](const Particle& particle) {
                return particle.dead;
            });
            live -= before - emitter.particles.size();
            tickEmitter(emitter, dt, world);
            if (emitter.stopped && !emitter.expirationFired) {
                emitter.expirationFired = true;
                fireAll(emitter, events.emitterExpiration, emitter.position, Vec3 {}, emitter.variables);
            }
            if (emitter.stopped && emitter.particles.empty()) {
                it = emitters.erase(it);
            } else {
                ++it;
            }
        }
    }

    void quad(const Emitter& emitter, Particle& particle, const ParticleCamera& camera, std::vector<ParticleQuad>& out)
    {
        const ParticleEffect& effect = *emitter.effect;
        const ParticleAppearance& appearance = effect.appearance;
        Variables& variables = particle.variables;

        ParticleQuad result;
        Vec3 center = worldPosition(emitter, particle);
        result.center = center;
        result.texture = effect.texture;
        result.material = effect.material;
        result.light = particle.light;

        double width = run(appearance.size[0], variables, 0.0);
        double height = run(appearance.size[1], variables, 0.0);

        Vec3 forward = normalize(toVec(camera.forward), { 0.0, 0.0, 1.0 });
        Vec3 cameraUp = normalize(toVec(camera.up));
        Vec3 cameraRight = normalize(cross(forward, cameraUp), { 1.0, 0.0, 0.0 });
        cameraUp = normalize(cross(cameraRight, forward));
        Vec3 worldUp { 0.0, 1.0, 0.0 };
        Vec3 toCamera = normalize(sub(camera.position, center), scale(forward, -1.0));
        Vec3 direction = normalize(particle.direction);
        Vec3 right = cameraRight;
        Vec3 up = cameraUp;
        switch (appearance.facing) {
        case ParticleAppearance::Facing::RotateXyz:
            break;
        case ParticleAppearance::Facing::RotateY:
            right = normalize(Vec3 { cameraRight[0], 0.0, cameraRight[2] }, { 1.0, 0.0, 0.0 });
            up = worldUp;
            break;
        case ParticleAppearance::Facing::LookatXyz:
            right = normalize(cross(worldUp, toCamera), cameraRight);
            up = normalize(cross(toCamera, right));
            break;
        case ParticleAppearance::Facing::LookatY: {
            Vec3 flat = normalize(Vec3 { toCamera[0], 0.0, toCamera[2] }, { 0.0, 0.0, 1.0 });
            right = normalize(cross(worldUp, flat));
            up = worldUp;
            break;
        }
        case ParticleAppearance::Facing::LookatDirection:
            up = direction;
            right = normalize(cross(direction, toCamera), perpendicular(direction));
            break;
        case ParticleAppearance::Facing::DirectionX:
            right = direction;
            up = perpendicular(direction);
            break;
        case ParticleAppearance::Facing::DirectionY:
            up = direction;
            right = perpendicular(direction);
            break;
        case ParticleAppearance::Facing::DirectionZ:
            right = perpendicular(direction);
            up = normalize(cross(direction, right));
            break;
        case ParticleAppearance::Facing::EmitterTransformXy:
            right = { 1.0, 0.0, 0.0 };
            up = { 0.0, 1.0, 0.0 };
            break;
        case ParticleAppearance::Facing::EmitterTransformXz:
            right = { 1.0, 0.0, 0.0 };
            up = { 0.0, 0.0, 1.0 };
            break;
        case ParticleAppearance::Facing::EmitterTransformYz:
            right = { 0.0, 0.0, 1.0 };
            up = { 0.0, 1.0, 0.0 };
            break;
        }
        double angle = particle.rotation * std::numbers::pi / 180.0;
        double cosine = std::cos(angle);
        double sine = std::sin(angle);
        Vec3 rotatedRight = add(scale(right, cosine), scale(up, sine));
        Vec3 rotatedUp = sub(scale(up, cosine), scale(right, sine));
        result.right = toFloat(scale(rotatedRight, width));
        result.up = toFloat(scale(rotatedUp, height));

        double textureWidth = appearance.textureWidth > 0.0f ? appearance.textureWidth : 1.0;
        double textureHeight = appearance.textureHeight > 0.0f ? appearance.textureHeight : 1.0;
        double u = 0.0;
        double v = 0.0;
        double uSize = textureWidth;
        double vSize = textureHeight;
        if (appearance.flipbook) {
            double frames = std::max(1.0, std::floor(run(appearance.maxFrame, variables, 1.0)));
            double frame = 0.0;
            if (appearance.stretchToLifetime) {
                frame = std::floor(particle.age / std::max(1e-6, particle.lifetime) * frames);
            } else {
                frame = std::floor(particle.age * run(appearance.framesPerSecond, variables, 0.0));
            }
            frame = appearance.loop ? std::fmod(std::max(0.0, frame), frames) : std::clamp(frame, 0.0, frames - 1.0);
            u = run(appearance.flipbookBase[0], variables, 0.0) + frame * run(appearance.flipbookStep[0], variables, 0.0);
            v = run(appearance.flipbookBase[1], variables, 0.0) + frame * run(appearance.flipbookStep[1], variables, 0.0);
            uSize = run(appearance.flipbookSize[0], variables, 0.0);
            vSize = run(appearance.flipbookSize[1], variables, 0.0);
        } else if (!appearance.uv[0].empty() || !appearance.uvSize[0].empty()) {
            u = run(appearance.uv[0], variables, 0.0);
            v = run(appearance.uv[1], variables, 0.0);
            uSize = run(appearance.uvSize[0], variables, textureWidth);
            vSize = run(appearance.uvSize[1], variables, textureHeight);
        }
        result.uv = {
            static_cast<float>(u / textureWidth),
            static_cast<float>(v / textureHeight),
            static_cast<float>((u + uSize) / textureWidth),
            static_cast<float>((v + vSize) / textureHeight),
        };

        if (appearance.gradient && !appearance.gradientStops.empty()) {
            double t = run(appearance.gradientInterpolant, variables, 0.0);
            const auto& stops = appearance.gradientStops;
            auto evaluate = [&](const std::array<ParticleExpression, 4>& stop) {
                std::array<float, 4> color {};
                for (size_t channel = 0; channel < 4; ++channel) {
                    color[channel] = static_cast<float>(run(stop[channel], variables, 1.0));
                }
                return color;
            };
            std::array<float, 4> color {};
            if (t <= stops.front().first) {
                color = evaluate(stops.front().second);
            } else if (t >= stops.back().first) {
                color = evaluate(stops.back().second);
            } else {
                for (size_t index = 1; index < stops.size(); ++index) {
                    if (t <= stops[index].first) {
                        std::array<float, 4> low = evaluate(stops[index - 1].second);
                        std::array<float, 4> high = evaluate(stops[index].second);
                        double span = stops[index].first - stops[index - 1].first;
                        float blend = span > 0.0 ? static_cast<float>((t - stops[index - 1].first) / span) : 1.0f;
                        for (size_t channel = 0; channel < 4; ++channel) {
                            color[channel] = low[channel] + (high[channel] - low[channel]) * blend;
                        }
                        break;
                    }
                }
            }
            result.color = color;
        } else if (appearance.tinted) {
            for (size_t channel = 0; channel < 4; ++channel) {
                result.color[channel] = static_cast<float>(run(appearance.color[channel], variables, 1.0));
            }
        }
        for (float& channel : result.color) {
            channel = std::clamp(channel, 0.0f, 1.0f);
        }
        out.push_back(std::move(result));
    }
};

ParticleSystem::ParticleSystem(const ParticleLibrary& library)
    : library(library)
    , state(std::make_unique<State>())
{
}

ParticleSystem::~ParticleSystem() = default;

void ParticleSystem::spawn(const ParticleSpawn& request)
{
    start(request, 0);
}

/**
 * Starts an effect depth events away from the one the client asked for, and
 * fires its emitter creation events.
 */
bool ParticleSystem::start(const ParticleSpawn& request, int depth, uint64_t group)
{
    const ParticleEffect* effect = library.find(request.identifier);
    if (!effect) {
        return false;
    }
    if (state->emitters.size() >= MaxEmitters) {
        state->live -= state->emitters.front().particles.size();
        state->emitters.pop_front();
    }
    Emitter& emitter = state->emitters.emplace_back();
    emitter.group = group;
    emitter.effect = effect;
    emitter.position = request.position;
    emitter.origin = request.position;
    emitter.direction = request.direction;
    emitter.actor = request.attachedActor;
    for (const auto& [name, value] : request.variables) {
        emitter.variables[variableKey(name)] = value;
    }
    emitter.depth = depth;
    state->startLoop(emitter);
    state->runAll(effect->emitter.initialization, emitter.variables);
    if (!effect->events.emitterCreation.empty()) {
        state->fireAll(emitter, effect->events.emitterCreation, emitter.position, Vec3 {}, emitter.variables);
    }
    return true;
}

uint64_t ParticleSystem::spawnTracked(const ParticleSpawn& request)
{
    if (state->nextGroup == UINT64_MAX) return 0;
    uint64_t group = ++state->nextGroup;
    return start(request, 0, group) ? group : 0;
}

bool ParticleSystem::active(uint64_t handle) const
{
    if (!handle) return false;
    return std::any_of(state->emitters.begin(), state->emitters.end(), [handle](const Emitter& emitter) { return emitter.group == handle; })
        || std::any_of(state->eventSpawns.begin(), state->eventSpawns.end(), [handle](const EventSpawn& spawn) { return spawn.group == handle; });
}

bool ParticleSystem::move(uint64_t handle, const Vec3& position)
{
    if (!handle) return false;
    auto root = std::find_if(state->emitters.begin(), state->emitters.end(), [handle](const Emitter& emitter) { return emitter.group == handle; });
    if (root == state->emitters.end()) return false;
    Vec3 delta = sub(position, root->position);
    for (Emitter& emitter : state->emitters) {
        if (emitter.group != handle) continue;
        emitter.position = add(emitter.position, delta);
        emitter.origin = add(emitter.origin, delta);
        emitter.actor.reset();
    }
    for (EventSpawn& spawn : state->eventSpawns) {
        if (spawn.group != handle) continue;
        spawn.request.position = add(spawn.request.position, delta);
        spawn.request.attachedActor.reset();
    }
    return true;
}

bool ParticleSystem::remove(uint64_t handle)
{
    if (!handle) return false;
    bool removed = false;
    std::erase_if(state->emitters, [&](const Emitter& emitter) {
        if (emitter.group != handle) return false;
        state->live -= emitter.particles.size();
        removed = true;
        return true;
    });
    removed |= std::erase_if(state->eventSpawns, [handle](const EventSpawn& spawn) { return spawn.group == handle; }) != 0;
    return removed;
}

void ParticleSystem::detach(uint64_t runtimeId)
{
    for (Emitter& emitter : state->emitters) {
        if (emitter.actor && *emitter.actor == runtimeId) {
            emitter.stopped = true;
        }
    }
}

void ParticleSystem::clear()
{
    state->emitters.clear();
    state->eventSpawns.clear();
    state->live = 0;
}

void ParticleSystem::tick(double seconds, const ParticleWorld& world)
{
    if (seconds <= 0.0) {
        return;
    }
    for (Emitter& emitter : state->emitters) {
        if (emitter.actor && emitter.age == 0.0 && emitter.attachOffset == Vec3 {}) {
            std::optional<Vec3> actor = world.actorPosition(*emitter.actor);
            if (actor) {
                emitter.attachOffset = sub(emitter.origin, *actor);
            }
        }
    }
    double remaining = seconds;
    while (remaining > 0.0) {
        double dt = std::min(remaining, MaxStep);
        state->step(dt, world);
        std::vector<EventSpawn> spawns = std::move(state->eventSpawns);
        state->eventSpawns.clear();
        for (const EventSpawn& pending : spawns) {
            start(pending.request, pending.depth, pending.group);
        }
        remaining -= dt;
    }
}

void ParticleSystem::collect(const ParticleCamera& camera, std::vector<ParticleQuad>& out) const
{
    out.reserve(out.size() + state->live);
    for (Emitter& emitter : state->emitters) {
        for (Particle& particle : emitter.particles) {
            state->quad(emitter, particle, camera, out);
        }
    }
}

size_t ParticleSystem::liveParticles() const
{
    return state->live;
}

}
