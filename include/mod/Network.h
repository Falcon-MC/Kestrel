#pragma once

#include "mod/Event.h"

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
};

}
