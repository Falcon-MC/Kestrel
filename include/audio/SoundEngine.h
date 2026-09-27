#pragma once

#include "audio/SoundLibrary.h"

#include <array>
#include <memory>
#include <string>

namespace kestrel::audio {

/**
 * Plays sounds through the default output device: positional sounds heard
 * from the listener with linear falloff, flat sounds such as the interface
 * and music, one volume per category under a master volume.
 */
class SoundEngine {
public:
    SoundEngine();
    ~SoundEngine();

    SoundEngine(const SoundEngine&) = delete;
    SoundEngine& operator=(const SoundEngine&) = delete;

    bool ready() const;

    void setLibrary(std::shared_ptr<SoundLibrary> value);

    SoundLibrary* library() const
    {
        return sounds.get();
    }

    void setListener(const std::array<double, 3>& position, const std::array<float, 3>& forward);
    void setVolumes(float master, const std::array<float, SoundCategoryCount>& categories);

    void play(const ResolvedSound& sound, const std::array<double, 3>& position, bool positional);
    bool playNamed(const std::string& name, const std::array<double, 3>& position, bool positional, float volume = 1.0f, float pitch = 1.0f);
    void playMusic(const ResolvedSound& sound);
    bool musicPlaying() const;
    void stopMusic();
    void stop(const std::string& name);
    void stopAll();

    /**
     * Releases the sounds that have finished playing.
     */
    void update();

private:
    struct Impl;

    std::unique_ptr<Impl> impl;
    std::shared_ptr<SoundLibrary> sounds;
    std::array<double, 3> listener {};
};

}
