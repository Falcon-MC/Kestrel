#include "agent/AgentServer.h"

#include "agent/JsonWriter.h"

#include <algorithm>
#include <fstream>
#include <random>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
using SocketHandle = SOCKET;
constexpr SocketHandle NoSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle NoSocket = -1;
#endif

namespace kestrel::agent {

namespace {

// A screenshot request is tiny, anything this long is somebody misbehaving.
constexpr size_t MaxLineBytes = 1 << 20;

void closeSocket(SocketHandle socket)
{
#if defined(_WIN32)
    closesocket(socket);
#else
    ::close(socket);
#endif
}

bool sendAll(SocketHandle socket, const std::string& data)
{
    size_t sent = 0;
    while (sent < data.size()) {
#if defined(_WIN32)
        int wrote = ::send(socket, data.data() + sent, static_cast<int>(std::min<size_t>(data.size() - sent, 1 << 20)), 0);
#else
#if defined(MSG_NOSIGNAL)
        ssize_t wrote = ::send(socket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else
        ssize_t wrote = ::send(socket, data.data() + sent, data.size() - sent, 0);
#endif
#endif
        if (wrote <= 0) {
            return false;
        }
        sent += static_cast<size_t>(wrote);
    }
    return true;
}

bool sameToken(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return difference == 0;
}

std::string idText(const json::Value* id)
{
    if (!id) {
        return "null";
    }
    JsonWriter writer;
    if (id->isString()) {
        writer.value(id->mString);
    } else if (id->isNumber()) {
        writer.value(static_cast<int64_t>(id->mNumber));
    } else {
        writer.null();
    }
    return writer.take();
}

}

struct AgentServer::Peer {
    uint64_t id = 0;
    SocketHandle socket = NoSocket;
    std::string buffer;
    bool authorized = false;
    std::mutex sending;
};

AgentServer::AgentServer(uint16_t port, std::string token)
    : secret(std::move(token))
{
#if defined(_WIN32)
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("Agent server: WSAStartup failed");
    }
#endif
    if (secret.empty()) {
        secret = randomToken();
    }
    SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == NoSocket) {
        throw std::runtime_error("Agent server: could not create a socket");
    }
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(socket, 4) != 0) {
        closeSocket(socket);
        throw std::runtime_error("Agent server: could not listen on 127.0.0.1:" + std::to_string(port));
    }
    socklen_t length = sizeof(address);
    getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length);
    boundPort = ntohs(address.sin_port);
    listener = static_cast<intptr_t>(socket);
    worker = std::thread([this] { serve(); });
}

AgentServer::~AgentServer()
{
    stopping = true;
    if (worker.joinable()) {
        worker.join();
    }
    for (auto& [id, peer] : peers) {
        closeSocket(peer->socket);
    }
    closeSocket(static_cast<SocketHandle>(listener));
#if defined(_WIN32)
    WSACleanup();
#endif
}

std::string AgentServer::randomToken()
{
    static constexpr char Hex[] = "0123456789abcdef";
    std::random_device device;
    std::string token;
    for (int i = 0; i < 32; ++i) {
        token.push_back(Hex[device() & 0xF]);
    }
    return token;
}

void AgentServer::publish(const std::filesystem::path& file) const
{
    JsonWriter writer;
    writer.beginObject().field("port", boundPort).field("token", secret);
#if defined(_WIN32)
    writer.field("pid", static_cast<uint64_t>(GetCurrentProcessId()));
#else
    writer.field("pid", static_cast<int64_t>(getpid()));
#endif
    writer.endObject();
    std::string text = writer.take();
    std::error_code error;
    std::filesystem::remove(file, error);
    {
        std::ofstream create(file, std::ios::binary);
    }
    std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, error);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text << '\n';
}

std::vector<Request> AgentServer::takeRequests()
{
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<Request> taken = std::move(pending);
    pending.clear();
    return taken;
}

void AgentServer::respond(const Request& request, const std::string& resultJson)
{
    std::string line = "{\"id\":" + request.id + ",\"result\":" + (resultJson.empty() ? "null" : resultJson) + "}\n";
    send(request.connection, std::move(line));
}

void AgentServer::fail(const Request& request, const std::string& message)
{
    JsonWriter writer;
    writer.beginObject().key("id").raw(request.id).key("error").beginObject().field("message", message).endObject().endObject();
    send(request.connection, writer.take() + "\n");
}

void AgentServer::send(uint64_t connection, std::string line)
{
    std::shared_ptr<Peer> peer;
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto found = peers.find(connection);
        if (found == peers.end()) {
            return;
        }
        peer = found->second;
    }
    std::lock_guard<std::mutex> guard(peer->sending);
    sendAll(peer->socket, line);
}

void AgentServer::closePeer(uint64_t connection)
{
    std::lock_guard<std::mutex> guard(mutex);
    auto found = peers.find(connection);
    if (found == peers.end()) {
        return;
    }
    std::lock_guard<std::mutex> sending(found->second->sending);
    closeSocket(found->second->socket);
    peers.erase(found);
}

void AgentServer::serve()
{
    while (!stopping) {
        std::vector<std::shared_ptr<Peer>> watched;
        {
            std::lock_guard<std::mutex> guard(mutex);
            for (auto& [id, peer] : peers) {
                watched.push_back(peer);
            }
        }
#if defined(_WIN32)
        std::vector<WSAPOLLFD> polls;
#else
        std::vector<pollfd> polls;
#endif
        polls.push_back({ static_cast<SocketHandle>(listener), POLLIN, 0 });
        for (const std::shared_ptr<Peer>& peer : watched) {
            polls.push_back({ peer->socket, POLLIN, 0 });
        }
#if defined(_WIN32)
        int ready = WSAPoll(polls.data(), static_cast<ULONG>(polls.size()), 100);
#else
        int ready = ::poll(polls.data(), polls.size(), 100);
#endif
        if (ready <= 0) {
            continue;
        }
        if (polls[0].revents & POLLIN) {
            accept();
        }
        for (size_t i = 0; i < watched.size(); ++i) {
            if (polls[i + 1].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (!readFrom(*watched[i])) {
                    closePeer(watched[i]->id);
                }
            }
        }
    }
}

void AgentServer::accept()
{
    SocketHandle socket = ::accept(static_cast<SocketHandle>(listener), nullptr, nullptr);
    if (socket == NoSocket) {
        return;
    }
    int on = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
#if defined(SO_NOSIGPIPE)
    // macOS has no MSG_NOSIGNAL, a closed agent would kill the game with SIGPIPE otherwise.
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    auto peer = std::make_shared<Peer>();
    peer->socket = socket;
    std::lock_guard<std::mutex> guard(mutex);
    peer->id = nextConnection++;
    peers[peer->id] = std::move(peer);
}

bool AgentServer::readFrom(Peer& peer)
{
    char chunk[16384];
#if defined(_WIN32)
    int received = ::recv(peer.socket, chunk, sizeof(chunk), 0);
#else
    ssize_t received = ::recv(peer.socket, chunk, sizeof(chunk), 0);
#endif
    if (received <= 0) {
        return false;
    }
    peer.buffer.append(chunk, static_cast<size_t>(received));
    size_t start = 0;
    for (size_t newline = peer.buffer.find('\n'); newline != std::string::npos; newline = peer.buffer.find('\n', start)) {
        std::string line = peer.buffer.substr(start, newline - start);
        start = newline + 1;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            handleLine(peer, line);
        }
    }
    peer.buffer.erase(0, start);
    return peer.buffer.size() <= MaxLineBytes;
}

void AgentServer::handleLine(Peer& peer, const std::string& line)
{
    std::unique_ptr<json::Value> root = json::parse(line);
    Request request;
    request.connection = peer.id;
    if (!root || !root->isObject() || !root->get("method") || !root->get("method")->isString()) {
        request.id = root && root->isObject() ? idText(root->get("id")) : "null";
        fail(request, "Expected {\"id\", \"method\", \"params\"}");
        return;
    }
    request.id = idText(root->get("id"));
    request.method = root->get("method")->mString;
    if (const json::Value* params = root->get("params")) {
        request.params = params->clone();
    }
    if (!peer.authorized) {
        const json::Value* token = request.param("token");
        if (request.method != "hello" || !token || !token->isString() || !sameToken(token->mString, secret)) {
            fail(request, "Say hello with the agent token first");
            return;
        }
        peer.authorized = true;
    }
    std::lock_guard<std::mutex> guard(mutex);
    pending.push_back(std::move(request));
}

}
