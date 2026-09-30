#include "world/PackSource.h"
#include "world/Particles.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace kestrel::world;

namespace {

constexpr double TickSeconds = 1.0 / 20.0;
constexpr int TicksPerSecond = 20;
constexpr int SimulatedSeconds = 5;
constexpr int DrainSeconds = 30;
constexpr size_t LiveParticleCap = 20000;
constexpr size_t MinimumEffects = 100;
constexpr float UvEpsilon = 1.0e-3f;

/**
 * A world of air above a solid floor whose top is at y = 0, fully lit, with
 * no entities.
 */
class FlatWorld : public ParticleWorld {
public:
    std::vector<std::array<double, 6>> collisionBoxes(const std::array<double, 3>& low, const std::array<double, 3>& high) const override
    {
        std::vector<std::array<double, 6>> boxes;
        if (low[1] > 0.0) {
            return boxes;
        }
        int32_t minX = static_cast<int32_t>(std::floor(low[0]));
        int32_t maxX = static_cast<int32_t>(std::floor(high[0]));
        int32_t minZ = static_cast<int32_t>(std::floor(low[2]));
        int32_t maxZ = static_cast<int32_t>(std::floor(high[2]));
        if (maxX - minX > 64 || maxZ - minZ > 64) {
            boxes.push_back({ low[0], -1.0, low[2], high[0], 0.0, high[2] });
            return boxes;
        }
        for (int32_t x = minX; x <= maxX; ++x) {
            for (int32_t z = minZ; z <= maxZ; ++z) {
                boxes.push_back({ static_cast<double>(x), -1.0, static_cast<double>(z), static_cast<double>(x) + 1.0, 0.0, static_cast<double>(z) + 1.0 });
            }
        }
        return boxes;
    }

    std::string blockName(int32_t, int32_t y, int32_t) const override
    {
        return y < 0 ? "minecraft:stone" : "minecraft:air";
    }

    uint8_t light(int32_t, int32_t, int32_t) const override
    {
        return 0xFF;
    }

    std::optional<std::array<double, 3>> actorPosition(uint64_t) const override
    {
        return std::nullopt;
    }
};

/**
 * Collects the problems of one effect run.
 */
struct EffectReport {
    std::vector<std::string> failures;
    size_t peakParticles = 0;
    size_t peakQuads = 0;
    size_t finalParticles = 0;

    void fail(const std::string& message)
    {
        if (std::find(failures.begin(), failures.end(), message) == failures.end()) {
            failures.push_back(message);
        }
    }
};

template <size_t Count, typename T>
bool finite(const std::array<T, Count>& values)
{
    for (const T& value : values) {
        if (!std::isfinite(static_cast<double>(value))) {
            return false;
        }
    }
    return true;
}

/**
 * Checks every quad of one frame for non finite values and UVs outside the
 * texture.
 */
void checkQuads(const std::vector<ParticleQuad>& quads, EffectReport& report)
{
    for (const ParticleQuad& quad : quads) {
        if (!finite(quad.center)) {
            report.fail("non finite center");
        }
        if (!finite(quad.right) || !finite(quad.up)) {
            report.fail("non finite extents");
        }
        if (!finite(quad.uv)) {
            report.fail("non finite uv");
        } else {
            for (float value : quad.uv) {
                if (value < -UvEpsilon || value > 1.0f + UvEpsilon) {
                    report.fail("uv outside [0,1]");
                }
            }
        }
        if (!finite(quad.color)) {
            report.fail("non finite color");
        }
    }
}

/**
 * The camera every frame looks from: a few blocks back and up, facing the
 * origin.
 */
ParticleCamera fixedCamera()
{
    ParticleCamera camera;
    camera.position = { 0.0, 2.0, -6.0 };
    float length = std::sqrt(2.0f * 2.0f + 6.0f * 6.0f);
    camera.forward = { 0.0f, -2.0f / length, 6.0f / length };
    camera.up = { 0.0f, 6.0f / length, 2.0f / length };
    return camera;
}

/**
 * Spawns one effect at the origin, runs it for SimulatedSeconds collecting
 * its quads every second, then lets a once emitter drain.
 */
EffectReport runEffect(const ParticleLibrary& library, const ParticleEffect& effect, const ParticleWorld& world)
{
    EffectReport report;
    ParticleSystem system(library);
    ParticleSpawn request;
    request.identifier = effect.identifier;
    system.spawn(request);
    ParticleCamera camera = fixedCamera();
    std::vector<ParticleQuad> quads;
    for (int second = 0; second < SimulatedSeconds; ++second) {
        for (int tick = 0; tick < TicksPerSecond; ++tick) {
            system.tick(TickSeconds, world);
            size_t live = system.liveParticles();
            report.peakParticles = std::max(report.peakParticles, live);
            if (live > LiveParticleCap) {
                report.fail("live particles above cap");
            }
        }
        quads.clear();
        system.collect(camera, quads);
        report.peakQuads = std::max(report.peakQuads, quads.size());
        checkQuads(quads, report);
    }
    if (effect.emitter.lifetime == ParticleEmitterRules::Lifetime::Once) {
        for (int tick = 0; tick < DrainSeconds * TicksPerSecond && system.liveParticles() > 0; ++tick) {
            system.tick(TickSeconds, world);
        }
        if (system.liveParticles() > 0) {
            report.fail("once emitter never empties");
        }
    }
    report.finalParticles = system.liveParticles();
    return report;
}

const char* lifetimeName(ParticleEmitterRules::Lifetime lifetime)
{
    switch (lifetime) {
    case ParticleEmitterRules::Lifetime::Once:
        return "once";
    case ParticleEmitterRules::Lifetime::Looping:
        return "looping";
    case ParticleEmitterRules::Lifetime::Expression:
        return "expression";
    }
    return "?";
}

}

int main()
{
    std::filesystem::path root = PackSource::locateVanilla();
    if (root.empty()) {
        std::printf("FAIL vanilla resource pack not found (set KESTREL_VANILLA_PACK)\n");
        return 1;
    }
    PackSource game(root);
    ParticleLibrary library;
    library.load(game, {});
    size_t archived = game.archiveEntries("particles").size();
    std::printf("loaded %zu effects from %zu archive entries in %s\n", library.size(), archived, root.string().c_str());
    int failures = 0;
    if (library.size() < MinimumEffects) {
        std::printf("FAIL expected at least %zu effects\n", MinimumEffects);
        ++failures;
    }

    std::vector<const ParticleEffect*> effects;
    for (const auto& [identifier, effect] : library.all()) {
        effects.push_back(&effect);
    }
    std::sort(effects.begin(), effects.end(), [](const ParticleEffect* left, const ParticleEffect* right) {
        return left->identifier < right->identifier;
    });

    FlatWorld world;
    std::map<std::string, std::set<std::string>> unsupported;
    for (const ParticleEffect* effect : effects) {
        EffectReport report = runEffect(library, *effect, world);
        for (const std::string& component : effect->unsupported) {
            unsupported[component].insert(effect->identifier);
        }
        std::printf("%-4s %-60s %-10s peak=%-6zu quads=%-6zu end=%-6zu unsupported=%zu", report.failures.empty() ? "ok" : "FAIL", effect->identifier.c_str(), lifetimeName(effect->emitter.lifetime), report.peakParticles,
            report.peakQuads, report.finalParticles, effect->unsupported.size());
        for (const std::string& failure : report.failures) {
            std::printf(" [%s]", failure.c_str());
        }
        std::printf("\n");
        if (!report.failures.empty()) {
            ++failures;
        }
    }

    std::printf("\nunsupported components: %zu\n", unsupported.size());
    for (const auto& [component, users] : unsupported) {
        std::printf("  %s (%zu):", component.c_str(), users.size());
        for (const std::string& identifier : users) {
            std::printf(" %s", identifier.c_str());
        }
        std::printf("\n");
    }
    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
