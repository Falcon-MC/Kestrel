#include "audio/SoundLibrary.h"

#include "client/DebugLog.h"
#include "util/JsonText.h"
#include "util/Text.h"
#include "world/PackSource.h"

#include <algorithm>

namespace kestrel::audio {

namespace {

using util::jsonBool;
using util::jsonNumber;
using util::jsonRange;
using util::parseJsonObject;
using util::withoutNamespace;

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
    jsonRange(value.get("volume"), volumeLow, volumeHigh);
    float pitchLow = 1.0f;
    float pitchHigh = 1.0f;
    jsonRange(value.get("pitch"), pitchLow, pitchHigh);
    sound.volumeMin = volumeLow * groupVolume;
    sound.volumeMax = volumeHigh * groupVolume;
    sound.pitchMin = pitchLow * groupPitchMin;
    sound.pitchMax = pitchHigh * groupPitchMax;
    return sound;
}

template <typename Load>
void loadLayers(const std::vector<std::string>& layers, Load load)
{
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
        load(*layer);
    }
}

}

SoundLibrary::SoundLibrary(std::shared_ptr<world::PackSource> vanillaPack, std::shared_ptr<world::PackSource> music, std::vector<std::shared_ptr<const world::PackFiles>> serverPacks)
    : vanilla(std::move(vanillaPack))
    , musicPack(std::move(music))
    , overlays(std::move(serverPacks))
{
    auto definitionsFrom = [this](const std::string& text) {
        loadDefinitions(text);
    };
    if (vanilla) {
        loadLayers(vanilla->readArchivedLayers("sounds", "sound_definitions.json"), definitionsFrom);
        loadLayers(vanilla->readArchivedLayers("sounds", "music_definitions.json"), [this](const std::string& text) {
            loadMusic(text);
        });
        loadLayers(vanilla->readTextLayers("sounds/sound_definitions.json"), definitionsFrom);
        loadLayers(vanilla->readTextLayers("sounds.json"), [this](const std::string& text) {
            loadEvents(text);
        });
        loadLayers(vanilla->readTextLayers("blocks.json"), [this](const std::string& text) {
            loadBlocks(text);
        });
    }
    if (musicPack) {
        loadLayers(musicPack->readArchivedLayers("sounds", "sound_definitions.json"), definitionsFrom);
        loadLayers(musicPack->readTextLayers("sounds/sound_definitions.json"), definitionsFrom);
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
    std::unique_ptr<json::Value> root = parseJsonObject(text);
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
        definition.minDistance = jsonNumber(value->get("min_distance"), definition.minDistance);
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
                variant.volume = jsonNumber(entry->get("volume"), 1.0f);
                variant.pitch = jsonNumber(entry->get("pitch"), 1.0f);
                variant.weight = std::max(1, static_cast<int>(jsonNumber(entry->get("weight"), 1.0f)));
                variant.stream = jsonBool(entry->get("stream"), false);
                variant.positional = jsonBool(entry->get("is3D"), true);
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
    std::unique_ptr<json::Value> root = parseJsonObject(text);
    if (!root) {
        return;
    }
    auto readTable = [](const json::Value& group, EventTable& table) {
        table.volume = jsonNumber(group.get("volume"), 1.0f);
        jsonRange(group.get("pitch"), table.pitchMin, table.pitchMax);
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
    std::unique_ptr<json::Value> root = parseJsonObject(text);
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
    std::unique_ptr<json::Value> root = parseJsonObject(text);
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
        track.minDelay = jsonNumber(value->get("min_delay"), 0.0f);
        track.maxDelay = jsonNumber(value->get("max_delay"), track.minDelay);
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
        if (!decodeAudio(*data, extension, *buffer)) {
            buffer.reset();
        }
    }
    if (!buffer) {
        debugLog("sound file missing or undecodable: " + path + extension);
    }
    std::lock_guard<std::mutex> guard(cacheMutex);
    cache[path] = buffer;
    return buffer;
}

}
