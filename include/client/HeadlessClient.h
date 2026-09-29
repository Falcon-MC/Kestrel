#pragma once

#include "agent/AgentServer.h"
#include "agent/EventLog.h"
#include "agent/SessionControl.h"
#include "client/Account.h"
#include "client/LaunchOptions.h"
#include "client/Session.h"

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace kestrel {

/**
 * Kestrel without a window: the account and the connection, driven by the
 * agent port or just sitting on the server named on the command line. Chat
 * goes to standard output so it also works as a quick bot.
 */
class HeadlessClient {
public:
    explicit HeadlessClient(LaunchOptions options);

    int run();

private:
    void drainSession(const SessionSnapshot& snapshot);
    bool handle(agent::Request& request);
    void steer(const SessionSnapshot& snapshot);

    LaunchOptions launch;
    Account account;
    Session session;
    agent::EventLog events;
    std::unique_ptr<agent::AgentServer> server;
    std::unique_ptr<agent::SessionControl> control;
    std::optional<std::pair<std::string, std::string>> pendingConnect;
    MotionInput motion;
    std::map<uint32_t, std::string> forms;
    std::deque<std::string> chat;
    int renderDistance = 4;
    bool quit = false;
};

}
