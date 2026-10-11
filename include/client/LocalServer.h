#pragma once

#include "menu/Menu.h"

#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>

class EmbeddedServer;

namespace kestrel {

/**
 * Plays a world on a server running inside Kestrel. The session joins it like any other server, over the
 * loopback address and the port the server picked, without the Microsoft account or encryption.
 */
class LocalServer {
public:
    explicit LocalServer(std::filesystem::path directory);
    ~LocalServer();

    /**
     * Where the server keeps its worlds, one folder per world id.
     */
    std::filesystem::path worldsDirectory() const;

    /**
     * Starts the server on the world with that folder id, with the world's own game mode, difficulty and cheats,
     * and returns the address to join, or nothing when the server could not start.
     */
    std::optional<std::string> open(const std::string& world, const menu::WorldSettings& settings);

    /**
     * Saves the world and stops the server on a background thread, so the menu keeps drawing meanwhile.
     */
    void closeInBackground();

    /**
     * Whether a server is still saving after closeInBackground.
     */
    bool closing();

    void setPaused(bool paused);
    bool running() const;
    const std::string& world() const
    {
        return openWorld;
    }

private:
    std::filesystem::path directory;
    std::unique_ptr<EmbeddedServer> server;
    std::future<void> stopping;
    std::string openWorld;
};

}
