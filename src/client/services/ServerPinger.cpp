#include "client/ServerPinger.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

namespace kestrel {

namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket NoSocket = INVALID_SOCKET;

void closeSocket(Socket socket)
{
    closesocket(socket);
}
#else
using Socket = int;
constexpr Socket NoSocket = -1;

void closeSocket(Socket socket)
{
    close(socket);
}
#endif

constexpr uint16_t DefaultPort = 19132;
constexpr int Attempts = 3;
constexpr int AttemptMilliseconds = 1000;
constexpr std::array<uint8_t, 16> OfflineMagic { 0x00, 0xFF, 0xFF, 0x00, 0xFE, 0xFE, 0xFE, 0xFE, 0xFD, 0xFD, 0xFD, 0xFD, 0x12, 0x34, 0x56, 0x78 };
constexpr uint8_t UnconnectedPing = 0x01;
constexpr uint8_t UnconnectedPong = 0x1C;
constexpr auto RefreshInterval = std::chrono::seconds(30);

void putLong(std::vector<uint8_t>& out, uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>(value >> shift));
    }
}

/**
 * Splits "host:port" (or "[v6]:port") into host and port, falling back to
 * the default Bedrock port.
 */
std::pair<std::string, uint16_t> splitAddress(const std::string& address)
{
    std::string host = address;
    uint16_t port = DefaultPort;
    if (!host.empty() && host.front() == '[') {
        size_t close = host.find(']');
        if (close != std::string::npos) {
            if (close + 2 < host.size() && host[close + 1] == ':') {
                port = static_cast<uint16_t>(std::atoi(host.c_str() + close + 2));
            }
            host = host.substr(1, close - 1);
        }
    } else if (size_t colon = host.rfind(':'); colon != std::string::npos && host.find(':') == colon) {
        port = static_cast<uint16_t>(std::atoi(host.c_str() + colon + 1));
        host = host.substr(0, colon);
    }
    return { host, port == 0 ? DefaultPort : port };
}

/**
 * Parses "MCPE;motd;protocol;version;players;max;guid;submotd;..." into the
 * shown fields.
 */
ServerPing parsePong(const std::string& text)
{
    std::vector<std::string> fields;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        fields.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    ServerPing ping;
    ping.state = PingState::Online;
    if (fields.size() > 1) {
        ping.motd = fields[1];
    }
    if (fields.size() > 3) {
        ping.version = fields[3];
    }
    if (fields.size() > 5) {
        ping.players = std::atoi(fields[4].c_str());
        ping.maxPlayers = std::atoi(fields[5].c_str());
    }
    return ping;
}

std::optional<ServerPing> pingOnce(const std::string& address)
{
    auto [host, port] = splitAddress(address);
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* resolved = nullptr;
    std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &resolved) != 0 || !resolved) {
        return std::nullopt;
    }
    Socket socket = ::socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
    if (socket == NoSocket) {
        freeaddrinfo(resolved);
        return std::nullopt;
    }

    std::vector<uint8_t> ping;
    ping.push_back(UnconnectedPing);
    putLong(ping, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
    ping.insert(ping.end(), OfflineMagic.begin(), OfflineMagic.end());
    putLong(ping, 0x4B455354524C0001ull);

    std::optional<ServerPing> result;
    std::array<uint8_t, 2048> buffer {};
    for (int attempt = 0; attempt < Attempts && !result; ++attempt) {
        sendto(socket, reinterpret_cast<const char*>(ping.data()), static_cast<int>(ping.size()), 0, resolved->ai_addr, static_cast<int>(resolved->ai_addrlen));
#ifdef _WIN32
        WSAPOLLFD waiting { socket, POLLRDNORM, 0 };
        int ready = WSAPoll(&waiting, 1, AttemptMilliseconds);
#else
        pollfd waiting { socket, POLLIN, 0 };
        int ready = poll(&waiting, 1, AttemptMilliseconds);
#endif
        if (ready <= 0) {
            continue;
        }
        int received = static_cast<int>(recv(socket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0));
        constexpr int Header = 1 + 8 + 8 + 16 + 2;
        if (received < Header || buffer[0] != UnconnectedPong) {
            continue;
        }
        size_t length = (size_t(buffer[Header - 2]) << 8) | buffer[Header - 1];
        if (Header + length > size_t(received)) {
            continue;
        }
        result = parsePong(std::string(reinterpret_cast<const char*>(buffer.data() + Header), length));
    }
    closeSocket(socket);
    freeaddrinfo(resolved);
    return result;
}

}

ServerPinger::ServerPinger()
{
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
    worker = std::thread([this] {
        run();
    });
}

ServerPinger::~ServerPinger()
{
    stopping = true;
    wake.notify_all();
    if (worker.joinable()) {
        worker.join();
    }
}

void ServerPinger::request(const std::string& address)
{
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> guard(mutex);
    auto asked = requested.find(address);
    if (asked != requested.end() && now - asked->second < RefreshInterval) {
        return;
    }
    requested[address] = now;
    known.try_emplace(address);
    queue.push_back(address);
    wake.notify_one();
}

std::map<std::string, ServerPing> ServerPinger::results() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return known;
}

void ServerPinger::run()
{
    while (!stopping) {
        std::string address;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] {
                return stopping || !queue.empty();
            });
            if (stopping) {
                return;
            }
            address = std::move(queue.front());
            queue.pop_front();
        }
        std::optional<ServerPing> answer = pingOnce(address);
        std::lock_guard<std::mutex> guard(mutex);
        if (answer) {
            known[address] = std::move(*answer);
        } else {
            known[address].state = PingState::Offline;
        }
    }
}

}
