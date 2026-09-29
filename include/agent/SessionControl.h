#pragma once

#include "agent/AgentServer.h"
#include "agent/EventLog.h"
#include "agent/JsonWriter.h"
#include "client/Account.h"
#include "client/Session.h"

#include <functional>
#include <string>

namespace kestrel::agent {

/**
 * What a session state reply carries besides the basics.
 */
struct SnapshotOptions {
    bool inventory = true;
    bool actors = true;
    double actorRadius = 64.0;
    size_t actorLimit = 128;
    bool commands = false;
};

/**
 * The agent methods that only need the connection and the account, shared
 * by the windowed client and the headless one: joining and leaving, the
 * player's state, chat, inventory, interaction, packets and the event log.
 */
class SessionControl {
public:
    using Connector = std::function<void(const std::string& name, const std::string& address)>;

    SessionControl(Session& session, Account& account, EventLog& events, std::string mode, Connector connect);

    /**
     * Answers the request when it names one of these methods; false leaves
     * it to the caller.
     */
    bool handle(const Request& request, AgentServer& server);

    /**
     * Turns changes in the connection into events. Call with each fresh
     * snapshot.
     */
    void observe(const SessionSnapshot& snapshot);

    void noteChat(const ChatMessage& message, const std::string& text);

    static void writeItem(JsonWriter& writer, const HudItem& item);
    static void writeSnapshot(JsonWriter& writer, const SessionSnapshot& snapshot, const SnapshotOptions& options);
    static std::string stateName(SessionState state);

private:
    void writePackets(JsonWriter& writer, const Request& request);

    Session& session;
    Account& account;
    EventLog& events;
    std::string mode;
    Connector connect;
    SessionState lastState = SessionState::Idle;
    bool lastDead = false;
    int lastDimension = 0;
    uint64_t lastJoin = 0;
    std::string lastError;
    bool lastPackPrompt = false;
};

std::string hexOf(std::string_view bytes);
bool bytesOfHex(std::string_view hex, std::string& out);
std::string stringParam(const Request& request, const std::string& name, const std::string& fallback = {});
double numberParam(const Request& request, const std::string& name, double fallback);
bool boolParam(const Request& request, const std::string& name, bool fallback);

}
