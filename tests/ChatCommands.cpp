#include "menu/ChatCommands.h"

#include <cstdio>
#include <cstdlib>

using namespace kestrel::menu;

void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main()
{
    const std::vector<ChatCommand> commands {
        { "gamemode", "Change game mode", { "gm", "game_mode" }, {} },
        { "time", "Change time", {}, {} },
    };
    auto hints = commandHints(commands, {}, "/mode");
    require(hints.replaceFrom == 1, "Substring completion must replace the entire command name");
    require(hints.suggestions == std::vector<CommandSuggestion> {
        { "game_mode", "Change game mode" }, { "gamemode", "Change game mode" }
    }, "Command names and aliases must match a substring");
    require(commandHints(commands, {}, "/MODE").suggestions == hints.suggestions, "Substring matching must ignore letter case");
    require(commandHints(commands, {}, "/gm").suggestions.size() == 1, "Prefix aliases must still match");
    require(commandHints(commands, {}, "/missing").suggestions.empty(), "Unmatched input must produce no suggestions");
    require(commandHints(commands, {}, "mode").suggestions.empty(), "Ordinary chat must not produce command suggestions");
    require(commandHints(commands, {}, "/").suggestions.size() == 4, "Empty command input must list all commands and aliases");
}
