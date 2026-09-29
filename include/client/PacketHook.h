#pragma once

#include <string>

namespace kestrel {

/**
 * Lets something outside the session look at, rewrite or drop game packets
 * as their raw payloads, header included. Called on the network thread.
 */
class PacketHook {
public:
    virtual ~PacketHook() = default;

    // False drops the packet.
    virtual bool inbound(int id, std::string& payload) = 0;
    virtual bool outbound(int id, std::string& payload) = 0;

    /**
     * Outgoing packets have to be serialized first to be shown, which the
     * session skips unless this says someone looks.
     */
    virtual bool wantsOutbound() const = 0;
};

}
