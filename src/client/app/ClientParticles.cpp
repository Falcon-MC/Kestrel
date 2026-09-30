#include "client/ParticleRenderer.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <random>
#include <string_view>
#include <unordered_set>

namespace kestrel {

namespace {

constexpr double TickRate = 20.0;
constexpr int MaxTicksPerFrame = 4;

std::mt19937& randomSource()
{
    static std::mt19937 source { std::random_device {}() };
    return source;
}

double uniform(double low, double high)
{
    std::uniform_real_distribution<double> distribution(low, high);
    return distribution(randomSource());
}

bool chance(double probability)
{
    return uniform(0.0, 1.0) < probability;
}

/**
 * Consumes whole 20 Hz ticks from a clock fed with the frame delta, capped
 * so a long stall does not burst every missed tick at once.
 */
int consumeTicks(double& clock, double seconds)
{
    clock += std::max(0.0, seconds);
    int ticks = static_cast<int>(std::floor(clock * TickRate));
    clock -= ticks / TickRate;
    if (ticks > MaxTicksPerFrame) {
        clock = 0.0;
        ticks = MaxTicksPerFrame;
    }
    return ticks;
}

world::ParticleSpawn spawnAt(std::string identifier, const std::array<double, 3>& position)
{
    world::ParticleSpawn spawn;
    spawn.identifier = std::move(identifier);
    spawn.position = position;
    return spawn;
}

std::string_view shortName(std::string_view name)
{
    size_t colon = name.find(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

bool endsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::array<double, 3> cellPoint(const std::array<int32_t, 3>& cell, double x, double y, double z)
{
    return { cell[0] + x, cell[1] + y, cell[2] + z };
}

void flameAndSmoke(const std::array<int32_t, 3>& cell, double height, std::vector<world::ParticleSpawn>& out)
{
    std::array<double, 3> point = cellPoint(cell, 0.5, height, 0.5);
    out.push_back(spawnAt("minecraft:basic_smoke_particle", point));
    out.push_back(spawnAt("minecraft:basic_flame_particle", point));
}

void furnaceFront(const std::array<int32_t, 3>& cell, bool flame, std::vector<world::ParticleSpawn>& out)
{
    double along = uniform(-0.3, 0.3);
    std::array<double, 3> point = cellPoint(cell, 0.5 + along, uniform(0.0, 6.0 / 16.0), 0.5);
    out.push_back(spawnAt("minecraft:basic_smoke_particle", point));
    if (flame) {
        out.push_back(spawnAt("minecraft:basic_flame_particle", point));
    }
}

void campfire(const std::array<int32_t, 3>& cell, std::vector<world::ParticleSpawn>& out)
{
    std::array<double, 3> point = cellPoint(cell, 0.5 + uniform(-1.0 / 3.0, 1.0 / 3.0), 0.5 + uniform(0.0, 0.5), 0.5 + uniform(-1.0 / 3.0, 1.0 / 3.0));
    world::ParticleSpawn smoke = spawnAt("minecraft:campfire_smoke_particle", point);
    smoke.variables["variable.size"] = uniform(0.5, 1.0);
    out.push_back(std::move(smoke));
}

/**
 * The families of blocks that give off particles on their own.
 */
enum class EmittingBlock {
    None,
    Torch,
    RedstoneTorch,
    Furnace,
    Smoker,
    Campfire,
    Lava,
    EndRod,
    Portal,
    Candle,
    CandleCake,
};

EmittingBlock emittingBlock(std::string_view fullName)
{
    std::string_view name = shortName(fullName);
    if (name == "torch" || name == "wall_torch" || name == "soul_torch" || name == "soul_wall_torch" || endsWith(name, "colored_torch_red") || endsWith(name, "colored_torch_blue") || endsWith(name, "colored_torch_green") || endsWith(name, "colored_torch_purple")) {
        return EmittingBlock::Torch;
    }
    if (name == "redstone_torch" || name == "lit_redstone_torch") {
        return EmittingBlock::RedstoneTorch;
    }
    if (name == "lit_furnace" || name == "lit_blast_furnace") {
        return EmittingBlock::Furnace;
    }
    if (name == "lit_smoker") {
        return EmittingBlock::Smoker;
    }
    if (name == "campfire" || name == "soul_campfire") {
        return EmittingBlock::Campfire;
    }
    if (name == "lava" || name == "flowing_lava") {
        return EmittingBlock::Lava;
    }
    if (name == "end_rod") {
        return EmittingBlock::EndRod;
    }
    if (name == "portal") {
        return EmittingBlock::Portal;
    }
    if (endsWith(name, "candle_cake")) {
        return EmittingBlock::CandleCake;
    }
    if (endsWith(name, "candle")) {
        return EmittingBlock::Candle;
    }
    return EmittingBlock::None;
}

/**
 * Starts the ambient effects one block gives on one display tick, with the
 * per tick odds of the block's animation.
 */
void blockTick(const ClientParticleEmitters::BlockState& block, std::vector<world::ParticleSpawn>& out)
{
    const std::array<int32_t, 3>& cell = block.cell;
    switch (emittingBlock(block.name)) {
    case EmittingBlock::None:
        break;
    case EmittingBlock::Torch:
        if (chance(0.25)) {
            flameAndSmoke(cell, 0.7, out);
        }
        break;
    case EmittingBlock::RedstoneTorch:
        if (chance(0.2)) {
            out.push_back(spawnAt("minecraft:redstone_torch_dust_particle", cellPoint(cell, 0.5 + uniform(-0.1, 0.1), 0.7 + uniform(-0.1, 0.1), 0.5 + uniform(-0.1, 0.1))));
        }
        break;
    case EmittingBlock::Furnace:
        if (chance(0.1)) {
            furnaceFront(cell, true, out);
        }
        break;
    case EmittingBlock::Smoker:
        if (chance(0.1)) {
            furnaceFront(cell, false, out);
        }
        break;
    case EmittingBlock::Campfire:
        if (block.lit && chance(0.1)) {
            campfire(cell, out);
        }
        break;
    case EmittingBlock::Lava:
        if (chance(0.01)) {
            out.push_back(spawnAt("minecraft:lava_particle", cellPoint(cell, uniform(0.0, 1.0), 1.0, uniform(0.0, 1.0))));
        }
        if (chance(0.01)) {
            out.push_back(spawnAt("minecraft:lava_drip_particle", cellPoint(cell, uniform(0.0, 1.0), -0.05, uniform(0.0, 1.0))));
        }
        break;
    case EmittingBlock::EndRod:
        if (chance(0.2)) {
            out.push_back(spawnAt("minecraft:endrod", cellPoint(cell, 0.5 + uniform(-0.2, 0.2), 0.5 + uniform(-0.2, 0.2), 0.5 + uniform(-0.2, 0.2))));
        }
        break;
    case EmittingBlock::Portal:
        for (int i = 0; i < 4; ++i) {
            if (chance(0.25)) {
                out.push_back(spawnAt("minecraft:basic_portal_particle", cellPoint(cell, uniform(0.0, 1.0), uniform(0.0, 1.0), uniform(0.0, 1.0))));
            }
        }
        break;
    case EmittingBlock::Candle:
        if (block.lit && chance(0.3)) {
            flameAndSmoke(cell, 0.5, out);
        }
        break;
    case EmittingBlock::CandleCake:
        if (block.lit && chance(0.3)) {
            flameAndSmoke(cell, 0.95, out);
        }
        break;
    }
}

/**
 * The swirl colors of the status effects by effect id, as 0xRRGGBB.
 */
constexpr uint32_t EffectColors[] = {
    0x000000,
    0x7CAFC6,
    0x5A6C81,
    0xD9C043,
    0x4A4217,
    0x932423,
    0xF82423,
    0x430A09,
    0x22FF4C,
    0x551D4A,
    0xCD5CAB,
    0x99453A,
    0xE49A3A,
    0x2E5299,
    0x7F8392,
    0x1F1F23,
    0x1F1FA1,
    0x587653,
    0x484D48,
    0x4E9331,
    0x352A27,
    0xF87D23,
    0x2552A5,
    0xF82423,
    0xCEFFFF,
    0x4E9331,
    0x1DC2D1,
    0xFFEFD1,
    0x0B6138,
    0x44FF44,
    0x292721,
    0x16A6A6,
    0xBDC9FF,
    0x78695A,
    0x99FFA3,
    0x8C9B8C,
    0xDE4058,
};

std::array<double, 4> colorChannels(uint32_t argb)
{
    double alpha = static_cast<double>((argb >> 24) & 0xFF) / 255.0;
    return {
        static_cast<double>((argb >> 16) & 0xFF) / 255.0,
        static_cast<double>((argb >> 8) & 0xFF) / 255.0,
        static_cast<double>(argb & 0xFF) / 255.0,
        alpha > 0.0 ? alpha : 1.0,
    };
}

}

uint32_t ClientParticleEmitters::effectColor(int32_t effectId)
{
    if (effectId <= 0 || size_t(effectId) >= std::size(EffectColors)) {
        return 0;
    }
    return 0xFF000000u | EffectColors[effectId];
}

bool ClientParticleEmitters::emitsParticles(std::string_view name)
{
    return emittingBlock(name) != EmittingBlock::None;
}

/**
 * Starts the hit effects of an attack on the target's upper body: the
 * critical burst on a critical hit and a spray of crit sparks when the
 * weapon is enchanted.
 */
void ClientParticleEmitters::attacked(uint64_t targetRuntimeId, const std::array<double, 3>& targetPosition, float targetHeight, bool critical, bool enchanted, std::vector<world::ParticleSpawn>& out)
{
    std::array<double, 3> point { targetPosition[0], targetPosition[1] + targetHeight * 0.75, targetPosition[2] };
    if (critical) {
        world::ParticleSpawn burst = spawnAt("minecraft:critical_hit_emitter", point);
        burst.attachedActor = targetRuntimeId;
        out.push_back(std::move(burst));
    }
    if (enchanted) {
        for (int i = 0; i < 16; ++i) {
            double x = uniform(-1.0, 1.0);
            double y = uniform(-1.0, 1.0);
            double z = uniform(-1.0, 1.0);
            if (x * x + y * y + z * z > 1.0) {
                continue;
            }
            world::ParticleSpawn spark = spawnAt("minecraft:basic_crit_particle", { point[0] + x * 0.5, point[1] + y * 0.5, point[2] + z * 0.5 });
            spark.variables["variable.direction.x"] = x;
            spark.variables["variable.direction.y"] = y + 0.2;
            spark.variables["variable.direction.z"] = z;
            out.push_back(std::move(spark));
        }
    }
}

/**
 * Runs the display ticks of every actor: dust kicked up under the feet
 * while sprinting on the ground and the swirls of the potion effects it
 * carries, one random effect color per swirl.
 */
void ClientParticleEmitters::actors(const std::vector<ActorState>& actors, double seconds, std::vector<world::ParticleSpawn>& out)
{
    std::unordered_set<uint64_t> seen;
    for (const ActorState& actor : actors) {
        seen.insert(actor.runtimeId);
        int ticks = consumeTicks(actorClock[actor.runtimeId], seconds);
        for (int tick = 0; tick < ticks; ++tick) {
            if (actor.sprinting && actor.onGround) {
                std::array<double, 3> feet { actor.position[0] + uniform(-0.5, 0.5) * actor.width, actor.position[1] + 0.1, actor.position[2] + uniform(-0.5, 0.5) * actor.width };
                world::ParticleSpawn dust = spawnAt("minecraft:basic_smoke_particle", feet);
                dust.direction = { 0.0f, 1.5f, 0.0f };
                out.push_back(std::move(dust));
            }
            if (!actor.effectColors.empty() && chance(0.25)) {
                std::uniform_int_distribution<size_t> pick(0, actor.effectColors.size() - 1);
                std::array<double, 4> color = colorChannels(actor.effectColors[pick(randomSource())]);
                std::array<double, 3> point { actor.position[0] + uniform(-0.5, 0.5) * actor.width, actor.position[1] + uniform(0.0, 1.0) * actor.height, actor.position[2] + uniform(-0.5, 0.5) * actor.width };
                world::ParticleSpawn swirl = spawnAt("minecraft:mobspell_emitter", point);
                swirl.variables["variable.color.r"] = color[0];
                swirl.variables["variable.color.g"] = color[1];
                swirl.variables["variable.color.b"] = color[2];
                swirl.variables["variable.color.a"] = color[3];
                out.push_back(std::move(swirl));
            }
        }
    }
    for (auto it = actorClock.begin(); it != actorClock.end();) {
        if (seen.count(it->first) == 0) {
            it = actorClock.erase(it);
        } else {
            ++it;
        }
    }
}

/**
 * Runs the display ticks of the lit blocks near the player: flames and
 * smoke of torches, furnaces and candles, campfire smoke, lava pops and
 * drips, end rod sparks, portal swirls and redstone torch dust.
 */
void ClientParticleEmitters::blocks(const std::vector<BlockState>& nearby, double seconds, std::vector<world::ParticleSpawn>& out)
{
    int ticks = consumeTicks(blockClock, seconds);
    for (int tick = 0; tick < ticks; ++tick) {
        for (const BlockState& block : nearby) {
            blockTick(block, out);
        }
    }
}

}
