#include "client/Client.h"

#include "client/DebugLog.h"
#include "world/PackSource.h"

#include <algorithm>
#include <cstdlib>

namespace kestrel {

/**
 * Keeps the sound engine in step with the frame: rebuilds the sound library
 * when the server's packs change, follows the camera, applies the volume
 * settings, plays interface clicks, the sounds the world asked for, rain and
 * music, and frees the sounds that ended.
 */
void Client::updateAudio(const SessionSnapshot& snapshot)
{
    if (!soundEngine || !soundEngine->ready()) {
        return;
    }
    std::vector<std::shared_ptr<const world::PackFiles>> packs = snapshot.state == SessionState::Joined ? snapshot.packs : std::vector<std::shared_ptr<const world::PackFiles>> {};
    if (!soundLibraryBuilt || packs != soundPacks) {
        soundPacks = packs;
        soundLibraryBuilt = true;
        auto library = std::make_shared<audio::SoundLibrary>(vanillaSounds, musicSounds, soundPacks);
        debugLog("sound library: " + std::to_string(library->definitionCount()) + " definitions, " + std::to_string(library->musicCount()) + " music situations, vanilla " + (vanillaSounds ? vanillaSounds->root().string() : std::string("none")) + ", music " + (musicSounds ? musicSounds->root().string() : std::string("none")));
        soundEngine->setLibrary(std::move(library));
        musicSituation.clear();
    }

    const std::array<int, menu::VolumeChannelCount>& volumes = menu.soundVolumes();
    std::array<float, audio::SoundCategoryCount> categories {};
    for (size_t i = 0; i < categories.size(); ++i) {
        categories[i] = static_cast<float>(volumes[i + 1]) / 100.0f;
    }
    soundEngine->setVolumes(static_cast<float>(volumes[0]) / 100.0f, categories);
    soundEngine->setListener({ camera.x(), camera.y(), camera.z() }, camera.forward());

    if (widgets.clicks != heardClicks) {
        heardClicks = widgets.clicks;
        soundEngine->playNamed("random.click", {}, false);
    }
    for (const SoundRequest& request : session.takeSounds()) {
        playSoundRequest(request);
    }

    double now = secondsNow();
    if (worldShown && snapshot.rainLevel > 0.0f && snapshot.cameraMedium == 0) {
        if (now >= nextRainSoundAt) {
            nextRainSoundAt = now + 0.4 + (std::rand() % 400) / 1000.0;
            soundEngine->playNamed("ambient.weather.rain", { camera.x(), camera.y(), camera.z() }, false, snapshot.rainLevel);
        }
    }
    updateMusic(snapshot);
    soundEngine->update();
}

void Client::playSoundRequest(const SoundRequest& request)
{
    audio::SoundLibrary* library = soundEngine->library();
    if (!library) {
        return;
    }
    switch (request.kind) {
    case SoundRequest::Kind::Event: {
        audio::ResolvedSound sound;
        if (library->levelEvent(request.name, request.actor, request.block, request.baby, sound)) {
            soundEngine->play(sound, request.position, !request.global);
        }
        break;
    }
    case SoundRequest::Kind::Named:
        soundEngine->playNamed(request.name, request.position, !request.global, request.volume, request.pitch);
        break;
    case SoundRequest::Kind::Stop:
        soundEngine->stop(request.name);
        break;
    case SoundRequest::Kind::StopAll:
        soundEngine->stopAll();
        soundEngine->stopMusic();
        break;
    }
}

/**
 * Picks the music for where the player is (menu, game, creative, water,
 * nether, end), starts a track after a short wait when that changes, and
 * waits the track's own delay between the next ones.
 */
void Client::updateMusic(const SessionSnapshot& snapshot)
{
    audio::SoundLibrary* library = soundEngine->library();
    if (!library) {
        return;
    }
    std::string situation = "menu";
    if (worldShown) {
        situation = "game";
        if (snapshot.dimension == 1) {
            situation = "nether";
        } else if (snapshot.dimension == 2) {
            situation = "end";
        } else if (snapshot.cameraMedium == 1) {
            situation = "water";
        } else if (snapshot.hud.gameType == 1) {
            situation = "creative";
        }
    }
    const audio::MusicTrack* track = library->music(situation);
    if (!track) {
        track = library->music(worldShown ? "game" : "menu");
    }
    double now = secondsNow();
    if (situation != musicSituation) {
        bool keep = musicSituation.size() && situation != "menu" && musicSituation != "menu" && soundEngine->musicPlaying();
        musicSituation = situation;
        if (!keep) {
            soundEngine->stopMusic();
            nextMusicAt = now + 2.0 + (std::rand() % 8000) / 1000.0;
        }
    }
    bool playing = soundEngine->musicPlaying();
    if (musicWasPlaying && !playing && track) {
        double span = std::max(0.0, double(track->maxDelay - track->minDelay));
        nextMusicAt = now + track->minDelay + span * (std::rand() % 1000) / 1000.0;
    }
    musicWasPlaying = playing;
    if (!playing && track && now >= nextMusicAt) {
        audio::ResolvedSound sound;
        if (library->resolve(track->event, 1.0f, 1.0f, sound)) {
            soundEngine->playMusic(sound);
            musicWasPlaying = soundEngine->musicPlaying();
        }
        nextMusicAt = now + 30.0;
    }
}

}
