#pragma once

#include "modding/ModSupport.h"

#include "mod/Commands.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kestrel::modding {

class CommandRegistry {
public:
    struct Entry {
        size_t owner = 0;
        mod::CommandSpec spec;
        mod::Commands::Handler handler;
        std::shared_ptr<Handle> handle;
    };

    /**
     * What Tab can put in for a typed dot command: suggestions as text and
     * description, replacing the line from replaceFrom on, and the usage of
     * the command being typed.
     */
    struct Completions {
        std::vector<std::pair<std::string, std::string>> suggestions;
        std::vector<std::string> usage;
        size_t replaceFrom = 0;
    };

    static constexpr size_t MaxSuggestions = 64;

    CommandRegistry() = default;
    ~CommandRegistry();

    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;

    mod::Subscription add(size_t owner, mod::CommandSpec spec, mod::Commands::Handler handler);

    /**
     * Runs line when it is a dot command some mod knows; false leaves it for
     * the server, so chat that merely starts with a dot still goes out.
     */
    bool execute(std::string_view line, mod::Chat& chat, const ErrorSink& errors);

    /**
     * Completions for line typed up to the caret: command names while the
     * first word is typed, then whatever the command's complete function
     * suggests for the argument being typed.
     */
    Completions complete(std::string_view line, mod::Chat& chat, const ErrorSink& errors) const;

    std::vector<std::shared_ptr<const Entry>> list() const;
    void release(size_t owner);

    static std::vector<std::string> splitArguments(std::string_view text);

private:
    std::shared_ptr<Entry> find(std::string_view name) const;
    void remove(const Entry* entry);

    std::vector<std::shared_ptr<Entry>> entries;
};

}
