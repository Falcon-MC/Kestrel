#pragma once

#include "modding/ModSupport.h"

#include "mod/Emotes.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kestrel::modding {

/**
 * The emotes the mods added, in the order they came, and what the mods asked
 * the player to play or stop since the client last looked.
 */
class EmoteRegistry {
public:
    struct Entry {
        size_t owner = 0;
        mod::EmoteSpec spec;
        // The lowercase name of the animation to play, as the animation library keys it.
        std::string clip;
        std::shared_ptr<Handle> handle;
    };

    EmoteRegistry() = default;
    ~EmoteRegistry();

    EmoteRegistry(const EmoteRegistry&) = delete;
    EmoteRegistry& operator=(const EmoteRegistry&) = delete;

    mod::Subscription add(size_t owner, mod::EmoteSpec spec, const ErrorSink& errors);
    void release(size_t owner);

    std::vector<std::shared_ptr<const Entry>> list() const;
    const Entry* find(const std::string& id) const;

    /**
     * Advances whenever an emote comes or goes.
     */
    uint64_t revision() const
    {
        return changes;
    }

    void requestPlay(std::string id);
    void requestStop();

    /**
     * What the mods asked for since the last call: an id to play, or an
     * empty string to stop.
     */
    std::optional<std::string> takeRequest();

    std::optional<std::string> playing;

private:
    void remove(const Entry* entry);

    std::vector<std::shared_ptr<Entry>> entries;
    std::optional<std::string> request;
    uint64_t changes = 0;
};

}
