#include "world/BiomeTints.h"

#include "Core/Json/Json.h"
#include "ui/Image.h"
#include "world/PackSource.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>

namespace kestrel::world {

namespace {

enum class TintMap : uint8_t {
    Grass,
    Foliage,
    Birch,
    Evergreen,
    SwampGrass,
    SwampFoliage,
    MangroveSwampFoliage,
    DryFoliage,
    Count,
};

constexpr const char* MapNames[] = { "grass", "foliage", "birch", "evergreen", "swamp_grass", "swamp_foliage", "mangrove_swamp_foliage", "dry_foliage" };

struct BiomeId {
    uint32_t id;
    const char* name;
};

constexpr BiomeId Registry[] = {
    { 0, "ocean" }, { 1, "plains" }, { 2, "desert" }, { 3, "extreme_hills" }, { 4, "forest" }, { 5, "taiga" }, { 6, "swampland" },
    { 7, "river" }, { 8, "hell" }, { 9, "the_end" }, { 10, "legacy_frozen_ocean" }, { 11, "frozen_river" }, { 12, "ice_plains" },
    { 13, "ice_mountains" }, { 14, "mushroom_island" }, { 15, "mushroom_island_shore" }, { 16, "beach" }, { 17, "desert_hills" },
    { 18, "forest_hills" }, { 19, "taiga_hills" }, { 20, "extreme_hills_edge" }, { 21, "jungle" }, { 22, "jungle_hills" },
    { 23, "jungle_edge" }, { 24, "deep_ocean" }, { 25, "stone_beach" }, { 26, "cold_beach" }, { 27, "birch_forest" },
    { 28, "birch_forest_hills" }, { 29, "roofed_forest" }, { 30, "cold_taiga" }, { 31, "cold_taiga_hills" }, { 32, "mega_taiga" },
    { 33, "mega_taiga_hills" }, { 34, "extreme_hills_plus_trees" }, { 35, "savanna" }, { 36, "savanna_plateau" }, { 37, "mesa" },
    { 38, "mesa_plateau_stone" }, { 39, "mesa_plateau" }, { 40, "warm_ocean" }, { 41, "deep_warm_ocean" }, { 42, "lukewarm_ocean" },
    { 43, "deep_lukewarm_ocean" }, { 44, "cold_ocean" }, { 45, "deep_cold_ocean" }, { 46, "frozen_ocean" }, { 47, "deep_frozen_ocean" },
    { 48, "bamboo_jungle" }, { 49, "bamboo_jungle_hills" }, { 129, "sunflower_plains" }, { 130, "desert_mutated" },
    { 131, "extreme_hills_mutated" }, { 132, "flower_forest" }, { 133, "taiga_mutated" }, { 134, "swampland_mutated" },
    { 140, "ice_plains_spikes" }, { 149, "jungle_mutated" }, { 151, "jungle_edge_mutated" }, { 155, "birch_forest_mutated" },
    { 156, "birch_forest_hills_mutated" }, { 157, "roofed_forest_mutated" }, { 158, "cold_taiga_mutated" },
    { 160, "redwood_taiga_mutated" }, { 161, "redwood_taiga_hills_mutated" }, { 162, "extreme_hills_plus_trees_mutated" },
    { 163, "savanna_mutated" }, { 164, "savanna_plateau_mutated" }, { 165, "mesa_bryce" }, { 166, "mesa_plateau_stone_mutated" },
    { 167, "mesa_plateau_mutated" }, { 178, "soulsand_valley" }, { 179, "crimson_forest" }, { 180, "warped_forest" },
    { 181, "basalt_deltas" }, { 182, "jagged_peaks" }, { 183, "frozen_peaks" }, { 184, "snowy_slopes" }, { 185, "grove" },
    { 186, "meadow" }, { 187, "lush_caves" }, { 188, "dripstone_caves" }, { 189, "stony_peaks" }, { 190, "deep_dark" },
    { 191, "mangrove_swamp" }, { 192, "cherry_grove" }, { 193, "pale_garden" }, { 194, "sulfur_caves" },
};

/**
 * Either a direct 0xRRGGBB color or a colormap sampled at the biome climate.
 */
struct TintSource {
    std::optional<uint32_t> direct;
    TintMap map = TintMap::Grass;
};

struct Appearance {
    std::optional<TintSource> grass;
    std::optional<TintSource> foliage;
    std::optional<TintSource> dryFoliage;
    std::optional<uint32_t> water;
    std::string fog;
};

struct Climate {
    float temperature = 0.8f;
    float downfall = 0.4f;
};

std::string bareName(const std::string& identifier)
{
    size_t colon = identifier.find(':');
    return colon == std::string::npos ? identifier : identifier.substr(colon + 1);
}

std::optional<uint32_t> parseDirect(const json::Value& value)
{
    if (value.isString()) {
        std::string text = value.string();
        if (text.size() == 7 && text[0] == '#') {
            return static_cast<uint32_t>(std::strtoul(text.c_str() + 1, nullptr, 16));
        }
        return std::nullopt;
    }
    if (value.isArray() && value.mArray.size() == 3) {
        uint32_t rgb = 0;
        for (const auto& channel : value.mArray) {
            rgb = (rgb << 8) | static_cast<uint32_t>(std::clamp(channel->integer(0), 0, 255));
        }
        return rgb;
    }
    return std::nullopt;
}

std::optional<TintSource> parseSource(const json::Value* value)
{
    if (!value) {
        return std::nullopt;
    }
    if (value->isObject()) {
        const json::Value* map = value->get("color_map");
        if (!map || !map->isString()) {
            return std::nullopt;
        }
        for (size_t i = 0; i < size_t(TintMap::Count); ++i) {
            if (map->string() == MapNames[i]) {
                return TintSource { std::nullopt, static_cast<TintMap>(i) };
            }
        }
        return std::nullopt;
    }
    if (std::optional<uint32_t> rgb = parseDirect(*value)) {
        return TintSource { rgb, TintMap::Grass };
    }
    return std::nullopt;
}

const json::Value* component(const json::Value& components, const char* name, const char* field)
{
    const json::Value* entry = components.get(name);
    return entry ? entry->get(field) : nullptr;
}

std::optional<BiomeFog> parseFog(const json::Value* source)
{
    const auto* start = source ? source->get("fog_start") : nullptr;
    const auto* end = source ? source->get("fog_end") : nullptr;
    const auto* color = source ? source->get("fog_color") : nullptr;
    const auto* mode = source ? source->get("render_distance_type") : nullptr;
    if (!start || !end || !color || !mode) return std::nullopt;
    auto rgb = parseDirect(*color);
    float first = float(start->number(-1)), last = float(end->number(-1));
    std::string type = mode->string();
    if (!rgb || !std::isfinite(first) || !std::isfinite(last) || first < 0 || last < first
        || (type != "fixed" && type != "render")) return std::nullopt;
    return BiomeFog { *rgb, first, last, type == "render" };
}

std::optional<BiomeFog> parseWaterFog(const json::Value* source)
{
    auto fog = parseFog(source);
    const auto* transition = source ? source->get("transition_fog") : nullptr;
    if (!fog || !transition) return fog;
    auto initial = parseFog(transition->get("init_fog"));
    auto number = [&](const char* name) {
        const auto* value = transition->get(name);
        return value ? float(value->number(-1)) : -1.0f;
    };
    float minimum = number("min_percent"), middle = number("mid_percent");
    float midSeconds = number("mid_seconds"), maxSeconds = number("max_seconds");
    if (initial && std::isfinite(minimum) && std::isfinite(middle)
        && std::isfinite(midSeconds) && std::isfinite(maxSeconds)
        && minimum >= 0 && minimum <= middle && middle <= 1
        && midSeconds >= 0 && maxSeconds >= midSeconds) {
        fog->transition = BiomeFogTransition { initial->color, initial->start, initial->end, initial->relative,
            minimum, midSeconds, middle, maxSeconds };
    }
    return fog;
}

}

uint32_t BiomeColors::domain(TintKind kind, FoliageVariant variant) const
{
    switch (kind) {
    case TintKind::Grass:
        return grass;
    case TintKind::Water:
        return water;
    case TintKind::Foliage:
        switch (variant) {
        case FoliageVariant::Birch:
            return birch;
        case FoliageVariant::Evergreen:
            return evergreen;
        case FoliageVariant::Dry:
            return dryFoliage;
        case FoliageVariant::Default:
            break;
        }
        return foliage;
    case TintKind::None:
        break;
    }
    return 0xFFFFFF;
}

void BiomeTints::load(PackSource& resources, PackSource& behaviors)
{
    powderSnow.reset();
    fogs.clear();
    for (const auto& entry : resources.archiveEntries("fogs")) {
        std::string text;
        if (!resources.readArchived("fogs", entry, text)) continue;
        auto document = json::parse(text);
        const auto* settings = document ? document->get("minecraft:fog_settings") : nullptr;
        const auto* description = settings ? settings->get("description") : nullptr;
        const auto* identifier = description ? description->get("identifier") : nullptr;
        const auto* distance = settings ? settings->get("distance") : nullptr;
        if (!identifier || !identifier->isString()) continue;
        auto snow = parseFog(distance ? distance->get("powder_snow") : nullptr);
        if (identifier->string() == "minecraft:fog_powder_snow") {
            powderSnow = snow;
        }
        auto water = parseWaterFog(distance ? distance->get("water") : nullptr);
        auto air = parseFog(distance ? distance->get("air") : nullptr);
        auto lava = parseFog(distance ? distance->get("lava") : nullptr);
        auto resistance = parseFog(distance ? distance->get("lava_resistance") : nullptr);
        auto weather = parseFog(distance ? distance->get("weather") : nullptr);
        if (!air && !water && !lava && !resistance && !weather && !snow) continue;
        fogs.try_emplace(identifier->string(), FogProfiles { air, water, lava, resistance, weather, snow });
    }
    std::array<std::vector<uint8_t>, size_t(TintMap::Count)> maps;
    for (size_t i = 0; i < maps.size(); ++i) {
        std::string encoded;
        std::vector<uint8_t> rgba;
        uint32_t width = 0;
        uint32_t height = 0;
        if (resources.readTexture(std::string("textures/colormap/") + MapNames[i], encoded) && ui::decodeImage(encoded, width, height, rgba) && width == 256 && height == 256) {
            maps[i] = std::move(rgba);
        }
    }

    std::map<std::string, Appearance> appearances;
    for (const std::string& entry : resources.archiveEntries("biomes")) {
        const std::string suffix = ".client_biome.json";
        if (entry.size() <= suffix.size() || entry.compare(entry.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        std::string text;
        if (!resources.readArchived("biomes", entry, text)) {
            continue;
        }
        std::unique_ptr<json::Value> document = json::parse(text);
        const json::Value* biome = document ? document->get("minecraft:client_biome") : nullptr;
        const json::Value* description = biome ? biome->get("description") : nullptr;
        const json::Value* identifier = description ? description->get("identifier") : nullptr;
        const json::Value* components = biome ? biome->get("components") : nullptr;
        if (!identifier || !components) {
            continue;
        }
        Appearance appearance;
        if (const auto* fog = component(*components, "minecraft:fog_appearance", "fog_identifier"))
            appearance.fog = fog->string();
        appearance.grass = parseSource(component(*components, "minecraft:grass_appearance", "color"));
        appearance.foliage = parseSource(component(*components, "minecraft:foliage_appearance", "color"));
        appearance.dryFoliage = parseSource(component(*components, "minecraft:dry_foliage_color", "color"));
        if (const json::Value* surface = component(*components, "minecraft:water_appearance", "surface_color")) {
            appearance.water = parseDirect(*surface);
        }
        appearances.try_emplace(bareName(identifier->string()), appearance);
    }

    std::map<std::string, Climate> climates;
    for (const std::string& entry : behaviors.archiveEntries("biomes")) {
        const std::string suffix = ".biome.json";
        if (entry.size() <= suffix.size() || entry.compare(entry.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        std::string text;
        if (!behaviors.readArchived("biomes", entry, text)) {
            continue;
        }
        std::unique_ptr<json::Value> document = json::parse(text);
        const json::Value* biome = document ? document->get("minecraft:biome") : nullptr;
        const json::Value* description = biome ? biome->get("description") : nullptr;
        const json::Value* identifier = description ? description->get("identifier") : nullptr;
        const json::Value* components = biome ? biome->get("components") : nullptr;
        const json::Value* climate = components ? components->get("minecraft:climate") : nullptr;
        if (!identifier || !climate) {
            continue;
        }
        Climate values;
        if (const json::Value* temperature = climate->get("temperature")) {
            values.temperature = static_cast<float>(temperature->number(0.8));
        }
        if (const json::Value* downfall = climate->get("downfall")) {
            values.downfall = static_cast<float>(downfall->number(0.4));
        }
        climates.try_emplace(bareName(identifier->string()), values);
    }

    auto resolve = [&](const std::optional<TintSource>& source, TintMap defaultMap, const Climate& climate) -> uint32_t {
        TintSource effective = source.value_or(TintSource { std::nullopt, defaultMap });
        if (effective.direct) {
            return *effective.direct;
        }
        const std::vector<uint8_t>& map = maps[size_t(effective.map)];
        if (map.empty()) {
            return 0xFFFFFF;
        }
        float temperature = std::clamp(climate.temperature, 0.0f, 1.0f);
        float humidity = std::clamp(climate.downfall, 0.0f, 1.0f) * temperature;
        size_t x = static_cast<size_t>(std::floor((1.0f - temperature) * 255.0f));
        size_t y = static_cast<size_t>(std::floor((1.0f - humidity) * 255.0f));
        const uint8_t* pixel = map.data() + (y * 256 + x) * 4;
        return (uint32_t(pixel[0]) << 16) | (uint32_t(pixel[1]) << 8) | uint32_t(pixel[2]);
    };
    auto build = [&](const Appearance& appearance, const Climate& climate) {
        BiomeColors colors;
        colors.grass = resolve(appearance.grass, TintMap::Grass, climate);
        colors.foliage = resolve(appearance.foliage, TintMap::Foliage, climate);
        colors.birch = resolve(std::nullopt, TintMap::Birch, climate);
        colors.evergreen = resolve(std::nullopt, TintMap::Evergreen, climate);
        colors.dryFoliage = resolve(appearance.dryFoliage, TintMap::DryFoliage, climate);
        colors.water = appearance.water.value_or(0x44AFF5);
        auto applyFog = [&](const FogProfiles& fog) {
            if (fog.air) colors.airFog = fog.air;
            if (fog.water) {
                colors.waterFog = fog.water->color;
                colors.waterFogStart = fog.water->start;
                colors.waterFogEnd = fog.water->end;
                colors.waterFogRelative = fog.water->relative;
            }
            if (fog.lava) colors.lavaFog = fog.lava;
            if (fog.resistance) colors.lavaResistanceFog = fog.resistance;
        };
        if (auto defaults = fogs.find("minecraft:fog_default"); defaults != fogs.end()) {
            applyFog(defaults->second);
        }
        if (auto fog = fogs.find(appearance.fog); fog != fogs.end()) applyFog(fog->second);
        return colors;
    };

    fallback = build(Appearance {}, Climate {});
    byId.clear();
    for (const BiomeId& biome : Registry) {
        auto appearance = appearances.find(biome.name);
        auto climate = climates.find(biome.name);
        byId[biome.id] = build(appearance != appearances.end() ? appearance->second : Appearance {}, climate != climates.end() ? climate->second : Climate {});
    }
}

const BiomeColors& BiomeTints::colors(uint32_t biomeId) const
{
    auto found = byId.find(biomeId);
    return found == byId.end() ? fallback : found->second;
}

const BiomeFog* BiomeTints::commandFog(const std::vector<std::string>& stack, FogMedium medium) const
{
    // PlayerFog lists pushes oldest first; absent media fall through to earlier pushes.
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        auto entry = fogs.find(*it);
        if (entry == fogs.end()) continue;
        const auto& fog = entry->second;
        const std::optional<BiomeFog>* selected = nullptr;
        switch (medium) {
        case FogMedium::Air: selected = &fog.air; break;
        case FogMedium::Weather: selected = &fog.weather; break;
        case FogMedium::Water: selected = &fog.water; break;
        case FogMedium::Lava: selected = &fog.lava; break;
        case FogMedium::LavaResistance: selected = &fog.resistance; break;
        case FogMedium::PowderSnow: selected = &fog.powderSnow; break;
        }
        if (selected && *selected) return &**selected;
    }
    return nullptr;
}

}
