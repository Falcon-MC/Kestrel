#include "audio/SoundLibrary.h"

#include "Core/Json/Json.h"
#include "client/DebugLog.h"
#include "world/PackSource.h"

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#include <algorithm>
#include <cstring>

namespace kestrel::audio {

namespace {

constexpr uint32_t FsbHeaderSize = 0x3C;
constexpr uint32_t FsbLegacyHeaderSize = 0x40;
constexpr uint32_t FsbModePcm8 = 1;
constexpr uint32_t FsbModePcm16 = 2;
constexpr uint32_t FsbModeFadpcm = 16;
constexpr uint32_t FadpcmFrameSize = 0x8C;
constexpr uint32_t FadpcmFrameSamples = 256;

constexpr int16_t FadpcmCoefficients[8][2] = {
    { 0, 0 },
    { 60, 0 },
    { 122, 60 },
    { 115, 52 },
    { 98, 55 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
};

uint32_t readU32(const std::string& data, size_t offset)
{
    uint32_t value = 0;
    std::memcpy(&value, data.data() + offset, 4);
    return value;
}

uint64_t readU64(const std::string& data, size_t offset)
{
    uint64_t value = 0;
    std::memcpy(&value, data.data() + offset, 8);
    return value;
}

int16_t readS16(const uint8_t* data)
{
    int16_t value = 0;
    std::memcpy(&value, data, 2);
    return value;
}

uint32_t readU32(const uint8_t* data)
{
    uint32_t value = 0;
    std::memcpy(&value, data, 4);
    return value;
}

std::string stripComments(const std::string& source)
{
    std::string out;
    out.reserve(source.size());
    bool inString = false;
    for (size_t i = 0; i < source.size(); ++i) {
        char c = source[i];
        if (inString) {
            out += c;
            if (c == '\\' && i + 1 < source.size()) {
                out += source[++i];
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            out += c;
        } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') {
                ++i;
            }
            out += '\n';
        } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            i += 2;
            while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/')) {
                ++i;
            }
            ++i;
        } else {
            out += c;
        }
    }
    return out;
}

std::unique_ptr<json::Value> parseJson(const std::string& text)
{
    std::unique_ptr<json::Value> root = json::parse(stripComments(text));
    if (!root || !root->isObject()) {
        return nullptr;
    }
    return root;
}

float numberOr(const json::Value* value, float fallback)
{
    return value && value->isNumber() ? static_cast<float>(value->mNumber) : fallback;
}

void rangeOf(const json::Value* value, float& low, float& high)
{
    if (!value) {
        return;
    }
    if (value->isNumber()) {
        low = high = static_cast<float>(value->mNumber);
    } else if (value->isArray() && value->mArray.size() >= 2 && value->mArray[0]->isNumber() && value->mArray[1]->isNumber()) {
        low = static_cast<float>(value->mArray[0]->mNumber);
        high = static_cast<float>(value->mArray[1]->mNumber);
    }
}

SoundCategory categoryOf(const std::string& name)
{
    if (name == "music") {
        return SoundCategory::Music;
    }
    if (name == "ambient") {
        return SoundCategory::Ambient;
    }
    if (name == "weather") {
        return SoundCategory::Weather;
    }
    if (name == "block" || name == "bottle" || name == "bucket") {
        return SoundCategory::Block;
    }
    if (name == "hostile") {
        return SoundCategory::Hostile;
    }
    if (name == "player") {
        return SoundCategory::Player;
    }
    if (name == "record") {
        return SoundCategory::Record;
    }
    if (name == "ui") {
        return SoundCategory::Ui;
    }
    return SoundCategory::Neutral;
}

std::string withoutNamespace(const std::string& name)
{
    size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

EventSound eventSoundOf(const json::Value& value, float groupVolume, float groupPitchMin, float groupPitchMax)
{
    EventSound sound;
    sound.volumeMin = sound.volumeMax = groupVolume;
    sound.pitchMin = groupPitchMin;
    sound.pitchMax = groupPitchMax;
    if (value.isString()) {
        sound.sound = value.mString;
        return sound;
    }
    if (!value.isObject()) {
        return sound;
    }
    const json::Value* name = value.get("sound");
    if (!name) {
        name = value.get("sounds");
    }
    if (name && name->isString()) {
        sound.sound = name->mString;
    }
    float volumeLow = 1.0f;
    float volumeHigh = 1.0f;
    rangeOf(value.get("volume"), volumeLow, volumeHigh);
    float pitchLow = 1.0f;
    float pitchHigh = 1.0f;
    rangeOf(value.get("pitch"), pitchLow, pitchHigh);
    sound.volumeMin = volumeLow * groupVolume;
    sound.volumeMax = volumeHigh * groupVolume;
    sound.pitchMin = pitchLow * groupPitchMin;
    sound.pitchMax = pitchHigh * groupPitchMax;
    return sound;
}

void decodeFadpcm(const uint8_t* frame, float* out, uint32_t stride)
{
    uint32_t coefficients = readU32(frame);
    uint32_t shifts = readU32(frame + 4);
    int32_t history1 = readS16(frame + 8);
    int32_t history2 = readS16(frame + 10);
    uint32_t written = 0;
    for (int group = 0; group < 8; ++group) {
        int index = static_cast<int>((coefficients >> (group * 4)) & 0x0F) % 0x07;
        int shift = 0x16 - static_cast<int>((shifts >> (group * 4)) & 0x0F);
        int32_t coefficient1 = FadpcmCoefficients[index][0];
        int32_t coefficient2 = FadpcmCoefficients[index][1];
        for (int word = 0; word < 4; ++word) {
            uint32_t nibbles = readU32(frame + 0x0C + 0x10 * group + 0x04 * word);
            for (int nibble = 0; nibble < 8; ++nibble) {
                int32_t sample = static_cast<int32_t>(((nibbles >> (nibble * 4)) & 0x0F) << 28) >> shift;
                sample = (sample - history2 * coefficient2 + history1 * coefficient1) >> 6;
                sample = std::clamp(sample, -32768, 32767);
                out[written * stride] = static_cast<float>(sample) / 32768.0f;
                ++written;
                history2 = history1;
                history1 = sample;
            }
        }
    }
}

}

/**
 * Decodes the first sample of an FMOD sound bank (FSB5): FMOD ADPCM, 8 or
 * 16 bit PCM.
 */
bool decodeFsb(const std::string& data, PcmBuffer& out)
{
    if (data.size() < FsbLegacyHeaderSize || data.compare(0, 4, "FSB5") != 0) {
        return false;
    }
    uint32_t version = readU32(data, 4);
    uint32_t sampleCount = readU32(data, 8);
    uint32_t sampleHeaders = readU32(data, 12);
    uint32_t nameTable = readU32(data, 16);
    uint32_t dataSize = readU32(data, 20);
    uint32_t mode = readU32(data, 24);
    uint32_t headerSize = version == 0 ? FsbLegacyHeaderSize : FsbHeaderSize;
    if (sampleCount == 0 || data.size() < size_t(headerSize) + sampleHeaders) {
        return false;
    }

    size_t cursor = headerSize;
    uint64_t raw = readU64(data, cursor);
    cursor += 8;
    static constexpr uint32_t Frequencies[] = { 4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000 };
    static constexpr uint32_t ChannelCounts[] = { 1, 2, 6, 8 };
    uint32_t frequencyIndex = static_cast<uint32_t>((raw >> 1) & 0x0F);
    out.sampleRate = frequencyIndex < std::size(Frequencies) ? Frequencies[frequencyIndex] : 44100;
    out.channels = ChannelCounts[(raw >> 5) & 0x03];
    uint64_t sampleOffset = ((raw >> 7) & 0x07FFFFFF) * 32;
    uint64_t frames = (raw >> 34) & 0x3FFFFFFF;
    bool more = (raw & 1) != 0;
    while (more && cursor + 4 <= data.size()) {
        uint32_t chunk = readU32(data, cursor);
        cursor += 4;
        more = (chunk & 1) != 0;
        uint32_t size = (chunk >> 1) & 0x00FFFFFF;
        uint32_t type = (chunk >> 25) & 0x7F;
        if (cursor + size > data.size()) {
            return false;
        }
        if (type == 1 && size >= 1) {
            out.channels = static_cast<uint8_t>(data[cursor]);
        } else if (type == 2 && size >= 4) {
            out.sampleRate = readU32(data, cursor);
        }
        cursor += size;
    }

    size_t start = size_t(headerSize) + sampleHeaders + nameTable + sampleOffset;
    size_t end = std::min(data.size(), size_t(headerSize) + sampleHeaders + nameTable + dataSize);
    if (start >= end || out.channels == 0) {
        return false;
    }
    if (sampleCount > 1) {
        uint64_t nextRaw = readU64(data, cursor);
        size_t nextOffset = size_t(headerSize) + sampleHeaders + nameTable + ((nextRaw >> 7) & 0x07FFFFFF) * 32;
        if (nextOffset > start && nextOffset < end) {
            end = nextOffset;
        }
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data()) + start;
    size_t length = end - start;

    if (mode == FsbModeFadpcm) {
        size_t frameCount = length / (size_t(FadpcmFrameSize) * out.channels);
        out.samples.assign(frameCount * FadpcmFrameSamples * out.channels, 0.0f);
        for (size_t frame = 0; frame < frameCount; ++frame) {
            for (uint32_t channel = 0; channel < out.channels; ++channel) {
                const uint8_t* source = bytes + (frame * out.channels + channel) * FadpcmFrameSize;
                decodeFadpcm(source, out.samples.data() + frame * FadpcmFrameSamples * out.channels + channel, out.channels);
            }
        }
    } else if (mode == FsbModePcm16) {
        size_t count = length / 2;
        out.samples.resize(count);
        for (size_t i = 0; i < count; ++i) {
            out.samples[i] = static_cast<float>(readS16(bytes + i * 2)) / 32768.0f;
        }
    } else if (mode == FsbModePcm8) {
        out.samples.resize(length);
        for (size_t i = 0; i < length; ++i) {
            out.samples[i] = (static_cast<float>(bytes[i]) - 128.0f) / 128.0f;
        }
    } else {
        return false;
    }
    size_t wanted = size_t(frames) * out.channels;
    if (frames > 0 && wanted < out.samples.size()) {
        out.samples.resize(wanted);
    }
    return !out.samples.empty();
}

/**
 * Decodes a RIFF wave file holding 8, 16 or 32 bit PCM or 32 bit float
 * samples.
 */
bool decodeWav(const std::string& data, PcmBuffer& out)
{
    if (data.size() < 12 || data.compare(0, 4, "RIFF") != 0 || data.compare(8, 4, "WAVE") != 0) {
        return false;
    }
    uint16_t format = 0;
    uint16_t bits = 0;
    size_t cursor = 12;
    while (cursor + 8 <= data.size()) {
        std::string id = data.substr(cursor, 4);
        uint32_t size = readU32(data, cursor + 4);
        size_t body = cursor + 8;
        if (body + size > data.size()) {
            size = static_cast<uint32_t>(data.size() - body);
        }
        if (id == "fmt " && size >= 16) {
            std::memcpy(&format, data.data() + body, 2);
            uint16_t channels = 0;
            std::memcpy(&channels, data.data() + body + 2, 2);
            out.channels = channels;
            out.sampleRate = readU32(data, body + 4);
            std::memcpy(&bits, data.data() + body + 14, 2);
        } else if (id == "data" && out.channels > 0) {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data()) + body;
            if (format == 3 && bits == 32) {
                out.samples.resize(size / 4);
                std::memcpy(out.samples.data(), bytes, out.samples.size() * 4);
            } else if (bits == 16) {
                out.samples.resize(size / 2);
                for (size_t i = 0; i < out.samples.size(); ++i) {
                    out.samples[i] = static_cast<float>(readS16(bytes + i * 2)) / 32768.0f;
                }
            } else if (bits == 8) {
                out.samples.resize(size);
                for (size_t i = 0; i < size; ++i) {
                    out.samples[i] = (static_cast<float>(bytes[i]) - 128.0f) / 128.0f;
                }
            } else if (bits == 32) {
                out.samples.resize(size / 4);
                for (size_t i = 0; i < out.samples.size(); ++i) {
                    int32_t value = 0;
                    std::memcpy(&value, bytes + i * 4, 4);
                    out.samples[i] = static_cast<float>(value) / 2147483648.0f;
                }
            }
            return !out.samples.empty();
        }
        cursor = body + size + (size & 1);
    }
    return false;
}

bool decodeOgg(const std::string& data, PcmBuffer& out)
{
    int channels = 0;
    int rate = 0;
    short* decoded = nullptr;
    int frames = stb_vorbis_decode_memory(reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()), &channels, &rate, &decoded);
    if (frames <= 0 || !decoded) {
        return false;
    }
    out.channels = static_cast<uint32_t>(channels);
    out.sampleRate = static_cast<uint32_t>(rate);
    out.samples.resize(size_t(frames) * size_t(channels));
    for (size_t i = 0; i < out.samples.size(); ++i) {
        out.samples[i] = static_cast<float>(decoded[i]) / 32768.0f;
    }
    free(decoded);
    return true;
}

SoundLibrary::SoundLibrary(std::shared_ptr<world::PackSource> vanillaPack, std::shared_ptr<world::PackSource> music, std::vector<std::shared_ptr<const world::PackFiles>> serverPacks)
    : vanilla(std::move(vanillaPack))
    , musicPack(std::move(music))
    , overlays(std::move(serverPacks))
{
    std::string archived;
    if (vanilla) {
        std::vector<std::string> layers = vanilla->readArchivedLayers("sounds", "sound_definitions.json");
        for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
            loadDefinitions(*layer);
        }
        layers = vanilla->readArchivedLayers("sounds", "music_definitions.json");
        for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
            loadMusic(*layer);
        }
        layers = vanilla->readTextLayers("sounds/sound_definitions.json");
        for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
            loadDefinitions(*layer);
        }
        layers = vanilla->readTextLayers("sounds.json");
        for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
            loadEvents(*layer);
        }
        layers = vanilla->readTextLayers("blocks.json");
        for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
            loadBlocks(*layer);
        }
    }
    if (musicPack) {
        if (musicPack->readArchived("sounds", "sound_definitions.json", archived)) {
            loadDefinitions(archived);
        }
        std::string text;
        if (musicPack->readText("sounds/sound_definitions.json", text)) {
            loadDefinitions(text);
        }
    }
    for (auto pack = overlays.rbegin(); pack != overlays.rend(); ++pack) {
        if (const std::string* text = (*pack)->find("sounds/sound_definitions.json")) {
            loadDefinitions(*text);
        }
        if (const std::string* text = (*pack)->find("sounds/music_definitions.json")) {
            loadMusic(*text);
        }
        if (const std::string* text = (*pack)->find("sounds.json")) {
            loadEvents(*text);
        }
        if (const std::string* text = (*pack)->find("blocks.json")) {
            loadBlocks(*text);
        }
    }
}

void SoundLibrary::loadDefinitions(const std::string& text)
{
    std::unique_ptr<json::Value> root = parseJson(text);
    if (!root) {
        return;
    }
    const json::Value* table = root->get("sound_definitions");
    if (!table || !table->isObject()) {
        table = root.get();
    }
    for (const auto& [name, value] : table->mObject) {
        if (!value->isObject()) {
            continue;
        }
        SoundDefinition definition;
        if (const json::Value* category = value->get("category"); category && category->isString()) {
            definition.category = categoryOf(category->mString);
        }
        definition.minDistance = numberOr(value->get("min_distance"), definition.minDistance);
        if (const json::Value* maximum = value->get("max_distance"); maximum && maximum->isNumber()) {
            definition.maxDistance = static_cast<float>(maximum->mNumber);
            definition.hasMaxDistance = true;
        }
        const json::Value* sounds = value->get("sounds");
        if (!sounds || !sounds->isArray()) {
            continue;
        }
        for (const std::unique_ptr<json::Value>& entry : sounds->mArray) {
            SoundVariant variant;
            if (entry->isString()) {
                variant.path = entry->mString;
            } else if (entry->isObject()) {
                if (const json::Value* path = entry->get("name"); path && path->isString()) {
                    variant.path = path->mString;
                }
                variant.volume = numberOr(entry->get("volume"), 1.0f);
                variant.pitch = numberOr(entry->get("pitch"), 1.0f);
                variant.weight = std::max(1, static_cast<int>(numberOr(entry->get("weight"), 1.0f)));
                if (const json::Value* stream = entry->get("stream"); stream && stream->mType == json::Value::Type::Boolean) {
                    variant.stream = stream->mBoolean;
                }
                if (const json::Value* positional = entry->get("is3D"); positional && positional->mType == json::Value::Type::Boolean) {
                    variant.positional = positional->mBoolean;
                }
            }
            if (!variant.path.empty()) {
                definition.variants.push_back(std::move(variant));
            }
        }
        if (!definition.variants.empty()) {
            definitions[name] = std::move(definition);
        }
    }
}

void SoundLibrary::loadEvents(const std::string& text)
{
    std::unique_ptr<json::Value> root = parseJson(text);
    if (!root) {
        return;
    }
    auto readTable = [](const json::Value& group, EventTable& table) {
        table.volume = numberOr(group.get("volume"), 1.0f);
        rangeOf(group.get("pitch"), table.pitchMin, table.pitchMax);
        const json::Value* events = group.get("events");
        if (!events || !events->isObject()) {
            return;
        }
        for (const auto& [event, value] : events->mObject) {
            EventSound sound = eventSoundOf(*value, table.volume, table.pitchMin, table.pitchMax);
            if (!sound.sound.empty()) {
                table.events[event] = std::move(sound);
            }
        }
    };
    auto readGroups = [&](const json::Value* groups, std::unordered_map<std::string, EventTable>& into) {
        if (!groups || !groups->isObject()) {
            return;
        }
        for (const auto& [name, value] : groups->mObject) {
            if (value->isObject()) {
                readTable(*value, into[withoutNamespace(name)]);
            }
        }
    };

    if (const json::Value* individual = root->get("individual_event_sounds"); individual && individual->isObject()) {
        readTable(*individual, individualEvents);
    }
    readGroups(root->get("block_sounds"), blockSounds);
    if (const json::Value* entities = root->get("entity_sounds"); entities && entities->isObject()) {
        if (const json::Value* defaults = entities->get("defaults"); defaults && defaults->isObject()) {
            readTable(*defaults, entityDefaults);
        }
        readGroups(entities->get("entities"), entitySounds);
    }
    if (const json::Value* interactive = root->get("interactive_sounds"); interactive && interactive->isObject()) {
        readGroups(interactive->get("block_sounds"), interactiveBlockSounds);
        if (const json::Value* entities = interactive->get("entity_sounds"); entities && entities->isObject()) {
            if (const json::Value* defaults = entities->get("defaults"); defaults && defaults->isObject()) {
                readTable(*defaults, interactiveEntityDefaults);
            }
            readGroups(entities->get("entities"), interactiveEntitySounds);
        }
    }
}

void SoundLibrary::loadBlocks(const std::string& text)
{
    std::unique_ptr<json::Value> root = parseJson(text);
    if (!root) {
        return;
    }
    for (const auto& [name, value] : root->mObject) {
        if (!value->isObject()) {
            continue;
        }
        if (const json::Value* sound = value->get("sound"); sound && sound->isString()) {
            blockSoundTypes[withoutNamespace(name)] = sound->mString;
        }
    }
}

void SoundLibrary::loadMusic(const std::string& text)
{
    std::unique_ptr<json::Value> root = parseJson(text);
    if (!root) {
        return;
    }
    for (const auto& [situation, value] : root->mObject) {
        if (!value->isObject()) {
            continue;
        }
        MusicTrack track;
        if (const json::Value* event = value->get("event_name"); event && event->isString()) {
            track.event = event->mString;
        }
        track.minDelay = numberOr(value->get("min_delay"), 0.0f);
        track.maxDelay = numberOr(value->get("max_delay"), track.minDelay);
        if (!track.event.empty()) {
            musicTracks[situation] = std::move(track);
        }
    }
}

const SoundDefinition* SoundLibrary::definition(const std::string& name) const
{
    auto found = definitions.find(name);
    return found == definitions.end() ? nullptr : &found->second;
}

const MusicTrack* SoundLibrary::music(const std::string& situation) const
{
    auto found = musicTracks.find(situation);
    return found == musicTracks.end() ? nullptr : &found->second;
}

float SoundLibrary::pick(float low, float high)
{
    if (high <= low) {
        return low;
    }
    return std::uniform_real_distribution<float>(low, high)(random);
}

bool SoundLibrary::resolve(const std::string& name, float volume, float pitch, ResolvedSound& out)
{
    auto found = definitions.find(name);
    if (found == definitions.end()) {
        return false;
    }
    const SoundDefinition& definition = found->second;
    int total = 0;
    for (const SoundVariant& variant : definition.variants) {
        total += variant.weight;
    }
    int roll = std::uniform_int_distribution<int>(0, std::max(total - 1, 0))(random);
    const SoundVariant* chosen = &definition.variants.front();
    for (const SoundVariant& variant : definition.variants) {
        if (roll < variant.weight) {
            chosen = &variant;
            break;
        }
        roll -= variant.weight;
    }
    out.name = name;
    out.definition = &definition;
    out.variant = chosen;
    out.volume = volume * chosen->volume;
    out.pitch = pitch * chosen->pitch;
    return true;
}

bool SoundLibrary::resolveEvent(const EventSound& event, ResolvedSound& out)
{
    return resolve(event.sound, pick(event.volumeMin, event.volumeMax), pick(event.pitchMin, event.pitchMax), out);
}

bool SoundLibrary::fromTable(const EventTable& table, const std::string& event, ResolvedSound& out)
{
    auto found = table.events.find(event);
    return found != table.events.end() && resolveEvent(found->second, out);
}

bool SoundLibrary::levelEvent(const std::string& event, const std::string& actor, const std::string& blockName, bool baby, ResolvedSound& out)
{
    std::string entity = withoutNamespace(actor);
    bool found = false;
    if (!entity.empty()) {
        if (auto table = entitySounds.find(entity); table != entitySounds.end()) {
            found = fromTable(table->second, event, out);
        }
        if (!found) {
            if (auto table = interactiveEntitySounds.find(entity); table != interactiveEntitySounds.end()) {
                found = fromTable(table->second, event, out);
            }
        }
    }
    if (!found && !blockName.empty()) {
        std::string type = "normal";
        if (auto sound = blockSoundTypes.find(withoutNamespace(blockName)); sound != blockSoundTypes.end()) {
            type = sound->second;
        }
        for (auto* tables : { &blockSounds, &interactiveBlockSounds }) {
            if (found) {
                break;
            }
            auto table = tables->find(type);
            if (table == tables->end()) {
                table = tables->find("normal");
            }
            if (table != tables->end()) {
                found = fromTable(table->second, event, out);
            }
        }
    }
    if (!found && !entity.empty()) {
        found = fromTable(entityDefaults, event, out) || fromTable(interactiveEntityDefaults, event, out);
    }
    if (!found) {
        found = fromTable(individualEvents, event, out);
    }
    if (found && baby) {
        out.pitch *= 1.5f;
    }
    return found;
}

std::shared_ptr<const std::string> SoundLibrary::encoded(const std::string& path, std::string& extension)
{
    static constexpr const char* Extensions[] = { ".ogg", ".fsb", ".wav" };
    for (const std::shared_ptr<const world::PackFiles>& pack : overlays) {
        for (const char* candidate : Extensions) {
            if (const std::string* data = pack->find(path + candidate)) {
                extension = candidate;
                return std::make_shared<const std::string>(*data);
            }
        }
    }
    for (world::PackSource* source : { vanilla.get(), musicPack.get() }) {
        if (!source) {
            continue;
        }
        for (const char* candidate : Extensions) {
            std::string data;
            if (source->readText(path + candidate, data)) {
                extension = candidate;
                return std::make_shared<const std::string>(std::move(data));
            }
        }
    }
    return nullptr;
}

std::shared_ptr<const PcmBuffer> SoundLibrary::decoded(const std::string& path)
{
    {
        std::lock_guard<std::mutex> guard(cacheMutex);
        if (auto found = cache.find(path); found != cache.end()) {
            return found->second;
        }
    }
    std::string extension;
    std::shared_ptr<const std::string> data = encoded(path, extension);
    std::shared_ptr<PcmBuffer> buffer;
    if (data) {
        buffer = std::make_shared<PcmBuffer>();
        bool ok = extension == ".fsb" ? decodeFsb(*data, *buffer) : extension == ".wav" ? decodeWav(*data, *buffer) : decodeOgg(*data, *buffer);
        if (!ok) {
            buffer.reset();
        }
    }
    if (!buffer) {
        debugLog("sound file missing or undecodable: " + path + (extension.empty() ? std::string() : extension));
    }
    std::lock_guard<std::mutex> guard(cacheMutex);
    cache[path] = buffer;
    return buffer;
}

}
