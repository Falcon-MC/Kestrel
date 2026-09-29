#pragma once

#include "Core/Json/Json.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kestrel::agent {

/**
 * A call from an automation client: the connection it came in on, its id as
 * JSON text so the answer can echo it, the method and its parameters.
 */
struct Request {
    uint64_t connection = 0;
    std::string id;
    std::string method;
    std::unique_ptr<json::Value> params;

    const json::Value* param(const std::string& name) const
    {
        return params && params->isObject() ? params->get(name) : nullptr;
    }
};

/**
 * The local control port that lets an agent drive Kestrel. It speaks one
 * JSON object per line, {"id", "method", "params"} in and {"id", "result"}
 * or {"id", "error"} out, on 127.0.0.1 only. A connection must open with
 * "hello" and the token before anything else is taken from it.
 *
 * Requests pile up on a background thread; the owner takes them on its own
 * thread, so handlers can touch game state without locks.
 */
class AgentServer {
public:
    AgentServer(uint16_t port, std::string token);
    ~AgentServer();

    AgentServer(const AgentServer&) = delete;
    AgentServer& operator=(const AgentServer&) = delete;

    uint16_t port() const
    {
        return boundPort;
    }

    const std::string& token() const
    {
        return secret;
    }

    std::vector<Request> takeRequests();
    void respond(const Request& request, const std::string& resultJson);
    void fail(const Request& request, const std::string& message);

    /**
     * Writes where to reach this server, for clients that attach to a
     * running Kestrel, readable only by the current user.
     */
    void publish(const std::filesystem::path& file) const;

    static std::string randomToken();

private:
    struct Peer;

    void serve();
    void accept();
    bool readFrom(Peer& peer);
    void handleLine(Peer& peer, const std::string& line);
    void send(uint64_t connection, std::string line);
    void closePeer(uint64_t connection);

    std::string secret;
    uint16_t boundPort = 0;
    intptr_t listener = -1;
    std::atomic<bool> stopping { false };
    std::thread worker;
    std::mutex mutex;
    std::map<uint64_t, std::shared_ptr<Peer>> peers;
    std::vector<Request> pending;
    uint64_t nextConnection = 1;
};

}
