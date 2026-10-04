#include "modding/ModSlot.h"

#include "mod/Api.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace kestrel::modding {

namespace {

using AbiFunction = const char* (*)();
using CreateFunction = mod::Mod* (*)();
using DestroyFunction = void (*)(mod::Mod*);

constexpr std::string_view AbiPrefix = "kestrel-mod/";

/**
 * Splits "kestrel-mod/<version>/<toolchain>" into its API version and the
 * toolchain part; false when the text has another shape.
 */
bool splitAbi(std::string_view abi, long& version, std::string_view& toolchain)
{
    if (abi.substr(0, AbiPrefix.size()) != AbiPrefix) {
        return false;
    }
    abi.remove_prefix(AbiPrefix.size());
    size_t slash = abi.find('/');
    if (slash == 0 || slash == std::string_view::npos) {
        return false;
    }
    version = 0;
    for (char c : abi.substr(0, slash)) {
        if (c < '0' || c > '9' || version > 100000) {
            return false;
        }
        version = version * 10 + (c - '0');
    }
    toolchain = abi.substr(slash + 1);
    return version > 0;
}

/**
 * A mod loads when it was built with the same compiler and runtime for this
 * API version or an older one: the interfaces only grow at their end, so
 * what an older mod calls is still where it expects it.
 */
bool compatibleAbi(std::string_view built, std::string_view host)
{
    long builtVersion = 0;
    long hostVersion = 0;
    std::string_view builtToolchain;
    std::string_view hostToolchain;
    return splitAbi(built, builtVersion, builtToolchain) && splitAbi(host, hostVersion, hostToolchain)
        && builtToolchain == hostToolchain && builtVersion <= hostVersion;
}

}

bool validModId(std::string_view id)
{
    if (id.empty() || id.size() > 64 || id == "." || id == "..") {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
    });
}

std::unique_ptr<ModSlot> ModSlot::load(const std::filesystem::path& file, HostState& host, size_t owner, std::string& error)
{
    platform::Library library;
    if (!library.open(file, error)) {
        return nullptr;
    }
    auto abi = reinterpret_cast<AbiFunction>(library.symbol("kestrel_mod_abi"));
    auto create = reinterpret_cast<CreateFunction>(library.symbol("kestrel_mod_create"));
    auto destroy = reinterpret_cast<DestroyFunction>(library.symbol("kestrel_mod_destroy"));
    if (!abi || !create || !destroy) {
        error = "not a Kestrel mod, KESTREL_MOD(...) is missing";
        return nullptr;
    }
    const char* wanted = KESTREL_MOD_ABI;
    const char* built = abi();
    if (!built || !compatibleAbi(built, wanted)) {
        error = std::string("built for ") + (built ? built : "?") + " but this Kestrel runs " + wanted + " and older API versions";
        return nullptr;
    }
    mod::Mod* instance = nullptr;
    try {
        instance = create();
    } catch (const std::exception& failure) {
        error = std::string("its constructor threw: ") + failure.what();
        return nullptr;
    } catch (...) {
        error = "its constructor threw";
        return nullptr;
    }
    if (!instance) {
        error = "it made no mod";
        return nullptr;
    }
    if (!validModId(instance->info().id)) {
        error = "its id \"" + instance->info().id + "\" may only use a-z, 0-9, '.', '-' and '_'";
        destroy(instance);
        return nullptr;
    }
    return std::unique_ptr<ModSlot>(new ModSlot(std::move(library), host, owner, instance, destroy));
}

ModSlot::ModSlot(platform::Library library, HostState& host, size_t owner, mod::Mod* instance, Destroy destroy)
    : library(std::move(library))
    , host(host)
    , id(owner)
    , instance(instance)
    , destroy(destroy)
    , eventService(host, owner)
    , chatService(host)
    , playerService(host)
    , worldService(host, owner)
    , networkService(host, owner)
    , commandService(host, owner)
    , loggerService(instance->info().id)
    , schedulerService(host, owner)
    , shaderService(host, owner)
    , settings(host.root / instance->info().id / "config.txt")
    , inputService(host, owner, settings)
    , cameraService(host, owner)
{
}

ModSlot::~ModSlot()
{
    if (enabled) {
        guarded(host.errors, id, [this] { instance->onDisable(); });
    }
    releaseAll();
    settings.saveIfChanged();
    guarded(host.errors, id, [this] { destroy(instance); });
}

bool ModSlot::enable()
{
    instance->host = this;
    bool ok = true;
    try {
        instance->onEnable();
    } catch (const std::exception& failure) {
        host.errors(id, std::string("onEnable threw: ") + failure.what());
        ok = false;
    } catch (...) {
        host.errors(id, "onEnable threw");
        ok = false;
    }
    if (!ok) {
        releaseAll();
    }
    enabled = ok;
    return ok;
}

void ModSlot::releaseAll()
{
    host.events.release(id);
    host.commands.release(id);
    host.keyBinds.release(id);
    host.scheduler.release(id);
    host.packets->release(id);
    host.shaders.release(id);
    host.cursorOwners.erase(id);
    host.cameras.erase(id);
    if (host.hiddenBlocks.erase(id)) {
        host.hiddenChanged = true;
    }
    if (host.visibleBlocks.erase(id)) {
        host.hiddenChanged = true;
    }
}

mod::EventBus& ModSlot::events()
{
    return eventService;
}

mod::Chat& ModSlot::chat()
{
    return chatService;
}

mod::Player& ModSlot::player()
{
    return playerService;
}

mod::World& ModSlot::world()
{
    return worldService;
}

mod::Network& ModSlot::network()
{
    return networkService;
}

mod::Input& ModSlot::input()
{
    return inputService;
}

mod::Commands& ModSlot::commands()
{
    return commandService;
}

mod::Config& ModSlot::config()
{
    return settings;
}

mod::Logger& ModSlot::log()
{
    return loggerService;
}

mod::Scheduler& ModSlot::scheduler()
{
    return schedulerService;
}

mod::Shaders& ModSlot::shaders()
{
    return shaderService;
}

mod::Camera& ModSlot::camera()
{
    return cameraService;
}

std::filesystem::path ModSlot::dataDirectory()
{
    std::filesystem::path folder = host.root / info().id;
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    return folder;
}

std::vector<mod::ModInfo> ModSlot::loadedMods() const
{
    return host.loaded;
}

}
