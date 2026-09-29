#include "client/PacketJournal.h"

#include "client/Session.h"

#include "Protocol/MinecraftPacketIds.h"

namespace kestrel {

void PacketJournal::configure(Settings value)
{
    std::lock_guard<std::mutex> guard(mutex);
    current = std::move(value);
    while (records.size() > current.capacity) {
        records.pop_front();
    }
}

PacketJournal::Settings PacketJournal::settings() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return current;
}

bool PacketJournal::recording() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return current.recording;
}

bool PacketJournal::wanted(int id) const
{
    if (!current.only.empty() && !current.only.count(id)) {
        return false;
    }
    return !current.ignore.count(id);
}

void PacketJournal::record(bool outbound, int id, const std::string& payload)
{
    std::lock_guard<std::mutex> guard(mutex);
    PacketTally& tally = counts[{ outbound, id }];
    ++tally.count;
    tally.bytes += payload.size();
    if (!current.recording || current.capacity == 0 || !wanted(id)) {
        return;
    }
    PacketRecord& entry = records.emplace_back();
    entry.sequence = ++sequence;
    entry.time = secondsNow();
    entry.outbound = outbound;
    entry.id = id;
    entry.name = id >= 0 ? toString(static_cast<MinecraftPacketIds>(id)) : "raw";
    entry.size = payload.size();
    entry.head = payload.substr(0, current.headBytes);
    while (records.size() > current.capacity) {
        records.pop_front();
    }
}

void PacketJournal::clear()
{
    std::lock_guard<std::mutex> guard(mutex);
    records.clear();
    counts.clear();
}

std::vector<PacketRecord> PacketJournal::since(uint64_t after, size_t limit) const
{
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<PacketRecord> found;
    for (const PacketRecord& entry : records) {
        if (entry.sequence <= after) {
            continue;
        }
        found.push_back(entry);
        if (found.size() >= limit) {
            break;
        }
    }
    return found;
}

std::map<std::pair<bool, int>, PacketTally> PacketJournal::tallies() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return counts;
}

uint64_t PacketJournal::lastSequence() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return sequence;
}

}
