#pragma once

#include "ModSupport.h"

#include "client/PacketHook.h"
#include "mod/Network.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace kestrel::modding {

struct ObservedPacket {
    bool outbound = false;
    int id = 0;
    std::string payload;
};

/**
 * The session's packet hook: runs the mods' filters on the network thread and
 * keeps copies of the packets for the main thread's packet events while
 * someone listens to them.
 */
class PacketFilters final : public PacketHook {
public:
    explicit PacketFilters(ErrorSink errors);
    ~PacketFilters() override;

    mod::Subscription add(size_t owner, std::shared_ptr<mod::PacketFilter> filter);
    void release(size_t owner);

    void observe(bool inbound, bool outbound);
    std::vector<ObservedPacket> takeObserved();

    bool inbound(int id, std::string& payload) override;
    bool outbound(int id, std::string& payload) override;
    bool wantsOutbound() const override;

private:
    struct Entry {
        size_t owner = 0;
        std::shared_ptr<mod::PacketFilter> filter;
        std::shared_ptr<Handle> handle;
    };

    bool pass(bool outbound, int id, std::string& payload);
    void remove(const Entry* entry);

    // Only a few seconds' worth of traffic waits for the main thread.
    static constexpr size_t ObservedLimit = 8192;

    ErrorSink errors;
    // Held while filters run, so unloading a mod waits for its filter to return.
    mutable std::recursive_mutex mutex;
    std::vector<std::shared_ptr<Entry>> entries;
    std::atomic<bool> filtering { false };
    std::atomic<bool> observeInbound { false };
    std::atomic<bool> observeOutbound { false };
    std::mutex observedMutex;
    std::vector<ObservedPacket> observed;
};

}
