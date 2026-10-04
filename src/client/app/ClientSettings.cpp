#include "client/Client.h"
#include "mod/Events.h"
#include "modding/ModManager.h"

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
    menu::ChatSettings chat;
    std::array<std::string, menu::EmoteSlotCount> emoteSlots {};
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
        } else if (key == "vsync") {
            menu.setVsync(value == "1");
        } else if (key == "gameplayFov") {
            menu.setGameplayFov(value == "1");
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
        } else if (key == "offlineName") {
            menu.setOfflineName(value);
        } else if (key.rfind("option.", 0) == 0) {
            menu.setExtraOption(key.substr(7), std::atoi(value.c_str()));
        } else if (key == "glintStrength") {
            menu.setGlintStrength(std::atoi(value.c_str()));
        } else if (key == "glintSpeed") {
            menu.setGlintSpeed(std::atoi(value.c_str()));
        } else if (key == "language") {
            menu.setLanguage(value);
        } else if (key.rfind("volume.", 0) == 0) {
            size_t channel = static_cast<size_t>(std::atoi(key.c_str() + 7));
            menu.setSoundVolume(channel, std::clamp(std::atoi(value.c_str()), 0, 100));
        } else if (key == "chat.muteAll") {
            chat.muteAll = value == "1";
        } else if (key == "chat.muteEmotes") {
            chat.muteEmotes = value == "1";
        } else if (key == "chat.textToSpeech") {
            chat.textToSpeech = value == "1";
        } else if (key == "chat.font") {
            chat.smoothFont = value == "smooth";
        } else if (key == "chat.fontSize") {
            chat.fontSize = std::atoi(value.c_str());
        } else if (key == "chat.lineSpacing") {
            chat.lineSpacing = std::strtof(value.c_str(), nullptr);
        } else if (key == "chat.color") {
            chat.chatColor = std::atoi(value.c_str());
        } else if (key == "chat.mentionsColor") {
            chat.mentionsColor = std::atoi(value.c_str());
        } else if (key.rfind("emote.", 0) == 0) {
            size_t slot = static_cast<size_t>(std::atoi(key.c_str() + 6));
            if (slot < menu::EmoteSlotCount) {
                emoteSlots[slot] = value;
            }
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
    menu.setChatSettings(chat);
    menu.setEmoteSlots(emoteSlots);
    savedChat = menu.chatSettings();
    savedScale = menu.interfaceScale();
    savedBindings = bindings;
    savedRenderDistance = menu.renderDistance();
    savedMaxFps = menu.maxFps();
    savedFov = menu.fov();
    savedPaperDollHidden = menu.paperDollHidden();
    savedVsync = menu.vsync();
    savedGameplayFov = menu.gameplayFov();
    savedSafeArea = menu.safeArea();
    savedBrightness = menu.brightness();
    savedGlintStrength = menu.glintStrength();
    savedGlintSpeed = menu.glintSpeed();
    savedExtraOptions = menu.extraOptions();
    savedOfflineName = menu.offlineName();
    savedVolumes = menu.soundVolumes();
    savedLanguage = menu.language();
}

void Client::saveSettings()
{
    uint32_t changed = 0;
    auto mark = [&changed](bool differs, mod::SettingsChange setting) {
        if (differs) {
            changed |= static_cast<uint32_t>(setting);
        }
    };
    mark(menu.fov() != savedFov, mod::SettingsChange::Fov);
    mark(menu.interfaceScale() != savedScale, mod::SettingsChange::GuiScale);
    mark(menu.language() != savedLanguage, mod::SettingsChange::Language);
    mark(menu.renderDistance() != savedRenderDistance, mod::SettingsChange::RenderDistance);
    mark(menu.maxFps() != savedMaxFps, mod::SettingsChange::MaxFps);
    mark(menu.vsync() != savedVsync, mod::SettingsChange::Vsync);
    mark(menu.gameplayFov() != savedGameplayFov, mod::SettingsChange::GameplayFov);
    mark(window && window->fullscreen() != savedFullscreen, mod::SettingsChange::Fullscreen);
    mark(menu.soundVolumes() != savedVolumes, mod::SettingsChange::Volumes);
    mark(!(menu.keyBindings() == savedBindings), mod::SettingsChange::KeyBindings);
    mark(!(menu.chatSettings() == savedChat), mod::SettingsChange::Chat);
    mark(menu.brightness() != savedBrightness, mod::SettingsChange::Brightness);
    mark(menu.safeArea() != savedSafeArea, mod::SettingsChange::SafeArea);
    mark(menu.paperDollHidden() != savedPaperDollHidden, mod::SettingsChange::PaperDoll);
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
    file << "vsync=" << (menu.vsync() ? 1 : 0) << '\n';
    file << "gameplayFov=" << (menu.gameplayFov() ? 1 : 0) << '\n';
    file << "safeArea=" << menu.safeArea() << '\n';
    file << "brightness=" << menu.brightness() << '\n';
    file << "glintStrength=" << menu.glintStrength() << '\n';
    file << "glintSpeed=" << menu.glintSpeed() << '\n';
    file << "offlineName=" << menu.offlineName() << '\n';
    for (const auto& [name, value] : menu.extraOptions()) {
        file << "option." << name << '=' << value << '\n';
    }
    file << "language=" << menu.language() << '\n';
    for (size_t i = 0; i < menu::VolumeChannelCount; ++i) {
        file << "volume." << i << '=' << menu.soundVolumes()[i] << '\n';
    }
    const menu::ChatSettings& chat = menu.chatSettings();
    file << "chat.muteAll=" << (chat.muteAll ? 1 : 0) << '\n';
    file << "chat.muteEmotes=" << (chat.muteEmotes ? 1 : 0) << '\n';
    file << "chat.textToSpeech=" << (chat.textToSpeech ? 1 : 0) << '\n';
    file << "chat.font=" << (chat.smoothFont ? "smooth" : "default") << '\n';
    file << "chat.fontSize=" << chat.fontSize << '\n';
    file << "chat.lineSpacing=" << chat.lineSpacing << '\n';
    file << "chat.color=" << chat.chatColor << '\n';
    file << "chat.mentionsColor=" << chat.mentionsColor << '\n';
    const KeyBindings& current = menu.keyBindings();
    for (size_t i = 0; i < KeyBindings::Count; ++i) {
        file << "key." << KeyBindings::id(i) << '=' << keyName(current.keys[i]) << '\n';
    }
    for (size_t slot = 0; slot < menu::EmoteSlotCount; ++slot) {
        if (!menu.emoteSlots()[slot].empty()) {
            file << "emote." << slot << '=' << menu.emoteSlots()[slot] << '\n';
        }
    }
    savedChat = chat;
    savedScale = menu.interfaceScale();
    savedBindings = current;
    savedRenderDistance = menu.renderDistance();
    savedMaxFps = menu.maxFps();
    savedFov = menu.fov();
    savedPaperDollHidden = menu.paperDollHidden();
    savedVsync = menu.vsync();
    savedGameplayFov = menu.gameplayFov();
    savedSafeArea = menu.safeArea();
    savedBrightness = menu.brightness();
    savedGlintStrength = menu.glintStrength();
    savedGlintSpeed = menu.glintSpeed();
    savedExtraOptions = menu.extraOptions();
    savedOfflineName = menu.offlineName();
    savedVolumes = menu.soundVolumes();
    savedLanguage = menu.language();
    if (mods) {
        mods->settingsChanged(changed, menu.fov(), menu.interfaceScale(), menu.renderDistance(), menu.language());
    }
}

}
