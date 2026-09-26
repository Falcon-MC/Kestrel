#include "world/SubChunk.h"

#include <array>

namespace kestrel::world {

bool SubChunk::decode(const uint8_t* data, size_t size, SubChunk& out, size_t& consumed, std::string& error)
{
    ByteReader reader(data, size);
    uint8_t version = 0;
    if (!reader.readByte(version, error, "sub-chunk version")) {
        return false;
    }

    SubChunk subChunk;
    subChunk.format = version;
    size_t storageCount = 0;
    switch (version) {
    case 1:
        storageCount = 1;
        break;
    case 8:
    case 9: {
        uint8_t count = 0;
        if (!reader.readByte(count, error, "storage count")) {
            return false;
        }
        storageCount = count;
        if (version == 9) {
            uint8_t y = 0;
            if (!reader.readByte(y, error, "sub-chunk Y index")) {
                return false;
            }
            subChunk.index = static_cast<int8_t>(y);
        }
        break;
    }
    default:
        error = "unsupported sub-chunk version " + std::to_string(version);
        return false;
    }

    if (storageCount > MaxStorageCount) {
        error = "too many storages in sub-chunk: " + std::to_string(storageCount);
        return false;
    }

    subChunk.layers.resize(storageCount);
    for (PalettedStorage& storage : subChunk.layers) {
        if (!PalettedStorage::decode(reader, storage, error)) {
            return false;
        }
    }

    consumed = reader.position();
    out = std::move(subChunk);
    return true;
}

uint32_t SubChunk::runtimeId(size_t layer, uint32_t x, uint32_t y, uint32_t z) const
{
    if (layer >= layers.size()) {
        return ImplicitAir;
    }
    return layers[layer].runtimeId(x, y, z);
}

void SubChunk::apply(const std::vector<BlockUpdate>& updates)
{
    std::array<std::vector<std::pair<size_t, uint32_t>>, MaxStorageCount> byLayer;
    for (const BlockUpdate& update : updates) {
        if (update.layer >= MaxStorageCount) {
            continue;
        }
        while (layers.size() <= update.layer) {
            layers.push_back(PalettedStorage::uniform(ImplicitAir));
        }
        byLayer[update.layer].emplace_back(linearIndex(update.x, update.y, update.z), update.runtimeId);
    }
    for (size_t layer = 0; layer < byLayer.size(); ++layer) {
        if (!byLayer[layer].empty()) {
            layers[layer].apply(byLayer[layer]);
        }
    }
    while (!layers.empty() && layers.back().containsOnly(ImplicitAir)) {
        layers.pop_back();
    }
}

}
