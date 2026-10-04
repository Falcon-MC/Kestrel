#include "modding/PacketFilters.h"

#include <algorithm>

namespace kestrel::modding {

PacketFilters::PacketFilters(ErrorSink errors)
    : errors(std::move(errors))
{
}

PacketFilters::~PacketFilters()
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    for (const std::shared_ptr<Entry>& entry : entries) {
        entry->handle->expire();
    }
}

mod::Subscription PacketFilters::add(size_t owner, std::shared_ptr<mod::PacketFilter> filter)
{
    auto entry = std::make_shared<Entry>();
    entry->owner = owner;
    entry->filter = std::move(filter);
    entry->handle = std::make_shared<Handle>([this, raw = entry.get()] { remove(raw); });
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if (entry->filter) {
        entries.push_back(entry);
        filtering = true;
    }
    return mod::Subscription(entry->handle);
}

void PacketFilters::release(size_t owner)
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    std::erase_if(entries, [owner](const std::shared_ptr<Entry>& entry) {
        if (entry->owner != owner) {
            return false;
        }
        entry->handle->expire();
        return true;
    });
    filtering = !entries.empty();
}

void PacketFilters::observe(bool inbound, bool outbound)
{
    observeInbound = inbound;
    observeOutbound = outbound;
    if (!inbound && !outbound) {
        std::lock_guard<std::mutex> guard(observedMutex);
        observed.clear();
    }
}

std::vector<ObservedPacket> PacketFilters::takeObserved()
{
    std::vector<ObservedPacket> taken;
    std::lock_guard<std::mutex> guard(observedMutex);
    taken.swap(observed);
    return taken;
}

bool PacketFilters::inbound(int id, std::string& payload)
{
    return pass(false, id, payload);
}

bool PacketFilters::outbound(int id, std::string& payload)
{
    return pass(true, id, payload);
}

bool PacketFilters::wantsOutbound() const
{
    return filtering || observeOutbound;
}

bool PacketFilters::pass(bool outbound, int id, std::string& payload)
{
    if (filtering) {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        // A filter may add or cancel filters, so walk a copy.
        std::vector<std::shared_ptr<Entry>> current = entries;
        for (const std::shared_ptr<Entry>& entry : current) {
            if (!entry->handle->active()) {
                continue;
            }
            bool keep = true;
            guarded(errors, entry->owner, [&] { keep = outbound ? entry->filter->outbound(id, payload) : entry->filter->inbound(id, payload); });
            if (!keep) {
                return false;
            }
        }
    }
    if (outbound ? observeOutbound : observeInbound) {
        std::lock_guard<std::mutex> guard(observedMutex);
        if (observed.size() < ObservedLimit) {
            observed.push_back({ outbound, id, payload });
        }
    }
    return true;
}

void PacketFilters::remove(const Entry* entry)
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    std::erase_if(entries, [entry](const std::shared_ptr<Entry>& other) { return other.get() == entry; });
    filtering = !entries.empty();
}

}
