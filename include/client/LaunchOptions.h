#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kestrel {

/**
 * What the command line asked for. Everything is optional; a plain launch
 * opens the start screen like before.
 *
 *   --connect <address>     join a server (or realm:<id>) straight away
 *   --name <name>           the offline player name when not signed in
 *   --agent                 open the agent control port on a free port
 *   --agent-port <port>     open it on this port
 *   --hidden                keep the window off screen and draw offscreen
 *   --headless              no window and no rendering, only the connection
 *   --size <width>x<height> the window size
 *
 * The agent token comes from KESTREL_AGENT_TOKEN rather than the command
 * line, so other users cannot read it from the process list.
 */
struct LaunchOptions {
    std::optional<std::string> connect;
    std::string name;
    bool agent = false;
    uint16_t agentPort = 0;
    std::string agentToken;
    bool hidden = false;
    bool headless = false;
    uint32_t width = 1280;
    uint32_t height = 760;

    static LaunchOptions parse(const std::vector<std::string>& arguments);
};

}
