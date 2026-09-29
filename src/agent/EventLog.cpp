#include "agent/EventLog.h"

#include "agent/JsonWriter.h"
#include "client/Session.h"

namespace kestrel::agent {

void EventLog::add(std::string_view kind, std::string_view details)
{
    Entry& entry = entries.emplace_back();
    entry.sequence = ++sequence;
    JsonWriter writer;
    writer.beginObject().field("seq", entry.sequence).field("time", secondsNow()).field("kind", kind);
    std::string json = writer.take();
    if (details.size() > 2 && details.front() == '{') {
        json.push_back(',');
        json += details.substr(1);
    } else {
        json.push_back('}');
    }
    entry.json = std::move(json);
    while (entries.size() > Capacity) {
        entries.pop_front();
    }
}

std::string EventLog::since(uint64_t after, size_t limit) const
{
    std::string json = "[";
    size_t count = 0;
    for (const Entry& entry : entries) {
        if (entry.sequence <= after) {
            continue;
        }
        if (count++ > 0) {
            json.push_back(',');
        }
        json += entry.json;
        if (count >= limit) {
            break;
        }
    }
    return json + "]";
}

}
