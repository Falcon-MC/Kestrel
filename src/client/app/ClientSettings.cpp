#include "client/Client.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace kestrel {

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
        } else if (key == "fullscreen") {
            savedFullscreen = value == "1";
        } else if (key == "hidePaperDoll") {
            menu.setPaperDollHidden(value == "1");
        } else if (key == "safeArea") {
            float parsed = std::strtof(value.c_str(), nullptr);
            if (parsed >= menu::MinSafeArea && parsed <= menu::MaxSafeArea) {
                menu.setSafeArea(parsed);
            }
        } else if (key == "brightness") {
            char* end = nullptr;
            long parsed = std::strtol(value.c_str(), &end, 10);
            if (end != value.c_str() && *end == '\0') {
                menu.setBrightness(static_cast<int>(std::clamp<long>(parsed, menu::MinBrightness, menu::MaxBrightness)));
            }
        } else if (key == "language") {
            menu.setLanguage(value);
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
    savedPaperDollHidden = menu.paperDollHidden();
    savedSafeArea = menu.safeArea();
    savedBrightness = menu.brightness();
    savedVolumes = menu.soundVolumes();
    savedLanguage = menu.language();
}

void Client::saveSettings()
{
    if (window) {
        savedFullscreen = window->fullscreen();
    }
    std::ofstream file(settingsFile, std::ios::trunc);
    file << "interfaceScale=" << menu.interfaceScale() << '\n';
    file << "renderDistance=" << menu.renderDistance() << '\n';
    file << "maxFps=" << menu.maxFps() << '\n';
    file << "fov=" << menu.fov() << '\n';
    file << "fullscreen=" << (savedFullscreen ? 1 : 0) << '\n';
    file << "hidePaperDoll=" << (menu.paperDollHidden() ? 1 : 0) << '\n';
    file << "safeArea=" << menu.safeArea() << '\n';
    file << "brightness=" << menu.brightness() << '\n';
    file << "language=" << menu.language() << '\n';
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
    savedPaperDollHidden = menu.paperDollHidden();
    savedSafeArea = menu.safeArea();
    savedBrightness = menu.brightness();
    savedVolumes = menu.soundVolumes();
    savedLanguage = menu.language();
}

}
