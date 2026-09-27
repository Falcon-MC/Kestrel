#include "world/BlockRegistry.h"

#include "BlockDefinitionsNbt.h"
#include "BlockPaletteNbt.h"
#include "Core/NBT/NbtIo.h"
#include "Core/Utility/ReadOnlyBinaryStream.h"
#include "Protocol/BlockStateHasher.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace kestrel::world {

namespace {

bool gunzip(const unsigned char* data, size_t size, std::string& out)
{
    z_stream stream {};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        return false;
    }
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);

    char buffer[65536];
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer);
        stream.avail_out = sizeof(buffer);
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
            inflateEnd(&stream);
            return false;
        }
        out.append(buffer, sizeof(buffer) - stream.avail_out);
    }
    inflateEnd(&stream);
    return status == Z_STREAM_END;
}

void collectDefinitionNames(const Tag& node, const std::unordered_set<std::string>& paletteNames, std::unordered_set<std::string>& out)
{
    if (node.getType() == Tag::Type::Compound) {
        const std::vector<std::string>& keys = node.getKeys();
        const std::vector<Tag>& values = node.getValues();
        for (size_t i = 0; i < keys.size(); ++i) {
            if ((keys[i] == "name" || keys[i] == "identifier") && values[i].getType() == Tag::Type::String && paletteNames.contains(values[i].asString())) {
                out.insert(values[i].asString());
            }
            collectDefinitionNames(values[i], paletteNames, out);
        }
    } else if (node.getType() == Tag::Type::List) {
        for (const Tag& value : node.getList()) {
            collectDefinitionNames(value, paletteNames, out);
        }
    }
}

}

uint64_t BlockRegistry::nameHash(const std::string& name)
{
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : name) {
        hash *= 1099511628211ull;
        hash ^= c;
    }
    return hash;
}

bool BlockRegistry::load(std::string& error)
{
    std::string decompressed;
    if (!gunzip(KestrelBlockPaletteData::kBlockPaletteNbt, KestrelBlockPaletteData::kBlockPaletteNbtSize, decompressed)) {
        error = "could not decompress the block palette";
        return false;
    }

    Tag root;
    try {
        ReadOnlyBinaryStream stream(decompressed);
        EncodingSettings settings;
        settings.mMaxListSize = 65536;
        stream.setEncodingSettings(settings);
        root = NbtIo::readTag(stream, NbtVariant::BigEndian);
    } catch (const std::exception& exception) {
        error = std::string("could not parse the block palette: ") + exception.what();
        return false;
    }

    const Tag* blocks = root.get("blocks");
    if (!blocks) {
        error = "block palette has no blocks list";
        return false;
    }

    std::vector<BlockRecord> ordered;
    ordered.reserve(blocks->getList().size());
    for (const Tag& entry : blocks->getList()) {
        const Tag* name = entry.get("name");
        const Tag* states = entry.get("states");
        if (!name || !states) {
            continue;
        }
        BlockRecord record;
        record.name = name->asString();
        record.states = *states;
        record.networkHash = static_cast<uint32_t>(BlockStateHasher::hash(record.name, record.states));
        ordered.push_back(std::move(record));
    }

    std::stable_sort(ordered.begin(), ordered.end(), [](const BlockRecord& left, const BlockRecord& right) {
        return nameHash(left.name) < nameHash(right.name);
    });

    entries = std::move(ordered);

    std::unordered_set<std::string> paletteNames;
    for (const BlockRecord& record : entries) {
        paletteNames.insert(record.name);
    }
    std::string definitionsData;
    if (gunzip(KestrelBlockDefinitionData::kBlockDefinitionsNbt, KestrelBlockDefinitionData::kBlockDefinitionsNbtSize, definitionsData)) {
        try {
            ReadOnlyBinaryStream stream(definitionsData);
            EncodingSettings settings;
            settings.mMaxListSize = 65536;
            stream.setEncodingSettings(settings);
            Tag definitions = NbtIo::readTag(stream, NbtVariant::BigEndian);
            collectDefinitionNames(definitions, paletteNames, dataDriven);
        } catch (const std::exception&) {
            dataDriven.clear();
        }
    }

    byHash.clear();
    byHash.reserve(entries.size());
    for (uint32_t i = 0; i < entries.size(); ++i) {
        byHash.emplace(entries[i].networkHash, i);
    }
    return true;
}

int32_t BlockRegistry::resolve(uint32_t networkValue, bool hashed) const
{
    if (hashed) {
        auto found = byHash.find(networkValue);
        return found == byHash.end() ? -1 : static_cast<int32_t>(found->second);
    }
    return networkValue < entries.size() ? static_cast<int32_t>(networkValue) : -1;
}

}
