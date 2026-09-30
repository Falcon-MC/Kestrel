#pragma once

#include "CommandRegistry.h"
#include "EventDispatcher.h"
#include "KeyBindRegistry.h"
#include "PacketFilters.h"
#include "ShaderStore.h"
#include "TaskScheduler.h"

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
    std::map<size_t, std::set<std::string>> hiddenBlocks;
    bool hiddenChanged = false;
    mod::Vec3 viewPosition;
    mod::Rotation viewRotation;
    double seconds = 0.0;
    mod::Environment environment;
    std::filesystem::path root;
    std::vector<mod::ModInfo> loaded;

    EventDispatcher events;
    CommandRegistry commands;
    KeyBindRegistry keyBinds;
    TaskScheduler scheduler;
    std::shared_ptr<PacketFilters> packets;
    ShaderStore shaders;
};

}
