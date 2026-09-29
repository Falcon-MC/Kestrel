#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace kestrel {

/**
 * One packet that crossed the connection: its sequence number, which way it
 * went, its id and name, the size of its payload with the header, and the
 * first bytes of that payload when the journal keeps them.
 */
struct PacketRecord {
    uint64_t sequence = 0;
    double time = 0.0;
    bool outbound = false;
    int id = 0;
    std::string name;
    size_t size = 0;
    std::string head;
};

/**
 * Packet counts by id and direction since the journal was last cleared.
 */
struct PacketTally {
    uint64_t count = 0;
    uint64_t bytes = 0;
};

/**
 * Keeps the most recent packets of the session for debugging. Counting is
 * always on and cheap; the ring of records only fills while recording, and
 * can be narrowed to a set of packet ids.
 */
class PacketJournal {
public:
    struct Settings {
        bool recording = false;
        size_t capacity = 2000;
        size_t headBytes = 64;
        std::set<int> only;
        std::set<int> ignore;
    };

    void configure(Settings value);
    Settings settings() const;
    bool recording() const;
    void record(bool outbound, int id, const std::string& payload);
    void clear();

    std::vector<PacketRecord> since(uint64_t sequence, size_t limit) const;
    std::map<std::pair<bool, int>, PacketTally> tallies() const;
    uint64_t lastSequence() const;

private:
    bool wanted(int id) const;

    mutable std::mutex mutex;
    Settings current;
    std::deque<PacketRecord> records;
    std::map<std::pair<bool, int>, PacketTally> counts;
    uint64_t sequence = 0;
};

}
