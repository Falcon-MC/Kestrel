#include "client/HeadlessClient.h"

#include "client/ChatText.h"
#include "client/DebugLog.h"
#include "platform/Paths.h"
#include "ui/Localization.h"
#include "world/PackSource.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace kestrel {

namespace {

constexpr auto TickLength = std::chrono::milliseconds(20);
constexpr size_t ChatLines = 500;

}

HeadlessClient::HeadlessClient(LaunchOptions options)
    : launch(std::move(options))
    , account(platform::dataDirectory() / "microsoft-token.json")
{
    account.restore();
    if (launch.connect) {
        pendingConnect.emplace(*launch.connect, *launch.connect);
    }
    std::filesystem::path vanilla = world::PackSource::locateVanilla();
    if (!vanilla.empty()) {
        ui::Localization::shared().load(std::make_shared<world::PackSource>(vanilla), "en_US");
    }
    if (launch.agent) {
        server = std::make_unique<agent::AgentServer>(launch.agentPort, launch.agentToken);
        server->publish(platform::dataDirectory() / "agent.json");
        std::fprintf(stderr, "Kestrel agent listening on 127.0.0.1:%u\n", unsigned(server->port()));
        std::fflush(stderr);
    }
    control = std::make_unique<agent::SessionControl>(session, account, events, "headless",
        [this](const std::string& name, const std::string& address) { pendingConnect.emplace(name, address); });
}

int HeadlessClient::run()
{
    if (!launch.agent && !launch.connect) {
        std::fprintf(stderr, "Kestrel: --headless needs --connect <address> or --agent\n");
        return 2;
    }
    auto next = std::chrono::steady_clock::now();
    while (!quit) {
        // The saved sign in comes back on its own thread; joining before it would look offline.
        if (pendingConnect && account.snapshot().state != AccountState::Connecting) {
            session.connect(pendingConnect->first, pendingConnect->second, account.signedInAuthentication(), launch.name.empty() ? "Steve" : launch.name);
            pendingConnect.reset();
        }
        if (server) {
            for (agent::Request& request : server->takeRequests()) {
                if (!control->handle(request, *server) && !handle(request)) {
                    server->fail(request, "Unknown method " + request.method + " (headless mode has no screen, menus or input)");
                }
            }
        }
        session.setRenderDistance(renderDistance);
        auto published = session.sharedSnapshot();
        const SessionSnapshot& snapshot = *published;
        control->observe(snapshot);
        steer(snapshot);
        drainSession(snapshot);
        if (!server && (snapshot.state == SessionState::Failed || snapshot.state == SessionState::Disconnected)) {
            std::fprintf(stderr, "Kestrel: %s\n", snapshot.error.empty() ? "disconnected" : snapshot.error.c_str());
            return snapshot.state == SessionState::Failed ? 1 : 0;
        }
        next += TickLength;
        auto now = std::chrono::steady_clock::now();
        if (next < now) {
            next = now;
        }
        std::this_thread::sleep_until(next);
    }
    session.disconnect();
    return 0;
}

/**
 * Feeds the connection the motion and look ray a window would, from what
 * the agent asked for, so walking, breaking and placing work the same.
 */
void HeadlessClient::steer(const SessionSnapshot& snapshot)
{
    session.setMotionInput(motion);
    if (snapshot.state != SessionState::Joined) {
        return;
    }
    constexpr float Radians = 3.14159265f / 180.0f;
    float yaw = motion.yaw * Radians;
    float pitch = motion.pitch * Radians;
    std::array<float, 3> direction { -std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch) };
    const PlayerView& player = snapshot.player;
    std::array<double, 3> eye { player.current[0], player.current[1] + player.eyeHeight(), player.current[2] };
    session.setLookRay(eye, direction);
    session.setCameraBoom(eye, { 0.0, 0.0, 0.0 });
}

void HeadlessClient::drainSession(const SessionSnapshot& snapshot)
{
    if (snapshot.reloadedMeshes) session.acknowledgeResourceReload(snapshot.resourceReloadSerial, snapshot.assets.get());
    session.takeMeshUpdates();
    session.takeSkinUploads();
    session.takeSounds();
    session.takeParticleBursts();
    session.takeParticles();
    session.takeAttacks();
    for (const ChatMessage& message : session.takeChatMessages()) {
        std::string body = messageBody(message);
        std::string line = chatLine(message, body);
        std::string text = line.empty() ? body : line;
        control->noteChat(message, text);
        if (!line.empty()) {
            std::printf("%s\n", line.c_str());
            std::fflush(stdout);
            chat.push_back(std::move(line));
            if (chat.size() > ChatLines) {
                chat.pop_front();
            }
        }
    }
    if (std::optional<ActionbarText> actionbar = session.takeActionbar()) {
        agent::JsonWriter writer;
        events.add("actionbar", writer.beginObject().field("text", actionbar->json ? rawText(actionbar->text) : actionbar->text).endObject().take());
    }
    for (const TitleRequest& request : session.takeTitles()) {
        if (request.kind == TitleRequest::Kind::Title || request.kind == TitleRequest::Kind::Subtitle) {
            agent::JsonWriter writer;
            writer.beginObject().field("subtitle", request.kind == TitleRequest::Kind::Subtitle);
            events.add("title", writer.field("text", request.json ? rawText(request.text) : request.text).endObject().take());
        }
    }
    for (const ToastRequest& toast : session.takeToasts()) {
        agent::JsonWriter writer;
        events.add("toast", writer.beginObject().field("title", toast.title).field("content", toast.content).endObject().take());
    }
    for (FormRequest& request : session.takeForms()) {
        agent::JsonWriter writer;
        writer.beginObject().field("id", request.id).field("close", request.close);
        if (request.close) {
            forms.clear();
        } else {
            writer.field("json", request.data);
            forms[request.id] = std::move(request.data);
        }
        events.add("form", writer.endObject().take());
    }
    if (snapshot.state != SessionState::Joined) {
        forms.clear();
    }
}

bool HeadlessClient::handle(agent::Request& request)
{
    const std::string& method = request.method;
    agent::JsonWriter writer;
    if (method == "state") {
        writer.beginObject().field("mode", "headless").field("session", agent::SessionControl::stateName(session.sharedSnapshot()->state));
        writer.field("formsOpen", forms.size()).field("renderDistance", renderDistance).field("lastEvent", events.last());
        writer.key("motion").beginObject()
            .field("forward", motion.forward)
            .field("sideways", motion.sideways)
            .field("jump", motion.jump)
            .field("sneak", motion.sneak)
            .field("sprint", motion.sprint)
            .field("yaw", motion.yaw)
            .field("pitch", motion.pitch)
            .endObject();
        writer.endObject();
    } else if (method == "motion.set") {
        motion.forward = std::clamp(static_cast<float>(agent::numberParam(request, "forward", motion.forward)), -1.0f, 1.0f);
        motion.sideways = std::clamp(static_cast<float>(agent::numberParam(request, "sideways", motion.sideways)), -1.0f, 1.0f);
        motion.jump = agent::boolParam(request, "jump", motion.jump);
        motion.sneak = agent::boolParam(request, "sneak", motion.sneak);
        motion.sprint = agent::boolParam(request, "sprint", motion.sprint);
        writer.beginObject().field("forward", motion.forward).field("sideways", motion.sideways).endObject();
    } else if (method == "camera.look") {
        motion.yaw = static_cast<float>(agent::numberParam(request, "yaw", motion.yaw) + agent::numberParam(request, "turnYaw", 0.0));
        motion.pitch = std::clamp(static_cast<float>(agent::numberParam(request, "pitch", motion.pitch) + agent::numberParam(request, "turnPitch", 0.0)), -89.9f, 89.9f);
        writer.beginObject().field("yaw", motion.yaw).field("pitch", motion.pitch).endObject();
    } else if (method == "hold") {
        session.setAttackHeld(agent::boolParam(request, "attack", false));
        session.setUseHeld(agent::boolParam(request, "use", false));
        writer.beginObject().field("held", true).endObject();
    } else if (method == "settings.set") {
        renderDistance = std::clamp(static_cast<int>(agent::numberParam(request, "renderDistance", renderDistance)), 2, 32);
        writer.beginObject().field("renderDistance", renderDistance).endObject();
    } else if (method == "forms.list") {
        writer.beginObject().key("forms").beginArray();
        for (const auto& [id, json] : forms) {
            writer.beginObject().field("id", id).key("form").raw(json.empty() ? "null" : json).endObject();
        }
        writer.endArray().endObject();
    } else if (method == "forms.answer") {
        if (forms.empty()) {
            server->fail(request, "No form is open");
            return true;
        }
        auto id = static_cast<uint32_t>(agent::numberParam(request, "id", forms.rbegin()->first));
        auto found = forms.find(id);
        if (found == forms.end()) {
            server->fail(request, "No open form has that id");
            return true;
        }
        std::unique_ptr<json::Value> root = json::parse(found->second);
        std::string type = root && root->isObject() && root->get("type") ? root->get("type")->string() : std::string();
        std::optional<std::string> data;
        if (agent::boolParam(request, "close", false)) {
            data.reset();
        } else if (std::string response = agent::stringParam(request, "response"); !response.empty()) {
            data = response;
        } else if (const json::Value* button = request.param("button"); button && button->isNumber()) {
            data = type == "modal" ? (button->integer() == 0 ? "true\n" : "false\n") : std::to_string(button->integer()) + "\n";
        } else if (const json::Value* values = request.param("values"); values && values->isArray()) {
            agent::JsonWriter response;
            response.beginArray();
            for (const std::unique_ptr<json::Value>& value : values->mArray) {
                if (value->mType == json::Value::Type::Boolean) {
                    response.value(value->mBoolean);
                } else if (value->isNumber()) {
                    response.value(value->mNumber);
                } else if (value->isString()) {
                    response.value(value->mString);
                } else {
                    response.null();
                }
            }
            data = response.endArray().take() + "\n";
        } else {
            server->fail(request, "Give button, values, response or close");
            return true;
        }
        session.answerForm(id, data, false);
        forms.erase(found);
        writer.beginObject().field("id", id).field("closed", !data.has_value()).endObject();
    } else if (method == "chat.log") {
        auto limit = static_cast<size_t>(std::clamp(agent::numberParam(request, "limit", 100.0), 1.0, 500.0));
        writer.beginObject().key("lines").beginArray();
        for (size_t i = chat.size() > limit ? chat.size() - limit : 0; i < chat.size(); ++i) {
            writer.value(chat[i]);
        }
        writer.endArray().endObject();
    } else if (method == "debug.info") {
        writer.beginObject().field("debugLog", (platform::dataDirectory() / "debug.txt").string());
        writer.field("dataDirectory", platform::dataDirectory().string()).endObject();
    } else if (method == "app.quit") {
        quit = true;
        writer.beginObject().field("quitting", true).endObject();
    } else {
        return false;
    }
    server->respond(request, writer.take());
    return true;
}

}
