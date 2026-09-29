#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace kestrel::agent {

/**
 * What happened since an agent last looked: chat, forms, titles, toasts,
 * connection changes and the like, numbered so a poll can pick up where the
 * previous one stopped. Old events fall off the front.
 */
class EventLog {
public:
    /**
     * Adds an event; details is a JSON object or empty, and its fields join
     * the sequence, time and kind of the event.
     */
    void add(std::string_view kind, std::string_view details = {});

    /**
     * The events after sequence as a JSON array, at most limit of them.
     */
    std::string since(uint64_t sequence, size_t limit) const;

    uint64_t last() const
    {
        return sequence;
    }

private:
    struct Entry {
        uint64_t sequence = 0;
        std::string json;
    };

    static constexpr size_t Capacity = 4000;

    std::deque<Entry> entries;
    uint64_t sequence = 0;
};

}
