#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace kestrel::world {

struct BeaconBeamSection {
    int32_t bottom = 0;
    int32_t top = 0;
    uint32_t color = 0xFFFFFF;
};

inline bool beaconBase(std::string_view name)
{
    return name == "minecraft:iron_block" || name == "minecraft:gold_block" || name == "minecraft:emerald_block"
        || name == "minecraft:diamond_block" || name == "minecraft:netherite_block";
}

inline std::optional<uint32_t> beaconGlassColor(std::string_view name)
{
    constexpr std::string_view Colors[] = { "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
        "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black" };
    constexpr uint32_t Rgb[] = { 0xF9FFFE, 0xF9801D, 0xC74EBD, 0x3AB3DA, 0xFED83D, 0x80C71F, 0xF38BAA, 0x474F52,
        0x9D9D97, 0x169C9C, 0x8932B8, 0x3C44AA, 0x835432, 0x5E7C16, 0xB02E26, 0x1D1D21 };
    if (!name.starts_with("minecraft:")) return std::nullopt;
    name.remove_prefix(10);
    constexpr std::string_view Glass = "_stained_glass";
    constexpr std::string_view Pane = "_stained_glass_pane";
    if (name.ends_with(Pane)) name.remove_suffix(Pane.size());
    else if (name.ends_with(Glass)) name.remove_suffix(Glass.size());
    else return std::nullopt;
    for (size_t color = 0; color < std::size(Colors); ++color) {
        if (name == Colors[color]) return Rgb[color];
    }
    return std::nullopt;
}

inline uint32_t mixBeaconColors(uint32_t previous, uint32_t next)
{
    uint32_t mixed = 0;
    for (uint32_t shift : { 0u, 8u, 16u }) {
        mixed |= (((previous >> shift & 255u) + (next >> shift & 255u)) / 2) << shift;
    }
    return mixed;
}

struct BeaconColumnBlock {
    bool blocksBeam = false;
    std::optional<uint32_t> color;
};

template <class Sample>
std::vector<BeaconBeamSection> beaconSections(int32_t bottom, int32_t top, Sample sample)
{
    std::vector<BeaconBeamSection> sections;
    if (top <= bottom || int64_t(top) - bottom > 4096) return sections;
    sections.push_back({ bottom, top, 0xFFFFFF });
    bool colored = false;
    for (int32_t y = bottom + 1; y < top; ++y) {
        const BeaconColumnBlock block = sample(y);
        if (block.blocksBeam) return {};
        if (!block.color) continue;
        const uint32_t color = colored ? mixBeaconColors(sections.back().color, *block.color) : *block.color;
        colored = true;
        if (color == sections.back().color) continue;
        sections.back().top = y;
        if (sections.size() >= 256) return {};
        sections.push_back({ y, top, color });
    }
    return sections;
}

}
