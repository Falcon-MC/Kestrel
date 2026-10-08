#include "world/PackSource.h"
#include "world/Particles.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
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

int main(int argc, char** argv)
{
    if (argc == 2 && (std::string_view(argv[1]) == "--explosion-lifetime" || std::string_view(argv[1]) == "--tracked-effects")) {
        const auto root = std::filesystem::temp_directory_path()
            / ("kestrel-particle-lifetime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root / "__brarchive");
        const std::array<std::string, 3> identifiers {
            "minecraft:huge_explosion_emitter",
            "minecraft:huge_explosion_lab_misc_emitter",
            "test:continuous",
        };
        std::string archive(16 + identifiers.size() * 256, '\0');
        auto writeNumber = [&](size_t offset, uint64_t value, size_t bytes) {
            for (size_t i = 0; i < bytes; ++i) {
                archive[offset + i] = static_cast<char>(value >> (i * 8));
            }
        };
        writeNumber(0, 0x267052A0B125277Dull, 8);
        writeNumber(8, identifiers.size(), 4);
        const size_t dataStart = archive.size();
        for (size_t i = 0; i < identifiers.size(); ++i) {
            const std::string name = "effect" + std::to_string(i) + ".json";
            std::string definition = "{\"particle_effect\":{\"description\":{\"identifier\":\"" + identifiers[i]
                + "\"},\"components\":{\"minecraft:emitter_lifetime_looping\":{\"active_time\":0.4,\"sleep_time\":0},"
                  "\"minecraft:emitter_rate_steady\":{\"spawn_rate\":20,\"max_particles\":50},"
                  "\"minecraft:particle_lifetime_expression\":{\"max_lifetime\":0.5},"
                  "\"minecraft:particle_appearance_billboard\":{\"size\":[1,1]}}}}";
            if (i == 2 && std::string_view(argv[1]) == "--tracked-effects") {
                definition.resize(definition.size() - 3);
                definition += ",\"minecraft:emitter_lifetime_events\":{\"creation_event\":\"child\"}},"
                    "\"events\":{\"child\":{\"particle_effect\":{\"effect\":\"minecraft:huge_explosion_emitter\",\"type\":\"emitter\"}}}}}";
            }
            const size_t entry = 16 + i * 256;
            archive[entry] = static_cast<char>(name.size());
            archive.replace(entry + 1, name.size(), name);
            writeNumber(entry + 248, archive.size() - dataStart, 4);
            writeNumber(entry + 252, definition.size(), 4);
            archive += definition;
        }
        {
            std::ofstream file(root / "__brarchive/particles.brarchive", std::ios::binary);
            file.write(archive.data(), static_cast<std::streamsize>(archive.size()));
        }
        PackSource pack(root);
        ParticleLibrary library;
        library.load(pack, {});
        std::filesystem::remove_all(root);
        if (library.size() != identifiers.size()) {
            std::printf("FAIL synthetic particle library\n");
            return 1;
        }
        FlatWorld world;
        if (std::string_view(argv[1]) == "--tracked-effects") {
            ParticleSystem system(library);
            ParticleSpawn spawn;
            spawn.identifier = "test:continuous";
            auto queued = system.spawnTracked(spawn);
            if (!system.remove(queued)) return 1;
            system.tick(0.2, world);
            if (system.liveParticles() != 0) return 1;
            auto handle = system.spawnTracked(spawn);
            if (!handle || !system.active(handle) || system.active(0)) return 1;
            if (!system.move(handle, { 10.0, 2.0, 3.0 })) return 1;
            system.tick(0.2, world);
            std::vector<ParticleQuad> quads;
            system.collect(fixedCamera(), quads);
            if (quads.empty() || std::abs(quads.front().center[0] - 10.0) > 0.01) return 1;
            if (!system.remove(handle) || system.liveParticles() != 0) return 1;
            handle = system.spawnTracked(spawn);
            system.spawn(spawn);
            system.tick(0.2, world);
            if (!system.remove(handle) || system.active(handle) || system.remove(handle) || system.liveParticles() == 0) return 1;
            auto next = system.spawnTracked(spawn);
            system.clear();
            auto afterClear = system.spawnTracked(spawn);
            if (!afterClear || afterClear == next || system.active(next) || system.remove(next)) return 1;
            system.clear();
            class MovingWorld : public FlatWorld {
            public:
                std::array<double, 3> position {};
                std::optional<std::array<double, 3>> actorPosition(uint64_t) const override { return position; }
            } moving;
            spawn.attachedActor = 7;
            handle = system.spawnTracked(spawn);
            system.tick(0.1, moving);
            moving.position = { 20.0, 0.0, 0.0 };
            system.tick(0.2, moving);
            quads.clear();
            system.collect(fixedCamera(), quads);
            if (std::none_of(quads.begin(), quads.end(), [](const ParticleQuad& quad) { return quad.center[0] > 19.9; })) return 1;
            if (!system.remove(handle) || system.liveParticles() != 0) return 1;
            spawn.identifier = "test:missing";
            if (system.spawnTracked(spawn)) return 1;
            std::printf("PASS tracked particle movement, removal, isolation and stale handles\n");
            return 0;
        }
        for (size_t i = 0; i < identifiers.size(); ++i) {
            ParticleSystem system(library);
            ParticleSpawn spawn;
            spawn.identifier = identifiers[i];
            system.spawn(spawn);
            system.tick(0.2, world);
            if (system.liveParticles() == 0) {
                std::printf("FAIL missing initial burst: %s\n", identifiers[i].c_str());
                return 1;
            }
            system.tick(0.25, world);
            if (system.liveParticles() == 0) {
                std::printf("FAIL particles removed before their lifetime: %s\n", identifiers[i].c_str());
                return 1;
            }
            for (int tick = 0; tick < 100; ++tick) {
                system.tick(TickSeconds, world);
                if (tick >= 20 && i < 2 && system.liveParticles() != 0) {
                    std::printf("FAIL explosion keeps emitting: %s\n", identifiers[i].c_str());
                    return 1;
                }
            }
            std::vector<ParticleQuad> quads;
            system.collect(fixedCamera(), quads);
            if ((i < 2 && !quads.empty()) || (i == 2 && quads.empty())) {
                std::printf("FAIL explosion drain or continuous emitter: %s\n", identifiers[i].c_str());
                return 1;
            }
        }
        std::printf("PASS explosion lifetime and continuous emitter\n");
        return 0;
    }
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
        if ((effect->identifier == "minecraft:huge_explosion_emitter"
                || effect->identifier == "minecraft:huge_explosion_lab_misc_emitter")
            && report.finalParticles != 0) {
            report.fail("explosion keeps emitting");
        }
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
