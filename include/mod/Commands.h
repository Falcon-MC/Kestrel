#pragma once

#include "mod/Chat.h"
#include "mod/Event.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace kestrel::mod {

/**
 * Throw from a command to print the message in red and stop there.
 */
class CommandError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CommandContext {
    std::string label;
    std::vector<std::string> args;
    // Everything after the command name, as typed.
    std::string rawArgs;
    Chat& chat;

    void reply(std::string_view text) const
    {
        chat.print(text);
    }

    const std::string& arg(size_t index) const
    {
        if (index >= args.size()) {
            throw CommandError("Missing argument " + std::to_string(index + 1));
        }
        return args[index];
    }

    int intArg(size_t index) const
    {
        const std::string& text = arg(index);
        try {
            size_t used = 0;
            int value = std::stoi(text, &used);
            if (used == text.size()) {
                return value;
            }
        } catch (const std::exception&) {
        }
        throw CommandError("Not a whole number: " + text);
    }

    double numberArg(size_t index) const
    {
        const std::string& text = arg(index);
        try {
            size_t used = 0;
            double value = std::stod(text, &used);
            if (used == text.size()) {
                return value;
            }
        } catch (const std::exception&) {
        }
        throw CommandError("Not a number: " + text);
    }
};

struct CommandSpec {
    std::string name;
    std::string description;
    // Shown by .help after the name, like "<player> [seconds]".
    std::string usage;
    std::vector<std::string> aliases;
};

/**
 * Client side commands, typed in chat with a dot in front (.name args). They
 * never reach the server. Arguments split on spaces, quotes keep them
 * together.
 */
class Commands {
public:
    using Handler = std::function<void(CommandContext&)>;

    static constexpr char Prefix = '.';

    virtual ~Commands() = default;

    virtual Subscription add(CommandSpec spec, Handler handler) = 0;

    Subscription add(std::string name, std::string description, Handler handler)
    {
        return add(CommandSpec { std::move(name), std::move(description), {}, {} }, std::move(handler));
    }
};

}
