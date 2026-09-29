#include "menu/ChatCommands.h"

#include "ui/Localization.h"

#include <algorithm>
#include <cctype>

namespace kestrel::menu {

namespace {

constexpr std::string_view Gray = "\xC2\xA7" "7";
constexpr std::string_view White = "\xC2\xA7" "f";

struct Word {
    std::string_view text;
    size_t start = 0;
};

bool sameLetter(char a, char b)
{
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
}

bool startsWith(std::string_view text, std::string_view prefix)
{
    return prefix.size() <= text.size() && std::equal(prefix.begin(), prefix.end(), text.begin(), sameLetter);
}

bool equalWords(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && startsWith(a, b);
}

bool containsWord(std::string_view text, std::string_view part)
{
    return std::search(text.begin(), text.end(), part.begin(), part.end(), sameLetter) != text.end();
}

struct Selector {
    const char* text;
    const char* key;
    const char* fallback;
};

constexpr Selector Selectors[] = {
    { "@a", "commands.autocomplete.a", "all players" },
    { "@e", "commands.autocomplete.e", "all entities" },
    { "@n", "commands.autocomplete.n", "nearest entity" },
    { "@p", "commands.autocomplete.p", "closest player" },
    { "@r", "commands.autocomplete.r", "random player" },
    { "@s", "commands.autocomplete.s", "yourself" },
};

/**
 * The values an argument can take that contain what is typed so far:
 * its enum choices, or for targets the selectors and the players online.
 */
void argumentSuggestions(const ChatParameter& parameter, const std::vector<std::string>& players, std::string_view typed, std::vector<CommandSuggestion>& out)
{
    for (const std::string& value : parameter.values) {
        if (containsWord(value, typed)) {
            out.push_back({ value, {} });
        }
    }
    if (!parameter.target) {
        return;
    }
    for (const Selector& selector : Selectors) {
        if (containsWord(selector.text, typed)) {
            out.push_back({ selector.text, ui::tr(selector.key, selector.fallback) });
        }
    }
    for (const std::string& player : players) {
        if (containsWord(player, typed)) {
            out.push_back({ player.find(' ') == std::string::npos ? player : "\"" + player + "\"", {} });
        }
    }
}

/**
 * Splits the text after the slash into words, a quoted string being one word.
 * open is set when the last word is still being typed.
 */
std::vector<Word> splitWords(std::string_view draft, bool& open)
{
    std::vector<Word> words;
    size_t i = 1;
    open = false;
    while (i < draft.size()) {
        if (draft[i] == ' ') {
            ++i;
            continue;
        }
        size_t start = i;
        if (draft[i] == '"') {
            size_t close = draft.find('"', i + 1);
            i = close == std::string_view::npos ? draft.size() : close + 1;
        }
        while (i < draft.size() && draft[i] != ' ') {
            ++i;
        }
        words.push_back({ draft.substr(start, i - start), start });
    }
    open = !words.empty() && words.back().start + words.back().text.size() == draft.size();
    return words;
}

std::string parameterText(const ChatParameter& parameter)
{
    if (parameter.literal) {
        return parameter.values.front();
    }
    std::string text(parameter.optional ? "[" : "<");
    text += parameter.name + ": " + parameter.type;
    text += parameter.optional ? "]" : ">";
    return text;
}

std::string usageLine(const std::string& name, const ChatOverload& overload, size_t current)
{
    std::string line = std::string(Gray) + "/" + name;
    for (size_t i = 0; i < overload.parameters.size(); ++i) {
        line += " ";
        line += i == current ? White : Gray;
        line += parameterText(overload.parameters[i]);
    }
    return line;
}

const ChatCommand* findCommand(const std::vector<ChatCommand>& commands, std::string_view name)
{
    for (const ChatCommand& command : commands) {
        if (equalWords(command.name, name)) {
            return &command;
        }
        for (const std::string& alias : command.aliases) {
            if (equalWords(alias, name)) {
                return &command;
            }
        }
    }
    return nullptr;
}

void sortUnique(std::vector<CommandSuggestion>& values)
{
    std::stable_sort(values.begin(), values.end(), [](const CommandSuggestion& a, const CommandSuggestion& b) { return a.text < b.text; });
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

}

CommandHints commandHints(const std::vector<ChatCommand>& commands, const std::vector<std::string>& players, std::string_view draft)
{
    CommandHints hints;
    if (draft.empty() || draft.front() != '/') {
        return hints;
    }
    bool open = false;
    std::vector<Word> words = splitWords(draft, open);

    if (words.empty() || (words.size() == 1 && open)) {
        std::string_view typed = words.empty() ? std::string_view() : words.front().text;
        hints.replaceFrom = 1;
        for (const ChatCommand& command : commands) {
            if (startsWith(command.name, typed)) {
                hints.suggestions.push_back({ command.name, command.description });
            }
            for (const std::string& alias : command.aliases) {
                if (startsWith(alias, typed)) {
                    hints.suggestions.push_back({ alias, command.description });
                }
            }
        }
        sortUnique(hints.suggestions);
        return hints;
    }

    const ChatCommand* command = findCommand(commands, words.front().text);
    if (!command) {
        return hints;
    }
    size_t complete = words.size() - 1 - (open ? 1 : 0);
    std::string_view typed = open ? words.back().text : std::string_view();
    hints.replaceFrom = open ? words.back().start : draft.size();

    for (const ChatOverload& overload : command->overloads) {
        const std::vector<ChatParameter>& parameters = overload.parameters;
        size_t word = 0;
        size_t current = 0;
        bool fits = true;
        while (word < complete && current < parameters.size()) {
            const ChatParameter& parameter = parameters[current];
            if (parameter.rest) {
                word = complete;
                break;
            }
            if (!parameter.values.empty()) {
                std::string_view value = words[1 + word].text;
                fits = std::any_of(parameter.values.begin(), parameter.values.end(), [&](const std::string& choice) { return equalWords(choice, value); });
                if (!fits) {
                    break;
                }
            }
            word += parameter.words;
            ++current;
        }
        if (!fits || (current >= parameters.size() && word < complete)) {
            continue;
        }
        bool insideArgument = word > complete;
        if (insideArgument) {
            --current;
        }
        hints.usage.push_back(usageLine(command->name, overload, current));
        if (!insideArgument && current < parameters.size()) {
            argumentSuggestions(parameters[current], players, typed, hints.suggestions);
        }
    }
    sortUnique(hints.suggestions);
    return hints;
}

}
