#pragma once

#include "world/PalettedStorage.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kestrel::world {

inline constexpr size_t MaxStorageCount = 16;

struct BlockUpdate {
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t z = 0;
    uint8_t layer = 0;
    uint32_t runtimeId = 0;
};

class SubChunk {
public:
    static bool decode(const uint8_t* data, size_t size, SubChunk& out, size_t& consumed, std::string& error, const BlockPaletteResolver& resolver = {});

    uint8_t version() const
    {
        return format;
    }

    std::optional<int8_t> yIndex() const
    {
        return index;
    }

    const std::vector<PalettedStorage>& storages() const
    {
        return layers;
    }

    bool empty() const
    {
        return layers.empty();
    }

    uint32_t runtimeId(size_t layer, uint32_t x, uint32_t y, uint32_t z) const;
    bool apply(const std::vector<BlockUpdate>& updates);

private:
    uint8_t format = 9;
    std::optional<int8_t> index;
    std::vector<PalettedStorage> layers;
};

}
