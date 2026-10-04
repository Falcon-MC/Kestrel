#pragma once

#include "modding/ModSupport.h"

#include "mod/Commands.h"

#include <memory>
#include <string>
#include <string_view>
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

    std::vector<std::shared_ptr<const Entry>> list() const;
    void release(size_t owner);

    static std::vector<std::string> splitArguments(std::string_view text);

private:
    std::shared_ptr<Entry> find(std::string_view name) const;
    void remove(const Entry* entry);

    std::vector<std::shared_ptr<Entry>> entries;
};

}
