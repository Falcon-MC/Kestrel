#pragma once

#include "world/ServerPack.h"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace kestrel::world {
class PackSource;
}

namespace kestrel::audio {

enum class SoundCategory : uint8_t {
    Music,
    Ambient,
    Weather,
    Block,
    Hostile,
    Neutral,
    Player,
    Record,
    Ui,
    Count,
};

inline constexpr size_t SoundCategoryCount = static_cast<size_t>(SoundCategory::Count);

/**
 * Decoded audio: interleaved float samples at their own rate.
 */
struct PcmBuffer {
    uint32_t channels = 1;
    uint32_t sampleRate = 44100;
    std::vector<float> samples;
};

/**
 * One file a sound definition may pick: its path without extension, how loud
 * and how high it plays, how often it is picked and whether it streams.
 */
struct SoundVariant {
    std::string path;
    float volume = 1.0f;
    float pitch = 1.0f;
    int weight = 1;
    bool stream = false;
    bool positional = true;
};

struct SoundDefinition {
    SoundCategory category = SoundCategory::Neutral;
    float minDistance = 1.0f;
    float maxDistance = 16.0f;
    bool hasMaxDistance = false;
    std::vector<SoundVariant> variants;
};

/**
 * A named sound with the volume and pitch an event plays it at, each picked
 * inside its range.
 */
struct EventSound {
    std::string sound;
    float volumeMin = 1.0f;
    float volumeMax = 1.0f;
    float pitchMin = 1.0f;
    float pitchMax = 1.0f;
};

/**
 * A sound ready to play: the definition it came from, the variant picked,
 * the final volume and pitch.
 */
struct ResolvedSound {
    std::string name;
    const SoundDefinition* definition = nullptr;
    const SoundVariant* variant = nullptr;
    float volume = 1.0f;
    float pitch = 1.0f;
};

struct MusicTrack {
    std::string event;
    float minDelay = 0.0f;
    float maxDelay = 0.0f;
};

/**
 * Every sound the packs describe: named sound definitions, the block, entity
 * and individual events that pick them, the sound type of each block and the
 * music played in each situation, with the audio files decoded on demand.
 * Server packs override the vanilla ones.
 */
class SoundLibrary {
public:
    SoundLibrary(std::shared_ptr<world::PackSource> vanilla, std::shared_ptr<world::PackSource> music, std::vector<std::shared_ptr<const world::PackFiles>> overlays);

    const SoundDefinition* definition(const std::string& name) const;
    bool resolve(const std::string& name, float volume, float pitch, ResolvedSound& out);
    bool resolveEvent(const EventSound& event, ResolvedSound& out);

    /**
     * The sound a level event plays, looked up the way the game does: the
     * entity's own events, then the block's (for the block the extra data
     * names), then the entity defaults, then the individual events.
     */
    bool levelEvent(const std::string& event, const std::string& actor, const std::string& blockName, bool baby, ResolvedSound& out);

    const MusicTrack* music(const std::string& situation) const;

    size_t definitionCount() const
    {
        return definitions.size();
    }

    size_t musicCount() const
    {
        return musicTracks.size();
    }

    std::shared_ptr<const PcmBuffer> decoded(const std::string& path);
    std::shared_ptr<const std::string> encoded(const std::string& path, std::string& extension);

private:
    struct EventTable {
        float volume = 1.0f;
        float pitchMin = 1.0f;
        float pitchMax = 1.0f;
        std::unordered_map<std::string, EventSound> events;
    };

    void loadDefinitions(const std::string& text);
    void loadEvents(const std::string& text);
    void loadBlocks(const std::string& text);
    void loadMusic(const std::string& text);
    bool fromTable(const EventTable& table, const std::string& event, ResolvedSound& out);
    float pick(float low, float high);

    std::shared_ptr<world::PackSource> vanilla;
    std::shared_ptr<world::PackSource> musicPack;
    std::vector<std::shared_ptr<const world::PackFiles>> overlays;
    std::unordered_map<std::string, SoundDefinition> definitions;
    std::unordered_map<std::string, EventTable> blockSounds;
    std::unordered_map<std::string, EventTable> interactiveBlockSounds;
    std::unordered_map<std::string, EventTable> entitySounds;
    std::unordered_map<std::string, EventTable> interactiveEntitySounds;
    EventTable entityDefaults;
    EventTable interactiveEntityDefaults;
    EventTable individualEvents;
    std::unordered_map<std::string, std::string> blockSoundTypes;
    std::unordered_map<std::string, MusicTrack> musicTracks;
    std::mutex cacheMutex;
    std::unordered_map<std::string, std::shared_ptr<const PcmBuffer>> cache;
    std::mt19937 random { std::random_device {}() };
};

bool decodeFsb(const std::string& data, PcmBuffer& out);
bool decodeWav(const std::string& data, PcmBuffer& out);
bool decodeOgg(const std::string& data, PcmBuffer& out);

}
