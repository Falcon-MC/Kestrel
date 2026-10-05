#pragma once

#include "modding/CommandRegistry.h"
#include "modding/EmoteRegistry.h"
#include "modding/EventDispatcher.h"
#include "modding/KeyBindRegistry.h"
#include "modding/PacketFilters.h"
#include "modding/ShaderStore.h"
#include "modding/TaskScheduler.h"

#include "modding/ModManager.h"

#include <filesystem>
#include <map>
#include <string>
#include <memory>
#include <set>
#include <vector>

namespace kestrel::modding {

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
    std::map<size_t, std::set<std::string>> hiddenBlocks;
    std::map<size_t, std::set<std::string>> visibleBlocks;
    bool hiddenChanged = false;
    mod::Vec3 viewPosition;
    mod::Rotation viewRotation;
    double seconds = 0.0;
    mod::Environment environment;
    std::filesystem::path root;
    std::vector<mod::ModInfo> loaded;

    EventDispatcher events;
    CommandRegistry commands;
    EmoteRegistry emotes;
    KeyBindRegistry keyBinds;
    TaskScheduler scheduler;
    std::shared_ptr<PacketFilters> packets;
    ShaderStore shaders;
};

}
