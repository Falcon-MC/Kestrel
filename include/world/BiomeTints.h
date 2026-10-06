#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace kestrel::world {

class PackSource;

enum class TintKind : uint8_t {
    None = 0,
    Grass = 1,
    Foliage = 2,
    Water = 3,
};

enum class FoliageVariant : uint8_t {
    Default = 0,
    Birch = 1,
    Evergreen = 2,
    Dry = 3,
};

struct BiomeFog {
    uint32_t color = 0;
    float start = 0.0f;
    float end = 0.0f;
    bool relative = false;
};

/**
 * Resolved sRGB colors (0xRRGGBB) of one biome for every tint domain.
 */
struct BiomeColors {
    std::optional<BiomeFog> lavaFog;
    std::optional<BiomeFog> lavaResistanceFog;
    uint32_t waterFog = 0x44AFF5;
    float waterFogStart = 0.0f;
    float waterFogEnd = 60.0f;
    bool waterFogRelative = false;
    uint32_t grass = 0xFFFFFF;
    uint32_t foliage = 0xFFFFFF;
    uint32_t birch = 0xFFFFFF;
    uint32_t evergreen = 0xFFFFFF;
    uint32_t dryFoliage = 0xFFFFFF;
    uint32_t water = 0x44AFF5;

    uint32_t domain(TintKind kind, FoliageVariant variant) const;
};

/**
 * Biome palette IDs resolved to tint colors from the client biome appearances,
 * the behavior pack climates and the colormaps of the vanilla packs.
 */
class BiomeTints {
public:
    void load(PackSource& resources, PackSource& behaviors);

    const BiomeColors& colors(uint32_t biomeId) const;

private:
    std::unordered_map<uint32_t, BiomeColors> byId;
    BiomeColors fallback;
};

}
