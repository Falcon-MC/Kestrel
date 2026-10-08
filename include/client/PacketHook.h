#pragma once

#include <string>
#include <vector>

class PacketCodecContext;

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

    /**
     * The codec the connection encodes and decodes with, handed over each
     * time the session joins a server; it stays valid until the next one.
     */
    virtual void attachCodec(const PacketCodecContext* context) = 0;

    /**
     * Payloads, header included, the hook wants sent; taken on the network
     * thread, where they go out as they are.
     */
    virtual std::vector<std::string> takeOutgoing() = 0;
};

}
