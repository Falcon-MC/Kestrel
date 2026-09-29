#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kestrel::menu {

/**
 * One argument of a command overload as chat shows it: the label after the
 * colon (a type or enum name), the choices of an enum, and how many words it
 * takes. A literal is a one value enum, typed and shown as that value.
 */
struct ChatParameter {
    std::string name;
    std::string type;
    std::vector<std::string> values;
    bool optional = false;
    bool literal = false;
    bool target = false;
    size_t words = 1;
    bool rest = false;
};

struct ChatOverload {
    std::vector<ChatParameter> parameters;
};

/**
 * A command the server lets the player run, from its available commands.
 */
struct ChatCommand {
    std::string name;
    std::string description;
    std::vector<std::string> aliases;
    std::vector<ChatOverload> overloads;
};

/**
 * A completion for the word being typed, with the note the game shows next
 * to it (selectors say who they pick).
 */
struct CommandSuggestion {
    std::string text;
    std::string description;

    bool operator==(const CommandSuggestion&) const = default;
};

/**
 * What the chat screen lists above the box while a command is typed:
 * completions for the word under the caret, which start at replaceFrom in the
 * draft, and the syntax hint of every overload that still fits. The hint is
 * gray with the next argument to type in white, all gray once every argument
 * is in.
 */
struct CommandHints {
    std::vector<CommandSuggestion> suggestions;
    std::vector<std::string> usage;
    size_t replaceFrom = 0;
};

CommandHints commandHints(const std::vector<ChatCommand>& commands, const std::vector<std::string>& players, std::string_view draft);

}
