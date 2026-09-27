#pragma once

#include <memory>

namespace kestrel {

/** Publishes Minecraft activity to the local Discord client while the game runs. */
class DiscordPresence {
public:
    DiscordPresence();
    ~DiscordPresence();

    // Nonblocking; also reconnects when Discord starts or restarts.
    void update();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}
