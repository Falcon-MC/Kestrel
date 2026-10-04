#include "client/ParticleTriggers.h"

#include "Core/Json/Json.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <string_view>

namespace kestrel {

namespace {

constexpr int32_t LegacyParticleMask = 0x4000;

enum class ColorSource {
    None,
    Argb,
    Note,
};

struct EffectMapping {
    std::string_view identifier;
    ColorSource color = ColorSource::None;
    std::string_view colorVariable = "variable.color";
};

/**
 * The effect a legacy particle type shows when a level event names it with
 * the legacy mask.
 */
std::optional<EffectMapping> legacyEffect(int32_t type)
{
    switch (type) {
    case 1:
        return EffectMapping { "minecraft:basic_bubble_particle" };
    case 2:
        return EffectMapping { "minecraft:basic_bubble_particle_manual" };
    case 3:
        return EffectMapping { "minecraft:basic_crit_particle" };
    case 5:
    case 10:
        return EffectMapping { "minecraft:basic_smoke_particle" };
    case 6:
        return EffectMapping { "minecraft:explosion_particle" };
    case 7:
        return EffectMapping { "minecraft:water_evaporation_actor_emitter" };
    case 8:
        return EffectMapping { "minecraft:basic_flame_particle" };
    case 9:
        return EffectMapping { "minecraft:lava_particle" };
    case 11:
        return EffectMapping { "minecraft:redstone_wire_dust_particle" };
    case 12:
        return EffectMapping { "minecraft:rising_border_dust_particle" };
    case 13:
    case 14:
    case 36:
    case 50:
        return EffectMapping { "minecraft:breaking_item_icon" };
    case 15:
        return EffectMapping { "minecraft:large_explosion" };
    case 16:
        return EffectMapping { "minecraft:huge_explosion_emitter" };
    case 17:
        return EffectMapping { "minecraft:mobflame_single" };
    case 18:
        return EffectMapping { "minecraft:heart_particle" };
    case 19:
        return EffectMapping { "minecraft:breaking_item_terrain" };
    case 20:
    case 64:
        return EffectMapping { "minecraft:mycelium_dust_particle" };
    case 21:
        return EffectMapping { "minecraft:basic_portal_particle" };
    case 22:
        return EffectMapping { "minecraft:mob_portal" };
    case 23:
        return EffectMapping { "minecraft:water_splash_particle" };
    case 24:
        return EffectMapping { "minecraft:water_splash_particle_manual" };
    case 25:
        return EffectMapping { "minecraft:water_wake_particle" };
    case 26:
        return EffectMapping { "minecraft:water_drip_particle" };
    case 27:
        return EffectMapping { "minecraft:lava_drip_particle" };
    case 28:
        return EffectMapping { "minecraft:honey_drip_particle" };
    case 29:
        return EffectMapping { "minecraft:stalactite_water_drip_particle" };
    case 30:
        return EffectMapping { "minecraft:stalactite_lava_drip_particle" };
    case 31:
        return EffectMapping { "minecraft:falling_dust", ColorSource::Argb };
    case 32:
    case 34:
        return EffectMapping { "minecraft:mobspell_emitter", ColorSource::Argb };
    case 33:
        return EffectMapping { "minecraft:mobspell_ambient", ColorSource::Argb };
    case 35:
        return EffectMapping { "minecraft:ink_emitter" };
    case 37:
        return EffectMapping { "minecraft:rain_splash_particle" };
    case 38:
        return EffectMapping { "minecraft:villager_angry" };
    case 39:
        return EffectMapping { "minecraft:villager_happy" };
    case 40:
        return EffectMapping { "minecraft:enchanting_table_particle" };
    case 42:
        return EffectMapping { "minecraft:note_particle", ColorSource::Note, "variable.note_color" };
    case 43:
        return EffectMapping { "minecraft:witchspell_emitter" };
    case 44:
        return EffectMapping { "minecraft:crop_growth_emitter" };
    case 46:
        return EffectMapping { "minecraft:endrod" };
    case 47:
    case 68:
        return EffectMapping { "minecraft:dragon_breath_fire" };
    case 48:
        return EffectMapping { "minecraft:llama_spit_smoke" };
    case 49:
        return EffectMapping { "minecraft:totem_particle" };
    case 54:
        return EffectMapping { "minecraft:balloon_gas_particle" };
    case 55:
        return EffectMapping { "minecraft:colored_flame_particle", ColorSource::Argb };
    case 56:
        return EffectMapping { "minecraft:sparkler_emitter", ColorSource::Argb };
    case 57:
        return EffectMapping { "minecraft:conduit_particle" };
    case 58:
        return EffectMapping { "minecraft:bubble_column_up_particle" };
    case 59:
        return EffectMapping { "minecraft:bubble_column_down_particle" };
    case 60:
        return EffectMapping { "minecraft:sneeze" };
    case 61:
        return EffectMapping { "minecraft:shulker_bullet" };
    case 62:
        return EffectMapping { "minecraft:bleach" };
    case 63:
        return EffectMapping { "minecraft:dragon_destroy_block" };
    case 65:
        return EffectMapping { "minecraft:falling_border_dust_particle" };
    case 66:
        return EffectMapping { "minecraft:campfire_smoke_particle" };
    case 67:
        return EffectMapping { "minecraft:campfire_tall_smoke_particle" };
    case 69:
        return EffectMapping { "minecraft:dragon_breath_trail" };
    case 70:
        return EffectMapping { "minecraft:blue_flame_particle" };
    case 71:
        return EffectMapping { "minecraft:soul_particle" };
    case 72:
        return EffectMapping { "minecraft:obsidian_tear_particle" };
    case 73:
        return EffectMapping { "minecraft:portal_reverse_particle" };
    case 74:
        return EffectMapping { "minecraft:snowflake_particle" };
    case 75:
        return EffectMapping { "minecraft:vibration_signal" };
    case 76:
        return EffectMapping { "minecraft:sculk_sensor_redstone_particle" };
    case 77:
        return EffectMapping { "minecraft:spore_blossom_shower_particle" };
    case 78:
        return EffectMapping { "minecraft:spore_blossom_ambient_particle" };
    case 79:
        return EffectMapping { "minecraft:wax_particle" };
    case 80:
        return EffectMapping { "minecraft:electric_spark_particle" };
    case 81:
        return EffectMapping { "minecraft:candle_flame_particle" };
    case 82:
        return EffectMapping { "minecraft:shriek_particle" };
    case 83:
        return EffectMapping { "minecraft:sculk_soul_particle" };
    case 84:
        return EffectMapping { "minecraft:sonic_explosion" };
    case 86:
        return EffectMapping { "minecraft:cherry_leaves_particle" };
    case 87:
        return EffectMapping { "minecraft:dust_plume" };
    case 88:
        return EffectMapping { "minecraft:white_smoke_particle" };
    case 89:
        return EffectMapping { "minecraft:wind_explosion_emitter" };
    case 90:
        return EffectMapping { "minecraft:breeze_wind_explosion_emitter" };
    case 91:
        return EffectMapping { "minecraft:vault_connection_particle" };
    case 94:
        return EffectMapping { "minecraft:creaking_crumble_body" };
    case 95:
        return EffectMapping { "minecraft:pale_oak_leaves_particle" };
    case 96:
        return EffectMapping { "minecraft:eyeblossom_open" };
    case 97:
        return EffectMapping { "minecraft:eyeblossom_close" };
    case 98:
        return EffectMapping { "minecraft:green_flame_particle" };
    case 99:
        return EffectMapping { "minecraft:pause_mob_growth" };
    case 100:
        return EffectMapping { "minecraft:reset_mob_growth" };
    case 101:
        return EffectMapping { "minecraft:sulfur_cube_goo" };
    default:
        return std::nullopt;
    }
}

/**
 * The effect a dedicated particle level event shows.
 */
std::optional<EffectMapping> eventEffect(int32_t eventId)
{
    switch (eventId) {
    case 2000:
    case 2029:
    case 3609:
        return EffectMapping { "minecraft:basic_smoke_particle" };
    case 2001:
    case 2021:
        return EffectMapping { "minecraft:block_destruct" };
    case 2002:
        return EffectMapping { "minecraft:splash_spell_emitter", ColorSource::Argb };
    case 2003:
        return EffectMapping { "minecraft:eyeofender_death_explode_particle" };
    case 2004:
        return EffectMapping { "minecraft:mob_block_spawn_emitter" };
    case 2007:
        return EffectMapping { "minecraft:death_explosion_emitter" };
    case 2012:
        return EffectMapping { "minecraft:critical_hit_emitter" };
    case 2013:
        return EffectMapping { "minecraft:mob_portal" };
    case 2015:
        return EffectMapping { "minecraft:basic_bubble_particle" };
    case 2016:
        return EffectMapping { "minecraft:water_evaporation_bucket_emitter" };
    case 2019:
        return EffectMapping { "minecraft:egg_destroy_emitter" };
    case 2020:
        return EffectMapping { "minecraft:ice_evaporation_emitter" };
    case 2022:
        return EffectMapping { "minecraft:knockback_roar_particle" };
    case 2025:
        return EffectMapping { "minecraft:huge_explosion_emitter" };
    case 2026:
        return EffectMapping { "minecraft:explosion_particle" };
    default:
        return std::nullopt;
    }
}

void setColor(world::ParticleSpawn& spawn, std::string_view name, double red, double green, double blue, double alpha)
{
    std::string base(name);
    spawn.variables[base + ".r"] = red;
    spawn.variables[base + ".g"] = green;
    spawn.variables[base + ".b"] = blue;
    spawn.variables[base + ".a"] = alpha;
}

void applyColor(world::ParticleSpawn& spawn, const EffectMapping& mapping, int32_t data)
{
    if (mapping.color == ColorSource::Argb) {
        uint32_t argb = uint32_t(data);
        double alpha = double((argb >> 24) & 0xFF) / 255.0;
        setColor(spawn, mapping.colorVariable, double((argb >> 16) & 0xFF) / 255.0, double((argb >> 8) & 0xFF) / 255.0,
            double(argb & 0xFF) / 255.0, alpha == 0.0 ? 1.0 : alpha);
    } else if (mapping.color == ColorSource::Note) {
        double hue = double(data) / 24.0;
        double turn = 2.0 * std::numbers::pi;
        auto channel = [&](double offset) {
            return std::max(0.0, std::sin((hue + offset) * turn) * 0.65 + 0.35);
        };
        setColor(spawn, mapping.colorVariable, channel(0.0), channel(1.0 / 3.0), channel(2.0 / 3.0), 1.0);
    }
}

void storeValue(world::ParticleSpawn& spawn, const std::string& name, const json::Value& value)
{
    if (value.isNumber()) {
        spawn.variables[name] = value.number();
        return;
    }
    if (value.mType == json::Value::Type::Boolean) {
        spawn.variables[name] = value.boolean() ? 1.0 : 0.0;
        return;
    }
    if (!value.isObject()) {
        return;
    }
    const json::Value* inner = value.get("value");
    if (inner != nullptr && value.get("type") != nullptr) {
        storeValue(spawn, name, *inner);
        return;
    }
    for (const std::string& key : value.mKeys) {
        const json::Value* member = value.get(key);
        if (member != nullptr) {
            storeValue(spawn, name + "." + key, *member);
        }
    }
}

std::string variableName(const std::string& name)
{
    if (name.starts_with("variable.")) {
        return name;
    }
    if (name.starts_with("v.")) {
        return "variable." + name.substr(2);
    }
    return "variable." + name;
}

}

std::optional<world::ParticleSpawn> particleForLevelEvent(int32_t eventId, const std::array<double, 3>& position, int32_t data)
{
    std::optional<EffectMapping> mapping = (eventId & LegacyParticleMask) != 0 && (eventId & ~0x7FFF) == 0
        ? legacyEffect(eventId & ~LegacyParticleMask)
        : eventEffect(eventId);
    if (!mapping) {
        return std::nullopt;
    }
    world::ParticleSpawn spawn;
    spawn.identifier = std::string(mapping->identifier);
    spawn.position = position;
    applyColor(spawn, *mapping, data);
    return spawn;
}

world::ParticleSpawn particleForSpawnPacket(const std::string& identifier, const std::array<double, 3>& position, const std::string& molangJson)
{
    world::ParticleSpawn spawn;
    spawn.identifier = identifier;
    spawn.position = position;
    if (molangJson.empty()) {
        return spawn;
    }
    std::unique_ptr<json::Value> root = json::parse(molangJson);
    if (!root) {
        return spawn;
    }
    if (root->isArray()) {
        for (const std::unique_ptr<json::Value>& entry : root->mArray) {
            if (!entry || !entry->isObject()) {
                continue;
            }
            const json::Value* name = entry->get("name");
            const json::Value* value = entry->get("value");
            if (name == nullptr || !name->isString() || value == nullptr) {
                continue;
            }
            storeValue(spawn, variableName(name->string()), *value);
        }
    } else if (root->isObject()) {
        for (const std::string& key : root->mKeys) {
            const json::Value* value = root->get(key);
            if (value != nullptr) {
                storeValue(spawn, variableName(key), *value);
            }
        }
    }
    return spawn;
}

}
