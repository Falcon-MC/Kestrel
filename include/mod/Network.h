#pragma once

#include "mod/Event.h"
#include "mod/Packets.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kestrel::mod {

/**
 * Sees every game packet as its raw payload, header included, and can change
 * or drop it. Runs on the network thread, so keep it quick and use
 * Scheduler::post to reach the rest of the API.
 */
class PacketFilter {
public:
    virtual ~PacketFilter() = default;

    // False drops the packet.
    virtual bool inbound(int, std::string&)
    {
        return true;
    }

    virtual bool outbound(int, std::string&)
    {
        return true;
    }
};

/**
 * Sees every game packet as a PacketView, after the PacketFilters, and can
 * change or drop it. The packets mod/Packets.h describes are decoded with the
 * client's own codec and encoded again when a filter changes them; the others
 * come with only their id. Runs on the network thread like PacketFilter, so
 * keep it quick and use Scheduler::post to reach the rest of the API.
 */
class TypedPacketFilter {
public:
    virtual ~TypedPacketFilter() = default;

    // False drops the packet.
    virtual bool inboundTyped(PacketView&)
    {
        return true;
    }

    virtual bool outboundTyped(PacketView&)
    {
        return true;
    }
};

class Network {
public:
    virtual ~Network() = default;

    /**
     * Joins a server as if picked from the list; address is host:port or
     * realm:<id>.
     */
    virtual void connect(std::string_view address, std::string_view name = {}) = 0;
    virtual void disconnect() = 0;

    /**
     * Queues a raw game packet payload, header included, as it is.
     */
    virtual void sendRaw(std::string payload) = 0;

    /**
     * Answers a server form with its response JSON, or closes it with none.
     */
    virtual void answerForm(uint32_t id, std::optional<std::string> json) = 0;

    virtual Subscription addFilter(std::shared_ptr<PacketFilter> filter) = 0;
    virtual std::string packetName(int id) const = 0;
    virtual std::string_view gameVersion() const = 0;

    // Added in API 4 and kept last so older mods still find everything above.
    virtual Subscription addTypedFilter(std::shared_ptr<TypedPacketFilter> filter) = 0;

    /**
     * Queues one of the packets mod/Packets.h describes, encoded with the
     * client's own codec on the network thread. Fields the view leaves out
     * keep their protocol defaults; a view holding std::monostate is ignored.
     * It does not pass through the filters.
     */
    virtual void sendTyped(const PacketView& packet) = 0;

    /**
     * The connection's average round trip in milliseconds as its transport
     * measures it, or -1 when not in a world. Transports that do not measure
     * it report 0.
     */
    virtual int ping() const = 0;
};

}
