#include "world/PalettedStorage.h"

#include <algorithm>
#include <array>
#include <unordered_map>

namespace kestrel::world {

namespace {

constexpr std::array<uint8_t, 9> SupportedBits { 0, 1, 2, 3, 4, 5, 6, 8, 16 };

size_t wordCount(uint8_t bits)
{
    if (bits == 0) {
        return 0;
    }
    size_t valuesPerWord = 32 / bits;
    return (BlocksPerSubChunk + valuesPerWord - 1) / valuesPerWord;
}

uint8_t bitsForPaletteSize(size_t size)
{
    for (uint8_t bits : SupportedBits) {
        if (bits == 16 || size <= (size_t(1) << bits)) {
            return bits;
        }
    }
    return 16;
}

void writeIndex(std::vector<uint32_t>& words, uint8_t bits, size_t linear, uint32_t index)
{
    if (bits == 0) {
        return;
    }
    size_t valuesPerWord = 32 / bits;
    uint32_t& word = words[linear / valuesPerWord];
    uint32_t shift = static_cast<uint32_t>((linear % valuesPerWord) * bits);
    uint32_t mask = ((1u << bits) - 1u) << shift;
    word = (word & ~mask) | ((index << shift) & mask);
}

}

PalettedStorage PalettedStorage::uniform(uint32_t runtimeId)
{
    PalettedStorage storage;
    storage.values.push_back(runtimeId);
    return storage;
}

bool PalettedStorage::decode(ByteReader& reader, PalettedStorage& out, std::string& error)
{
    uint8_t header = 0;
    if (!reader.readByte(header, error, "palette header")) {
        return false;
    }
    if ((header & 1) == 0) {
        error = "disk palette found in network sub-chunk data";
        return false;
    }

    uint8_t bits = header >> 1;
    if (std::find(SupportedBits.begin(), SupportedBits.end(), bits) == SupportedBits.end()) {
        error = "unsupported bits per index: " + std::to_string(bits);
        return false;
    }

    PalettedStorage storage;
    storage.bits = bits;
    storage.words.resize(wordCount(bits));
    if (!reader.readWords(storage.words.data(), storage.words.size(), error, "packed index words")) {
        return false;
    }

    size_t paletteSize = 1;
    if (bits != 0) {
        int32_t count = 0;
        if (!reader.readVarInt(count, error, "palette length")) {
            return false;
        }
        size_t maxSize = std::min(size_t(1) << bits, BlocksPerSubChunk);
        if (count <= 0 || size_t(count) > maxSize) {
            error = "invalid palette length: " + std::to_string(count);
            return false;
        }
        paletteSize = size_t(count);
    }

    storage.values.resize(paletteSize);
    for (size_t i = 0; i < paletteSize; ++i) {
        int32_t value = 0;
        if (!reader.readVarInt(value, error, "palette entry")) {
            return false;
        }
        storage.values[i] = static_cast<uint32_t>(value);
    }

    for (size_t linear = 0; linear < BlocksPerSubChunk && bits != 0; ++linear) {
        if (storage.paletteIndex(linear) >= paletteSize) {
            error = "palette index out of bounds";
            return false;
        }
    }

    out = std::move(storage);
    return true;
}

size_t PalettedStorage::paletteIndex(size_t linear) const
{
    if (bits == 0) {
        return 0;
    }
    size_t valuesPerWord = 32 / bits;
    uint32_t word = words[linear / valuesPerWord];
    uint32_t shift = static_cast<uint32_t>((linear % valuesPerWord) * bits);
    return (word >> shift) & ((1u << bits) - 1u);
}

uint32_t PalettedStorage::runtimeIdAt(size_t linear) const
{
    return values[paletteIndex(linear)];
}

bool PalettedStorage::containsOnly(uint32_t runtimeId) const
{
    if (bits == 0) {
        return values.front() == runtimeId;
    }
    for (size_t linear = 0; linear < BlocksPerSubChunk; ++linear) {
        if (runtimeIdAt(linear) != runtimeId) {
            return false;
        }
    }
    return true;
}

void PalettedStorage::apply(const std::vector<std::pair<size_t, uint32_t>>& updates)
{
    std::unordered_map<size_t, uint32_t> finalValues;
    for (const auto& [linear, runtimeId] : updates) {
        finalValues[linear] = runtimeId;
    }

    std::vector<uint32_t> resolved(BlocksPerSubChunk);
    bool changed = false;
    for (size_t linear = 0; linear < BlocksPerSubChunk; ++linear) {
        uint32_t current = runtimeIdAt(linear);
        auto it = finalValues.find(linear);
        uint32_t value = it != finalValues.end() ? it->second : current;
        changed |= value != current;
        resolved[linear] = value;
    }
    if (!changed) {
        return;
    }

    std::vector<uint32_t> palette;
    std::unordered_map<uint32_t, uint32_t> indices;
    for (uint32_t value : resolved) {
        if (indices.emplace(value, static_cast<uint32_t>(palette.size())).second) {
            palette.push_back(value);
        }
    }

    uint8_t newBits = palette.size() == 1 ? 0 : bitsForPaletteSize(palette.size());
    std::vector<uint32_t> newWords(wordCount(newBits), 0);
    for (size_t linear = 0; linear < BlocksPerSubChunk; ++linear) {
        writeIndex(newWords, newBits, linear, indices[resolved[linear]]);
    }

    bits = newBits;
    words = std::move(newWords);
    values = std::move(palette);
}

}
