#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kestrel::mod {

/**
 * Text in and out. Section sign colour codes (§a, §l, ...) work everywhere.
 */
class Chat {
public:
    virtual ~Chat() = default;

    /**
     * Sends a message or, starting with a slash, a command to the server.
     * This skips ChatSendEvent so mods cannot loop through each other.
     */
    virtual void send(std::string_view text) = 0;

    /**
     * Adds a line to the local chat only; the server never sees it.
     */
    virtual void print(std::string_view text) = 0;
    virtual void toast(std::string_view title, std::string_view content) = 0;
    virtual void title(std::string_view title, std::string_view subtitle = {}) = 0;
    virtual void actionbar(std::string_view text) = 0;

    /**
     * The chat log, oldest line first.
     */
    virtual std::vector<std::string> history() const = 0;
};

}
