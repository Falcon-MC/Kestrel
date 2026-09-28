#pragma once

#include "world/ByteReader.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kestrel::world {

inline constexpr size_t BlocksPerSubChunk = 16 * 16 * 16;
inline constexpr uint32_t ImplicitAir = 0xFFFFFFFFu;

inline constexpr size_t linearIndex(uint32_t x, uint32_t y, uint32_t z)
{
    return (size_t(x) << 8) | (size_t(z) << 4) | size_t(y);
}

using BlockPaletteResolver = std::function<std::optional<uint32_t>(const Tag&)>;

class PalettedStorage {
public:
    static PalettedStorage uniform(uint32_t runtimeId);
    static bool decode(ByteReader& reader, PalettedStorage& out, std::string& error, const BlockPaletteResolver& resolver = {});
    static bool decodeWithHeader(ByteReader& reader, uint8_t header, PalettedStorage& out, std::string& error, const BlockPaletteResolver& resolver = {}, bool integerPalette = false);

    uint32_t runtimeIdAt(size_t linear) const;

    uint32_t runtimeId(uint32_t x, uint32_t y, uint32_t z) const
    {
        return runtimeIdAt(linearIndex(x, y, z));
    }

    uint8_t bitsPerIndex() const
    {
        return bits;
    }

    const std::vector<uint32_t>& palette() const
    {
        return values;
    }

    bool isUniform() const
    {
        return bits == 0;
    }

    bool containsOnly(uint32_t runtimeId) const;
    void apply(const std::vector<std::pair<size_t, uint32_t>>& updates);
    size_t paletteIndex(size_t linear) const;

private:
    uint8_t bits = 0;
    std::vector<uint32_t> words;
    std::vector<uint32_t> values;
};

}
