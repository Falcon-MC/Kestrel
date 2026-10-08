#pragma once

#include "modding/CommandRegistry.h"
#include "modding/EmoteRegistry.h"
#include "modding/EventDispatcher.h"
#include "modding/KeyBindRegistry.h"
#include "modding/PacketFilters.h"
#include "modding/ShaderStore.h"
#include "modding/TaskScheduler.h"
#include "modding/ModUi.h"

#include "modding/ModManager.h"

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace kestrel::modding {

/**
 * A notification a mod put over the HUD with Hud::notify, shown until the
 * host clock reaches until.
 */
struct HudNotice {
    size_t owner = 0;
    std::string text;
    double until = 0.0;
};

/**
 * Everything the mods' services share: the client's pieces, the latest
 * snapshot and input, and the registries that remember who registered what.
 */
struct HostState {
    HostState(Session& session, menu::Menu& menu, Renderer& renderer, ClientBridge bridge, ErrorSink errors, ErrorSink threadErrors)
        : session(session)
        , menu(menu)
        , bridge(std::move(bridge))
        , errors(errors)
        , events(errors)
        , effects(this->bridge.effects)
        , textures(this->bridge.textures)
        , packets(std::make_shared<PacketFilters>(std::move(threadErrors)))
        , shaders(renderer)
    {
    }

    Session& session;
    menu::Menu& menu;
    ClientBridge bridge;
    ErrorSink errors;
    SessionSnapshot snapshot;
    const InputState* input = nullptr;
    bool inGame = false;
    float uiScale = 1.0f;
    std::set<size_t> cursorOwners;
    std::map<size_t, CameraRequest> cameras;
    // HUD elements each mod hides, one bit per mod::HudElement.
    std::map<size_t, uint32_t> hiddenHud;
    std::map<size_t, VisualRequest> visuals;
    std::map<size_t, std::set<std::string>> hiddenBlocks;
    std::map<size_t, std::set<std::string>> visibleBlocks;
    bool hiddenChanged = false;
    mod::Vec3 viewPosition;
    mod::Rotation viewRotation;
    float viewFieldOfView = 70.0f;
    double seconds = 0.0;
    mod::Environment environment;
    std::filesystem::path root;
    std::vector<mod::ModInfo> loaded;
    // The last drawn world's camera relative view projection, for Hud::project.
    std::optional<std::array<float, 16>> worldViewProjection;
    mod::Vec3 worldCamera;
    float interfaceWidth = 0.0f;
    float interfaceHeight = 0.0f;
    std::map<size_t, std::vector<mod::SettingSpec>> settings;
    std::vector<HudNotice> notices;
    std::map<std::string, std::pair<size_t, std::shared_ptr<void>>, std::less<>> services;

    EventDispatcher events;
    LocalEffects effects;
    TextureStore textures;
    ModUi ui;
    CommandRegistry commands;
    EmoteRegistry emotes;
    KeyBindRegistry keyBinds;
    TaskScheduler scheduler;
    std::shared_ptr<PacketFilters> packets;
    ShaderStore shaders;
};

}
