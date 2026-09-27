#include "client/Client.h"

#include "client/LocalWorlds.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace kestrel {

void Client::loadWorlds()
{
    std::vector<menu::WorldEntry> entries;
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (const LocalWorld& world : scanLocalWorlds()) {
        std::string played = "Never played";
        if (world.lastPlayed > 0) {
            int64_t elapsed = std::max<int64_t>(0, now - world.lastPlayed);
            if (elapsed < 3600) {
                played = "Played " + std::to_string(elapsed / 60) + " min ago";
            } else if (elapsed < 86400) {
                played = "Played " + std::to_string(elapsed / 3600) + " h ago";
            } else {
                played = "Played " + std::to_string(elapsed / 86400) + " days ago";
            }
        }
        double megabytes = static_cast<double>(world.sizeBytes) / (1024.0 * 1024.0);
        char size[32];
        std::snprintf(size, sizeof(size), "%.1f MB", megabytes);
        entries.push_back({ world.name, played + " \xC2\xB7 " + size });
    }
    menu.setWorlds(std::move(entries));
}

void Client::loadSettings()
{
    std::ifstream file(settingsFile);
    KeyBindings bindings;
    std::string line;
    while (std::getline(file, line)) {
        size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, separator);
        std::string value = line.substr(separator + 1);
        if (key == "interfaceScale") {
            float parsed = std::strtof(value.c_str(), nullptr);
            if (parsed >= 1.0f && parsed <= 2.0f) {
                menu.setInterfaceScale(parsed);
            }
        } else if (key == "renderDistance") {
            menu.setRenderDistance(std::clamp(std::atoi(value.c_str()), menu::MinRenderDistance, menu::MaxRenderDistance));
        } else if (key == "maxFps") {
            int parsed = std::atoi(value.c_str());
            menu.setMaxFps(parsed == menu::UnlimitedFps ? parsed : std::clamp(parsed, menu::MinMaxFps, menu::MaxMaxFps));
        } else if (key == "fov") {
            menu.setFov(std::clamp(std::atoi(value.c_str()), menu::MinFov, menu::MaxFov));
        } else if (key.rfind("volume.", 0) == 0) {
            size_t channel = static_cast<size_t>(std::atoi(key.c_str() + 7));
            menu.setSoundVolume(channel, std::clamp(std::atoi(value.c_str()), 0, 100));
        }
        for (size_t i = 0; i < KeyBindings::Count; ++i) {
            if (key == std::string("key.") + KeyBindings::id(i)) {
                Key bound = keyFromName(value);
                if (bound != Key::None) {
                    bindings.keys[i] = bound;
                }
            }
        }
    }
    menu.setKeyBindings(bindings);
    savedScale = menu.interfaceScale();
    savedBindings = bindings;
    savedRenderDistance = menu.renderDistance();
    savedMaxFps = menu.maxFps();
    savedFov = menu.fov();
    savedVolumes = menu.soundVolumes();
}

void Client::saveSettings()
{
    std::ofstream file(settingsFile, std::ios::trunc);
    file << "interfaceScale=" << menu.interfaceScale() << '\n';
    file << "renderDistance=" << menu.renderDistance() << '\n';
    file << "maxFps=" << menu.maxFps() << '\n';
    file << "fov=" << menu.fov() << '\n';
    for (size_t i = 0; i < menu::VolumeChannelCount; ++i) {
        file << "volume." << i << '=' << menu.soundVolumes()[i] << '\n';
    }
    const KeyBindings& current = menu.keyBindings();
    for (size_t i = 0; i < KeyBindings::Count; ++i) {
        file << "key." << KeyBindings::id(i) << '=' << keyName(current.keys[i]) << '\n';
    }
    savedScale = menu.interfaceScale();
    savedBindings = current;
    savedRenderDistance = menu.renderDistance();
    savedMaxFps = menu.maxFps();
    savedFov = menu.fov();
    savedVolumes = menu.soundVolumes();
}

}
