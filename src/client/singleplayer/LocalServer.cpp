#include "client/LocalServer.h"

#include "client/DebugLog.h"

#include "Falcon/EmbeddedServer.h"

#include <chrono>

namespace kestrel {

namespace {

const char* const GameModes[] = { "survival", "creative", "adventure" };
const char* const Difficulties[] = { "peaceful", "easy", "normal", "hard" };

}

LocalServer::LocalServer(std::filesystem::path directory)
    : directory(std::move(directory))
{
}

LocalServer::~LocalServer()
{
    if (server) {
        server->stop();
    }
    if (stopping.valid()) {
        stopping.wait();
    }
}

std::filesystem::path LocalServer::worldsDirectory() const
{
    return directory / "worlds";
}

std::optional<std::string> LocalServer::open(const std::string& world, const menu::WorldSettings& settings)
{
    if (server || closing()) {
        return std::nullopt;
    }
    std::error_code error;
    std::filesystem::create_directories(worldsDirectory(), error);

    EmbeddedServerConfig config;
    config.dataDirectory = directory;
    config.levelName = world;
    config.properties["gamemode"] = GameModes[settings.gameType >= 0 && settings.gameType < 3 ? settings.gameType : 0];
    config.properties["difficulty"] = Difficulties[settings.difficulty >= 0 && settings.difficulty < 4 ? settings.difficulty : 2];
    config.properties["allow-cheats"] = settings.cheats ? "true" : "false";
    config.log = [](LogLevel, const std::string& message) {
        debugLog("local server: " + message);
    };

    server = std::make_unique<EmbeddedServer>();
    if (!server->start(config)) {
        server.reset();
        return std::nullopt;
    }
    openWorld = world;
    return "127.0.0.1:" + std::to_string(server->getPort());
}

void LocalServer::closeInBackground()
{
    if (!server) {
        return;
    }
    std::shared_ptr<EmbeddedServer> closingServer(std::move(server));
    stopping = std::async(std::launch::async, [closingServer] {
        closingServer->stop();
    });
}

bool LocalServer::closing()
{
    if (!stopping.valid()) {
        return false;
    }
    if (stopping.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return true;
    }
    stopping.get();
    openWorld.clear();
    return false;
}

void LocalServer::setPaused(bool paused)
{
    if (server) {
        server->setPaused(paused);
    }
}

bool LocalServer::running() const
{
    return server && server->isRunning();
}

}
