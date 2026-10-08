#pragma once

#include "modding/ModSupport.h"

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
    mod::Subscription addTyped(size_t owner, std::shared_ptr<mod::TypedPacketFilter> filter);
    void release(size_t owner);

    void observe(bool inbound, bool outbound);
    std::vector<ObservedPacket> takeObserved();

    /**
     * Queues a typed packet to be encoded and sent from the network thread.
     */
    void sendTyped(const mod::PacketView& packet);

    bool inbound(int id, std::string& payload) override;
    bool outbound(int id, std::string& payload) override;
    bool wantsOutbound() const override;
    void attachCodec(const PacketCodecContext* context) override;
    std::vector<std::string> takeOutgoing() override;

private:
    struct Entry {
        size_t owner = 0;
        std::shared_ptr<mod::PacketFilter> filter;
        std::shared_ptr<mod::TypedPacketFilter> typed;
        std::shared_ptr<Handle> handle;
    };

    bool pass(bool outbound, int id, std::string& payload);
    bool passTyped(bool outbound, int id, std::string& payload);
    void remove(const Entry* entry);
    void refresh();

    // Only a few seconds' worth of traffic waits for the main thread.
    static constexpr size_t ObservedLimit = 8192;
    static constexpr size_t OutgoingLimit = 256;

    ErrorSink errors;
    // Held while filters run, so unloading a mod waits for its filter to return.
    mutable std::recursive_mutex mutex;
    std::vector<std::shared_ptr<Entry>> entries;
    std::vector<std::shared_ptr<Entry>> typedEntries;
    std::atomic<bool> filtering { false };
    std::atomic<bool> typedFiltering { false };
    std::atomic<bool> observeInbound { false };
    std::atomic<bool> observeOutbound { false };
    std::mutex observedMutex;
    std::vector<ObservedPacket> observed;
    // Only touched on the network thread.
    const PacketCodecContext* codec = nullptr;
    std::mutex outgoingMutex;
    std::vector<mod::PacketView> outgoing;
};

}
