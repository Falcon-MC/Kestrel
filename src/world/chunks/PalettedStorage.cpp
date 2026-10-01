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
    storage.uses.push_back(static_cast<uint16_t>(BlocksPerSubChunk));
    return storage;
}

bool PalettedStorage::decode(ByteReader& reader, PalettedStorage& out, std::string& error, const BlockPaletteResolver& resolver)
{
    uint8_t header = 0;
    if (!reader.readByte(header, error, "palette header")) {
        return false;
    }
    return decodeWithHeader(reader, header, out, error, resolver);
}

bool PalettedStorage::decodeWithHeader(ByteReader& reader, uint8_t header, PalettedStorage& out, std::string& error, const BlockPaletteResolver& resolver, bool integerPalette)
{
    bool persistent = (header & 1) == 0;

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
    auto readPalette = [&](ByteReader& source, bool networkNbt) {
        paletteSize = 1;
        if (bits != 0) {
            int32_t count = 0;
            if (persistent && !networkNbt) {
                uint32_t value = 0;
                if (!source.readWords(&value, 1, error, "palette length")) return false;
                if (value > BlocksPerSubChunk) { error = "persistent palette length exceeds 4096"; return false; }
                count = static_cast<int32_t>(value);
            } else if (!source.readVarInt(count, error, "palette length")) return false;
            size_t maxSize = std::min(size_t(1) << bits, BlocksPerSubChunk);
            if (persistent && (count <= 0 || size_t(count) > maxSize)) { error = "invalid palette length: " + std::to_string(count); return false; }
            paletteSize = std::clamp(size_t(std::max(count, int32_t(1))), size_t(1), maxSize);
        }
        storage.values.resize(paletteSize);
        for (size_t i = 0; i < paletteSize; ++i) {
            if (persistent && integerPalette) {
                int32_t value = 0;
                if (networkNbt) {
                    if (!source.readVarInt(value, error, "palette entry")) return false;
                } else {
                    uint32_t word = 0;
                    if (!source.readWords(&word, 1, error, "palette entry")) return false;
                    value = static_cast<int32_t>(word);
                }
                storage.values[i] = static_cast<uint32_t>(value);
            } else if (persistent) {
                Tag state;
                if (!source.readTag(state, networkNbt, error)) return false;
                auto value = resolver ? resolver(state) : std::nullopt;
                if (!value) {
                    const Tag* name = state.get("name");
                    error = "unresolved persistent block palette state " + (name && name->getType() == Tag::Type::String ? name->asString() : std::string("<no name>"));
                    return false;
                }
                storage.values[i] = *value;
            } else {
                int32_t value = 0;
                if (!source.readVarInt(value, error, "palette entry")) return false;
                storage.values[i] = static_cast<uint32_t>(value);
            }
        }
        return true;
    };
    ByteReader paletteStart = reader;
    if (!readPalette(reader, true)) {
        if (!persistent) return false;
        std::string networkError = error;
        reader = paletteStart;
        if (!readPalette(reader, false)) {
            error = networkError + " (disk format: " + error + ")";
            return false;
        }
        error.clear();
    }

    storage.uses.assign(paletteSize, 0);
    for (size_t linear = 0; linear < BlocksPerSubChunk; ++linear) {
        size_t paletteIndex = storage.paletteIndex(linear);
        if (paletteIndex >= paletteSize) {
            if (persistent) {
                error = "palette index out of bounds";
                return false;
            }
            writeIndex(storage.words, storage.bits, linear, 0);
            paletteIndex = 0;
        }
        ++storage.uses[paletteIndex];
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

bool PalettedStorage::apply(const std::vector<std::pair<size_t, uint32_t>>& updates)
{
    if (updates.empty()) {
        return false;
    }
    if (updates.size() == 1 && values.size() < BlocksPerSubChunk) {
        auto [linear, runtimeId] = updates.front();
        if (linear >= BlocksPerSubChunk || runtimeIdAt(linear) == runtimeId) {
            return false;
        }
        size_t oldIndex = paletteIndex(linear);
        auto found = std::find(values.begin(), values.end(), runtimeId);
        size_t newIndex = size_t(found - values.begin());
        if (found == values.end()) {
            values.push_back(runtimeId);
            uses.push_back(0);
        }
        --uses[oldIndex];
        ++uses[newIndex];
        if (uses[newIndex] == BlocksPerSubChunk) {
            bits = 0;
            words.clear();
            values = { runtimeId };
            uses = { static_cast<uint16_t>(BlocksPerSubChunk) };
            return true;
        }
        uint8_t newBits = bitsForPaletteSize(values.size());
        if (newBits != bits) {
            std::vector<uint32_t> replacement(wordCount(newBits), 0);
            for (size_t position = 0; position < BlocksPerSubChunk; ++position) {
                writeIndex(replacement, newBits, position, static_cast<uint32_t>(paletteIndex(position)));
            }
            words = std::move(replacement);
            bits = newBits;
        }
        writeIndex(words, bits, linear, static_cast<uint32_t>(newIndex));
        return true;
    }

    std::array<uint32_t, BlocksPerSubChunk> finalValues {};
    std::array<bool, BlocksPerSubChunk> touched {};
    for (const auto& [linear, runtimeId] : updates) {
        if (linear < BlocksPerSubChunk) {
            touched[linear] = true;
            finalValues[linear] = runtimeId;
        }
    }
    bool changed = false;
    for (size_t linear = 0; linear < BlocksPerSubChunk; ++linear) {
        if (touched[linear] && runtimeIdAt(linear) != finalValues[linear]) {
            changed = true;
            break;
        }
    }
    if (!changed) {
        return false;
    }

    std::vector<uint32_t> palette;
    std::unordered_map<uint32_t, uint32_t> lookup;
    std::vector<uint32_t> oldRemap(values.size());
    std::vector<bool> used(values.size(), false);
    for (size_t linear = 0; linear < BlocksPerSubChunk; ++linear) {
        if (!touched[linear]) {
            used[paletteIndex(linear)] = true;
        }
    }
    auto indexFor = [&](uint32_t value) {
        auto [entry, inserted] = lookup.emplace(value, static_cast<uint32_t>(palette.size()));
        if (inserted) palette.push_back(value);
        return entry->second;
    };
    for (size_t i = 0; i < values.size(); ++i) {
        if (used[i]) oldRemap[i] = indexFor(values[i]);
    }
    for (size_t linear = 0; linear < BlocksPerSubChunk; ++linear) {
        if (touched[linear]) finalValues[linear] = indexFor(finalValues[linear]);
    }
    uint8_t newBits = bitsForPaletteSize(palette.size());
    std::vector<uint32_t> newWords(wordCount(newBits), 0);
    std::vector<uint16_t> newUses(palette.size(), 0);
    for (size_t linear = 0; linear < BlocksPerSubChunk; ++linear) {
        uint32_t next = touched[linear] ? finalValues[linear] : oldRemap[paletteIndex(linear)];
        ++newUses[next];
        writeIndex(newWords, newBits, linear, next);
    }
    bits = newBits;
    words = std::move(newWords);
    values = std::move(palette);
    uses = std::move(newUses);
    return true;
}

}
