#include "audio/SoundEngine.h"

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#include <algorithm>
#include <list>
#include <cmath>

namespace kestrel::audio {

namespace {

constexpr size_t MaxVoices = 96;

}

/**
 * One playing sound and the data it reads from: a decoded buffer, or the
 * encoded file a streaming decoder reads.
 */
struct Voice {
    uint64_t id = 0;
    float baseVolume = 1.0f;
    float minDistance = 1.0f;
    bool fixedDistance = false;
    bool spatial = false;
    bool tracked = false;
    std::string name;
    std::shared_ptr<const PcmBuffer> pcm;
    std::shared_ptr<const std::string> encoded;
    ma_audio_buffer buffer {};
    ma_decoder decoder {};
    ma_sound sound {};
    bool hasBuffer = false;
    bool hasDecoder = false;
    bool hasSound = false;
    bool music = false;

    ~Voice()
    {
        if (hasSound) {
            ma_sound_uninit(&sound);
        }
        if (hasDecoder) {
            ma_decoder_uninit(&decoder);
        }
        if (hasBuffer) {
            ma_audio_buffer_uninit(&buffer);
        }
    }
};

struct SoundEngine::Impl {
    ma_engine engine {};
    bool running = false;
    std::array<ma_sound_group, SoundCategoryCount> groups {};
    std::list<std::unique_ptr<Voice>> voices;
    uint64_t nextVoice = 0;
};

SoundEngine::SoundEngine()
    : impl(std::make_unique<Impl>())
{
    ma_engine_config config = ma_engine_config_init();
    config.listenerCount = 1;
    if (ma_engine_init(&config, &impl->engine) != MA_SUCCESS) {
        return;
    }
    impl->running = true;
    for (ma_sound_group& group : impl->groups) {
        ma_sound_group_init(&impl->engine, 0, nullptr, &group);
    }
    ma_engine_listener_set_world_up(&impl->engine, 0, 0.0f, 1.0f, 0.0f);
}

SoundEngine::~SoundEngine()
{
    if (!impl->running) {
        return;
    }
    impl->voices.clear();
    for (ma_sound_group& group : impl->groups) {
        ma_sound_group_uninit(&group);
    }
    ma_engine_uninit(&impl->engine);
}

bool SoundEngine::ready() const
{
    return impl->running;
}

void SoundEngine::setLibrary(std::shared_ptr<SoundLibrary> value)
{
    stopAll();
    sounds = std::move(value);
}

void SoundEngine::setListener(const std::array<double, 3>& position, const std::array<float, 3>& forward)
{
    listener = position;
    if (!impl->running) {
        return;
    }
    ma_engine_listener_set_position(&impl->engine, 0, float(position[0]), float(position[1]), float(position[2]));
    ma_engine_listener_set_direction(&impl->engine, 0, forward[0], forward[1], forward[2]);
}

void SoundEngine::setVolumes(float master, const std::array<float, SoundCategoryCount>& categories)
{
    if (!impl->running) {
        return;
    }
    ma_engine_set_volume(&impl->engine, std::clamp(master, 0.0f, 1.0f));
    for (size_t i = 0; i < SoundCategoryCount; ++i) {
        ma_sound_group_set_volume(&impl->groups[i], std::clamp(categories[i], 0.0f, 1.0f));
    }
}

void SoundEngine::play(const ResolvedSound& sound, const std::array<double, 3>& position, bool positional)
{
    if (sound.volume > 0.0f) playVoice(sound, position, positional, 1.0f, false, false);
}

uint64_t SoundEngine::playVoice(const ResolvedSound& sound, const std::array<double, 3>& position, bool positional, float volume, bool loop, bool tracked)
{
    if (!impl->running || !sounds || !sound.definition || !sound.variant || impl->nextVoice == UINT64_MAX) {
        return 0;
    }
    const SoundDefinition& definition = *sound.definition;
    bool spatial = positional && sound.variant->positional;
    float gain = sound.volume * volume;
    float maxDistance = definition.hasMaxDistance ? definition.maxDistance : 16.0f * std::max(gain, 1.0f);
    if (spatial && !tracked) {
        double dx = position[0] - listener[0];
        double dy = position[1] - listener[1];
        double dz = position[2] - listener[2];
        if (dx * dx + dy * dy + dz * dz > double(maxDistance) * maxDistance) {
            return 0;
        }
    }
    std::shared_ptr<const PcmBuffer> pcm = sounds->decoded(sound.variant->path);
    if (!pcm || pcm->samples.empty() || pcm->channels == 0) {
        return 0;
    }
    while (impl->voices.size() >= MaxVoices) {
        auto oldest = std::find_if(impl->voices.begin(), impl->voices.end(), [](const std::unique_ptr<Voice>& voice) {
            return !voice->music;
        });
        if (oldest == impl->voices.end()) {
            return 0;
        }
        impl->voices.erase(oldest);
    }

    auto voice = std::make_unique<Voice>();
    voice->id = ++impl->nextVoice;
    voice->baseVolume = sound.volume;
    voice->minDistance = definition.minDistance;
    voice->fixedDistance = definition.hasMaxDistance;
    voice->spatial = spatial;
    voice->tracked = tracked;
    voice->name = sound.name;
    voice->pcm = pcm;
    ma_audio_buffer_config config = ma_audio_buffer_config_init(ma_format_f32, pcm->channels, pcm->samples.size() / pcm->channels, pcm->samples.data(), nullptr);
    config.sampleRate = pcm->sampleRate;
    if (ma_audio_buffer_init(&config, &voice->buffer) != MA_SUCCESS) {
        return 0;
    }
    voice->hasBuffer = true;
    ma_uint32 flags = spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
    ma_sound_group* group = &impl->groups[static_cast<size_t>(definition.category)];
    if (ma_sound_init_from_data_source(&impl->engine, &voice->buffer, flags, group, &voice->sound) != MA_SUCCESS) {
        return 0;
    }
    voice->hasSound = true;
    ma_sound_set_volume(&voice->sound, spatial ? std::min(gain, 1.0f) : gain);
    ma_sound_set_looping(&voice->sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_pitch(&voice->sound, std::clamp(sound.pitch, 0.1f, 4.0f));
    if (spatial) {
        ma_sound_set_attenuation_model(&voice->sound, ma_attenuation_model_linear);
        ma_sound_set_rolloff(&voice->sound, 1.0f);
        ma_sound_set_min_distance(&voice->sound, std::max(definition.minDistance, 0.0f));
        ma_sound_set_max_distance(&voice->sound, std::max(maxDistance, definition.minDistance + 0.01f));
        ma_sound_set_position(&voice->sound, float(position[0]), float(position[1]), float(position[2]));
    }
    if (ma_sound_start(&voice->sound) != MA_SUCCESS) return 0;
    uint64_t id = voice->id;
    impl->voices.push_back(std::move(voice));
    return id;
}

uint64_t SoundEngine::playTracked(const std::string& name, const std::array<double, 3>& position, bool positional, float volume, float pitch, bool loop)
{
    if (!std::isfinite(volume) || volume < 0.0f || volume > 4.0f || !std::isfinite(pitch) || pitch < 0.1f || pitch > 4.0f) return 0;
    for (double coordinate : position) if (!std::isfinite(coordinate)) return 0;
    ResolvedSound sound;
    if (!sounds || !sounds->resolve(name, 1.0f, pitch, sound)) return 0;
    return playVoice(sound, position, positional, volume, loop, true);
}

bool SoundEngine::playing(uint64_t handle) const
{
    if (!handle) return false;
    for (const auto& voice : impl->voices) {
        if (voice->id == handle && voice->tracked) return voice->hasSound && ma_sound_is_playing(&voice->sound) && !ma_sound_at_end(&voice->sound);
    }
    return false;
}

bool SoundEngine::stopVoice(uint64_t handle)
{
    return handle && std::erase_if(impl->voices, [handle](const auto& voice) { return voice->tracked && voice->id == handle; }) != 0;
}

bool SoundEngine::moveVoice(uint64_t handle, const std::array<double, 3>& position)
{
    for (double coordinate : position) if (!std::isfinite(coordinate)) return false;
    if (!playing(handle)) return false;
    for (const auto& voice : impl->voices) {
        if (voice->id != handle || !voice->spatial) continue;
        ma_sound_set_position(&voice->sound, float(position[0]), float(position[1]), float(position[2]));
        return true;
    }
    return false;
}

bool SoundEngine::setVoiceVolume(uint64_t handle, float volume)
{
    if (!std::isfinite(volume) || volume < 0.0f || volume > 4.0f || !playing(handle)) return false;
    for (const auto& voice : impl->voices) {
        if (voice->id != handle) continue;
        float gain = voice->baseVolume * volume;
        ma_sound_set_volume(&voice->sound, voice->spatial ? std::min(gain, 1.0f) : gain);
        if (voice->spatial && !voice->fixedDistance) {
            ma_sound_set_max_distance(&voice->sound, std::max(16.0f * std::max(gain, 1.0f), voice->minDistance + 0.01f));
        }
        return true;
    }
    return false;
}

bool SoundEngine::playNamed(const std::string& name, const std::array<double, 3>& position, bool positional, float volume, float pitch)
{
    ResolvedSound sound;
    if (!sounds || !sounds->resolve(name, volume, pitch, sound)) {
        return false;
    }
    play(sound, position, positional);
    return true;
}

void SoundEngine::setLoop(const std::string& name, bool enabled)
{
    if (!impl->running) return;
    if (!enabled) { stop(name); return; }
    for (const auto& voice : impl->voices) {
        if (!voice->music && !voice->tracked && voice->name == name && voice->hasSound && !ma_sound_at_end(&voice->sound)) return;
    }
    ResolvedSound sound;
    if (!sounds || !sounds->resolve(name, 1.0f, 1.0f, sound)) return;
    play(sound, {}, false);
    if (!impl->voices.empty()) {
        auto& voice = impl->voices.back();
        if (voice->name == name && voice->hasSound) ma_sound_set_looping(&voice->sound, MA_TRUE);
    }
}

void SoundEngine::playMusic(const ResolvedSound& sound)
{
    if (!impl->running || !sounds || !sound.variant) {
        return;
    }
    stopMusic();
    auto voice = std::make_unique<Voice>();
    voice->name = sound.name;
    voice->music = true;
    std::string extension;
    std::shared_ptr<const std::string> encoded = sounds->encoded(sound.variant->path, extension);
    ma_data_source* source = nullptr;
    if (encoded && extension == ".ogg") {
        voice->encoded = encoded;
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
        if (ma_decoder_init_memory(voice->encoded->data(), voice->encoded->size(), &config, &voice->decoder) == MA_SUCCESS) {
            voice->hasDecoder = true;
            source = &voice->decoder;
        }
    }
    if (!source) {
        std::shared_ptr<const PcmBuffer> pcm = sounds->decoded(sound.variant->path);
        if (!pcm || pcm->samples.empty()) {
            return;
        }
        voice->pcm = pcm;
        ma_audio_buffer_config config = ma_audio_buffer_config_init(ma_format_f32, pcm->channels, pcm->samples.size() / pcm->channels, pcm->samples.data(), nullptr);
        config.sampleRate = pcm->sampleRate;
        if (ma_audio_buffer_init(&config, &voice->buffer) != MA_SUCCESS) {
            return;
        }
        voice->hasBuffer = true;
        source = &voice->buffer;
    }
    ma_sound_group* group = &impl->groups[static_cast<size_t>(SoundCategory::Music)];
    if (ma_sound_init_from_data_source(&impl->engine, source, MA_SOUND_FLAG_NO_SPATIALIZATION, group, &voice->sound) != MA_SUCCESS) {
        return;
    }
    voice->hasSound = true;
    ma_sound_set_volume(&voice->sound, sound.volume);
    ma_sound_start(&voice->sound);
    impl->voices.push_back(std::move(voice));
}

bool SoundEngine::musicPlaying() const
{
    for (const std::unique_ptr<Voice>& voice : impl->voices) {
        if (voice->music && voice->hasSound && !ma_sound_at_end(&voice->sound)) {
            return true;
        }
    }
    return false;
}

void SoundEngine::stopMusic()
{
    impl->voices.remove_if([](const std::unique_ptr<Voice>& voice) {
        return voice->music;
    });
}

void SoundEngine::stop(const std::string& name)
{
    impl->voices.remove_if([&](const std::unique_ptr<Voice>& voice) {
        return !voice->music && !voice->tracked && voice->name == name;
    });
}

void SoundEngine::stopAll()
{
    impl->voices.remove_if([](const std::unique_ptr<Voice>& voice) {
        return !voice->music;
    });
}

void SoundEngine::update()
{
    impl->voices.remove_if([](const std::unique_ptr<Voice>& voice) {
        return !voice->hasSound || ma_sound_at_end(&voice->sound);
    });
}

}
