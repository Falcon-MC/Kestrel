#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace kestrel {

enum class PingState {
    Checking,
    Online,
    Offline,
};

/**
 * What a server answered to the last ping: its message of the day with its
 * formatting codes, player counts and version.
 */
struct ServerPing {
    PingState state = PingState::Checking;
    std::string motd;
    std::string version;
    int players = 0;
    int maxPlayers = 0;
    int latencyMs = -1;
};

/**
 * Pings Bedrock servers on a background thread with the unconnected ping every
 * server answers before anyone joins, to show their message of the day in the
 * server list.
 */
class ServerPinger {
public:
    ServerPinger();
    ~ServerPinger();

    ServerPinger(const ServerPinger&) = delete;
    ServerPinger& operator=(const ServerPinger&) = delete;

    void request(const std::string& address);
    std::map<std::string, ServerPing> results() const;

private:
    void run();

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> queue;
    std::map<std::string, ServerPing> known;
    std::map<std::string, std::chrono::steady_clock::time_point> requested;
    std::atomic<bool> stopping { false };
    std::thread worker;
};

}
