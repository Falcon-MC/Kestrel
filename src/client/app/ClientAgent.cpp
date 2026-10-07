#include "client/Client.h"
#include "client/DebugLog.h"

#include "Network/Crypto/Base64.h"
#include "platform/Paths.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/Image.h"
#include "ui/Utf8.h"
#include "util/JsonText.h"
#include "util/Text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kestrel {

namespace {

using agent::JsonWriter;
using agent::Request;

template <typename T>
struct Named {
    const char* name;
    T value;
};

constexpr Named<menu::Screen> ScreenNames[] = {
    { "title", menu::Screen::Title },
    { "play", menu::Screen::Play },
    { "settings", menu::Screen::Settings },
    { "server_form", menu::Screen::ServerForm },
    { "marketplace", menu::Screen::Marketplace },
    { "dressing_room", menu::Screen::DressingRoom },
    { "profile", menu::Screen::Profile },
};

constexpr Named<menu::Dialog> DialogNames[] = {
    { "none", menu::Dialog::None },
    { "confirm_delete", menu::Dialog::ConfirmDelete },
    { "confirm_exit", menu::Dialog::ConfirmExit },
    { "pause", menu::Dialog::Pause },
    { "sign_in", menu::Dialog::SignIn },
    { "connecting", menu::Dialog::Connecting },
    { "connection_error", menu::Dialog::ConnectionError },
    { "chat", menu::Dialog::Chat },
    { "profile_options", menu::Dialog::ProfileOptions },
    { "death", menu::Dialog::Death },
    { "safe_area", menu::Dialog::SafeArea },
    { "realm_invites", menu::Dialog::RealmInvites },
    { "join_realm", menu::Dialog::JoinRealm },
    { "confirm_remove_friend", menu::Dialog::ConfirmRemoveFriend },
};

constexpr Named<menu::SettingsPage> PageNames[] = {
    { "accessibility", menu::SettingsPage::Accessibility },
    { "keyboard", menu::SettingsPage::Keyboard },
    { "controller", menu::SettingsPage::Controller },
    { "touch", menu::SettingsPage::Touch },
    { "party", menu::SettingsPage::Party },
    { "general", menu::SettingsPage::General },
    { "video", menu::SettingsPage::Video },
    { "audio", menu::SettingsPage::Audio },
    { "account", menu::SettingsPage::Account },
    { "mods", menu::SettingsPage::Mods },
    { "global_resources", menu::SettingsPage::GlobalResources },
    { "storage", menu::SettingsPage::Storage },
    { "language", menu::SettingsPage::Language },
    { "creator", menu::SettingsPage::Creator },
};

constexpr Named<menu::PlayTab> TabNames[] = {
    { "realms", menu::PlayTab::Realms },
    { "servers", menu::PlayTab::Servers },
};

template <typename T, size_t N>
std::optional<T> byName(const Named<T> (&table)[N], const std::string& name)
{
    for (const Named<T>& entry : table) {
        if (name == entry.name) {
            return entry.value;
        }
    }
    return std::nullopt;
}

template <typename T, size_t N>
const char* nameOf(const Named<T> (&table)[N], T value)
{
    for (const Named<T>& entry : table) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return "unknown";
}

template <typename T, size_t N>
std::string choices(const Named<T> (&table)[N])
{
    std::string text;
    for (const Named<T>& entry : table) {
        text += text.empty() ? "" : ", ";
        text += entry.name;
    }
    return text;
}

/**
 * The key an agent names, by the names the key binding screen uses ("W",
 * "Space", "Escape") in any case.
 */
Key keyByName(const std::string& name)
{
    std::string wanted = util::lowercase(name);
    if (wanted == "control" || wanted == "ctrl") {
        return Key::Control;
    }
    if (wanted == "esc") {
        return Key::Escape;
    }
    if (wanted == "return") {
        return Key::Enter;
    }
    for (size_t i = 1; i < KeyCount; ++i) {
        if (util::lowercase(keyName(static_cast<Key>(i))) == wanted) {
            return static_cast<Key>(i);
        }
    }
    return Key::None;
}

void pressKey(InputState& input, Key key)
{
    input.setKey(key, true);
    input.escape |= key == Key::Escape;
    input.enter |= key == Key::Enter;
    input.backspace |= key == Key::Backspace;
    input.tab |= key == Key::Tab;
}

std::u32string codepointsOf(const std::string& text)
{
    std::u32string codepoints;
    size_t i = 0;
    while (i < text.size()) {
        codepoints.push_back(ui::nextCodepoint(text, i));
    }
    return codepoints;
}

/**
 * Shrinks an RGBA image to fit width by averaging whole blocks of pixels,
 * which keeps text readable better than dropping pixels.
 */
void shrinkToWidth(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height, uint32_t maxWidth)
{
    if (maxWidth == 0 || width <= maxWidth) {
        return;
    }
    uint32_t newWidth = maxWidth;
    uint32_t newHeight = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(double(height) * newWidth / width)));
    std::vector<uint8_t> out(size_t(newWidth) * newHeight * 4);
    for (uint32_t y = 0; y < newHeight; ++y) {
        uint32_t y0 = uint32_t(uint64_t(y) * height / newHeight);
        uint32_t y1 = std::max(y0 + 1, uint32_t(uint64_t(y + 1) * height / newHeight));
        for (uint32_t x = 0; x < newWidth; ++x) {
            uint32_t x0 = uint32_t(uint64_t(x) * width / newWidth);
            uint32_t x1 = std::max(x0 + 1, uint32_t(uint64_t(x + 1) * width / newWidth));
            uint32_t sum[4] {};
            for (uint32_t sy = y0; sy < y1; ++sy) {
                const uint8_t* row = rgba.data() + (size_t(sy) * width + x0) * 4;
                for (uint32_t sx = x0; sx < x1; ++sx, row += 4) {
                    sum[0] += row[0];
                    sum[1] += row[1];
                    sum[2] += row[2];
                    sum[3] += row[3];
                }
            }
            uint32_t count = (y1 - y0) * (x1 - x0);
            uint8_t* pixel = out.data() + (size_t(y) * newWidth + x) * 4;
            for (int c = 0; c < 4; ++c) {
                pixel[c] = static_cast<uint8_t>(sum[c] / count);
            }
        }
    }
    rgba = std::move(out);
    width = newWidth;
    height = newHeight;
}

void cropTo(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height, const ui::Rect& area)
{
    auto x0 = static_cast<uint32_t>(std::clamp(area.x, 0.0f, float(width)));
    auto y0 = static_cast<uint32_t>(std::clamp(area.y, 0.0f, float(height)));
    auto x1 = static_cast<uint32_t>(std::clamp(area.right(), float(x0), float(width)));
    auto y1 = static_cast<uint32_t>(std::clamp(area.bottom(), float(y0), float(height)));
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    std::vector<uint8_t> out(size_t(x1 - x0) * (y1 - y0) * 4);
    for (uint32_t y = y0; y < y1; ++y) {
        std::copy_n(rgba.data() + (size_t(y) * width + x0) * 4, size_t(x1 - x0) * 4, out.data() + size_t(y - y0) * (x1 - x0) * 4);
    }
    rgba = std::move(out);
    width = x1 - x0;
    height = y1 - y0;
}

std::string formType(const std::string& json)
{
    std::unique_ptr<json::Value> root = json::parse(json);
    const json::Value* type = root && root->isObject() ? root->get("type") : nullptr;
    return type ? type->string() : std::string();
}

}

const std::string& Client::offlineName() const
{
    return launch.name.empty() ? menu.playerName() : launch.name;
}

void Client::startAgent()
{
    agentServer = std::make_unique<agent::AgentServer>(launch.agentPort, launch.agentToken);
    agentServer->publish(platform::dataDirectory() / "agent.json");
    agentSession = std::make_unique<agent::SessionControl>(session, account, agentEvents, launch.hidden ? "hidden" : "window",
        [this](const std::string& name, const std::string& address) { pendingConnect = menu::ConnectRequest { name, address }; });
    debugLog("agent listening on 127.0.0.1:" + std::to_string(agentServer->port()));
    std::fprintf(stderr, "Kestrel agent listening on 127.0.0.1:%u\n", unsigned(agentServer->port()));
    std::fflush(stderr);
}

void Client::serveAgent()
{
    if (!agentInput.empty()) {
        AgentInputStep step = std::move(agentInput.front());
        agentInput.pop_front();
        if (step.apply) {
            step.apply(window->input());
        }
        if (step.done) {
            agentServer->respond(*step.done, "{\"done\":true}");
        }
    }
    for (Request& request : agentServer->takeRequests()) {
        if (agentSession->handle(request, *agentServer)) {
            continue;
        }
        if (!handleAgentRequest(request)) {
            agentServer->fail(request, "Unknown method " + request.method);
        }
    }
}

void Client::queueAgentInput(std::vector<std::function<void(InputState&)>> steps, Request& request)
{
    auto done = std::make_shared<Request>(std::move(request));
    for (size_t i = 0; i < steps.size(); ++i) {
        agentInput.push_back({ std::move(steps[i]), i + 1 == steps.size() ? done : nullptr });
    }
}

std::string Client::agentState()
{
    JsonWriter writer;
    writer.beginObject();
    writer.field("screen", nameOf(ScreenNames, menu.currentScreen()));
    writer.field("dialog", nameOf(DialogNames, menu.currentDialog()));
    writer.field("settingsPage", nameOf(PageNames, menu.currentSettingsPage()));
    writer.field("playTab", nameOf(TabNames, menu.currentPlayTab()));
    writer.field("socialDrawer", menu.socialDrawerOpen());
    writer.field("inventoryOpen", menu.inventoryOpen());
    writer.field("formsOpen", menu.formPanel().openForms().size());
    writer.field("inGame", worldShown);
    writer.field("playing", menu.capturesMouse());
    writer.field("debugScreen", menu.debugVisible());
    writer.field("pendingInput", agentInput.size());
    writer.field("lastEvent", agentEvents.last());
    writer.key("window").beginObject()
        .field("width", window->width())
        .field("height", window->height())
        .field("guiScale", guiScale())
        .field("visible", window->visible())
        .field("fullscreen", window->fullscreen())
        .field("maximized", window->maximized())
        .endObject();
    writer.key("renderer").beginObject()
        .field("backend", renderer->backendName())
        .field("device", renderer->deviceName())
        .field("fps", framesPerSecond)
        .field("frames", renderer->submittedFrames())
        .endObject();
    writer.field("session", agent::SessionControl::stateName(session.sharedSnapshot()->state));
    writer.endObject();
    return writer.take();
}

std::string Client::agentWidgetList(const Request& request) const
{
    bool visibleOnly = agent::boolParam(request, "visibleOnly", true);
    std::string filter = util::lowercase(agent::stringParam(request, "filter"));
    float scale = guiScale();
    JsonWriter writer;
    writer.beginObject().field("guiScale", scale).key("widgets").beginArray();
    for (const ui::Widget& widget : agentWidgets) {
        if (visibleOnly && !widget.visible) {
            continue;
        }
        if (!filter.empty() && !util::contains(util::lowercase(widget.id), filter) && !util::contains(util::lowercase(widget.label), filter)) {
            continue;
        }
        writer.beginObject().field("id", widget.id);
        if (!widget.label.empty()) {
            writer.field("label", widget.label);
        }
        writer.field("x", std::round(widget.rect.x * scale)).field("y", std::round(widget.rect.y * scale));
        writer.field("width", std::round(widget.rect.w * scale)).field("height", std::round(widget.rect.h * scale));
        writer.field("centerX", std::round((widget.rect.x + widget.rect.w * 0.5f) * scale));
        writer.field("centerY", std::round((widget.rect.y + widget.rect.h * 0.5f) * scale));
        writer.field("visible", widget.visible).endObject();
    }
    writer.endArray().endObject();
    return writer.take();
}

std::string Client::agentSettings() const
{
    JsonWriter writer;
    writer.beginObject()
        .field("renderDistance", menu.renderDistance())
        .field("maxFps", menu.maxFps())
        .field("fov", menu.fov())
        .field("interfaceScale", menu.interfaceScale())
        .field("guiScaleModifier", menu.option("gui_scale", 0))
        .field("language", menu.language())
        .field("paperDollHidden", menu.paperDollHidden())
        .field("safeArea", menu.safeArea())
        .field("brightness", menu.brightness())
        .field("fullscreen", window->fullscreen());
    writer.key("volumes").beginArray();
    for (int volume : menu.soundVolumes()) {
        writer.value(volume);
    }
    writer.endArray();
    writer.key("keyBindings").beginObject();
    for (size_t i = 0; i < KeyBindings::Count; ++i) {
        writer.field(KeyBindings::id(i), keyName(menu.keyBindings().keys[i]));
    }
    writer.endObject().endObject();
    return writer.take();
}

bool Client::applyAgentSettings(const Request& request, std::string& error)
{
    if (const json::Value* value = request.param("renderDistance"); value && value->isNumber()) {
        menu.setRenderDistance(std::clamp(value->integer(), menu::MinRenderDistance, menu::MaxRenderDistance));
    }
    if (const json::Value* value = request.param("maxFps"); value && value->isNumber()) {
        int limit = value->integer();
        menu.setMaxFps(limit == menu::UnlimitedFps ? limit : std::clamp(limit, menu::MinMaxFps, menu::MaxMaxFps));
    }
    if (const json::Value* value = request.param("fov"); value && value->isNumber()) {
        menu.setFov(std::clamp(value->integer(), menu::MinFov, menu::MaxFov));
    }
    if (const json::Value* value = request.param("interfaceScale"); value && value->isNumber()) {
        menu.setInterfaceScale(std::clamp(static_cast<float>(value->mNumber), 0.25f, 4.0f));
    }
    if (const json::Value* value = request.param("language"); value && value->isString()) {
        menu.setLanguage(value->mString);
    }
    if (const json::Value* value = request.param("paperDollHidden"); value && value->mType == json::Value::Type::Boolean) {
        menu.setPaperDollHidden(value->mBoolean);
    }
    if (const json::Value* value = request.param("safeArea"); value && value->isNumber()) {
        menu.setSafeArea(std::clamp(static_cast<float>(value->mNumber), menu::MinSafeArea, menu::MaxSafeArea));
    }
    if (const json::Value* value = request.param("brightness"); value && value->isNumber()) {
        menu.setBrightness(static_cast<int>(std::lround(value->mNumber)));
    }
    if (const json::Value* value = request.param("fullscreen"); value && value->mType == json::Value::Type::Boolean) {
        if (value->mBoolean != window->fullscreen() && window->visible()) {
            window->toggleFullscreen();
        }
    }
    if (const json::Value* volumes = request.param("volumes"); volumes && volumes->isArray()) {
        for (size_t i = 0; i < volumes->mArray.size() && i < menu::VolumeChannelCount; ++i) {
            if (volumes->mArray[i]->isNumber()) {
                menu.setSoundVolume(i, std::clamp(volumes->mArray[i]->integer(), 0, 100));
            }
        }
    }
    if (const json::Value* keys = request.param("keyBindings"); keys && keys->isObject()) {
        KeyBindings bindings = menu.keyBindings();
        for (const std::string& id : keys->mKeys) {
            const json::Value* key = keys->get(id);
            size_t index = KeyBindings::Count;
            for (size_t i = 0; i < KeyBindings::Count; ++i) {
                if (id == KeyBindings::id(i)) {
                    index = i;
                }
            }
            Key chosen = key && key->isString() ? keyByName(key->mString) : Key::None;
            if (index == KeyBindings::Count || chosen == Key::None) {
                error = "Unknown binding or key: " + id;
                return false;
            }
            bindings.keys[index] = chosen;
        }
        menu.setKeyBindings(bindings);
    }
    return true;
}

std::string Client::agentServers()
{
    std::map<std::string, ServerPing> pings = pinger.results();
    auto writePing = [&](JsonWriter& writer, const std::string& address) {
        auto found = pings.find(address);
        if (found == pings.end()) {
            return;
        }
        const ServerPing& ping = found->second;
        writer.key("ping").beginObject()
            .field("state", ping.state == PingState::Checking ? "checking" : ping.state == PingState::Online ? "online" : "offline")
            .field("motd", ping.motd)
            .field("version", ping.version)
            .field("players", ping.players)
            .field("maxPlayers", ping.maxPlayers)
            .field("latencyMs", ping.latencyMs)
            .endObject();
    };
    JsonWriter writer;
    writer.beginObject().key("saved").beginArray();
    for (size_t i = 0; i < store.servers().size(); ++i) {
        const menu::SavedServer& server = store.servers()[i];
        writer.beginObject().field("index", i).field("name", server.name).field("address", server.address);
        writer.field("favorite", server.favorite).field("lastJoined", server.lastJoined);
        writePing(writer, server.address);
        writer.endObject();
    }
    writer.endArray().key("featured").beginArray();
    for (const FeaturedServer& server : featuredList) {
        writer.beginObject().field("id", server.id).field("name", server.name).field("creator", server.creator);
        writer.field("description", server.description).field("address", server.creatorExperience() ? "experience:" + server.id : server.address);
        writePing(writer, server.address);
        writer.endObject();
    }
    writer.endArray().endObject();
    return writer.take();
}

std::string Client::agentForms()
{
    JsonWriter writer;
    writer.beginObject().key("forms").beginArray();
    for (const auto& [id, json] : menu.formPanel().openForms()) {
        writer.beginObject().field("id", id).field("type", formType(json)).key("form").raw(json.empty() ? "null" : json).endObject();
    }
    writer.endArray().endObject();
    return writer.take();
}

std::string Client::agentDebug()
{
    auto published = session.sharedSnapshot();
    const SessionSnapshot& snapshot = *published;
    menu::DebugView view = buildDebugView(snapshot);
    memory = platform::memoryUsage();
    JsonWriter writer;
    writer.beginObject();
    writer.key("left").beginArray();
    for (const std::string& line : view.left) {
        writer.value(line);
    }
    writer.endArray().key("right").beginArray();
    for (const std::string& line : view.right) {
        writer.value(line);
    }
    writer.endArray().key("profiler").beginArray();
    for (const std::string& line : profiler.lines()) {
        writer.value(line);
    }
    writer.endArray();
    writer.field("fps", framesPerSecond).field("residentBytes", memory.resident).field("processor", processor);
    writer.field("backend", renderer->backendName()).field("device", renderer->deviceName());
    writer.field("debugLog", (platform::dataDirectory() / "debug.txt").string());
    writer.field("dataDirectory", platform::dataDirectory().string());
    writer.endObject();
    return writer.take();
}

bool Client::handleAgentRequest(Request& request)
{
    const std::string& method = request.method;
    JsonWriter writer;

    if (method == "state") {
        agentServer->respond(request, agentState());
    } else if (method == "screenshot") {
        if (!renderer->requestCapture()) {
            agentServer->fail(request, std::string("Screenshots are not supported on the ") + std::string(renderer->backendName()) + " renderer yet");
            return true;
        }
        AgentCapture capture;
        capture.maxWidth = static_cast<uint32_t>(std::clamp(agent::numberParam(request, "maxWidth", 0.0), 0.0, 16384.0));
        if (const json::Value* crop = request.param("crop"); crop && crop->isObject()) {
            capture.crop = ui::Rect { util::jsonNumber(crop->get("x"), 0.0f), util::jsonNumber(crop->get("y"), 0.0f),
                util::jsonNumber(crop->get("width"), 0.0f), util::jsonNumber(crop->get("height"), 0.0f) };
        }
        capture.framesLeft = 45;
        capture.request = std::make_shared<Request>(std::move(request));
        agentCaptures.push_back(std::move(capture));
    } else if (method == "ui.widgets") {
        agentServer->respond(request, agentWidgetList(request));
    } else if (method == "ui.click") {
        std::string id = agent::stringParam(request, "id");
        std::string label = util::lowercase(agent::stringParam(request, "label"));
        const ui::Widget* target = nullptr;
        for (int pass = 0; pass < 2 && !target; ++pass) {
            for (const ui::Widget& widget : agentWidgets) {
                if (!widget.visible) {
                    continue;
                }
                bool exact = pass == 0;
                bool matches = !id.empty() ? (exact ? widget.id == id : util::contains(widget.id, id))
                    : !label.empty() ? (exact ? util::lowercase(widget.label) == label : util::contains(util::lowercase(widget.label), label))
                    : false;
                if (matches) {
                    target = &widget;
                    break;
                }
            }
        }
        if (!target) {
            agentServer->fail(request, "No visible widget matches; list them with ui.widgets");
            return true;
        }
        float scale = guiScale();
        float x = (target->rect.x + target->rect.w * 0.5f) * scale;
        float y = (target->rect.y + target->rect.h * 0.5f) * scale;
        queueAgentInput({
                            [x, y](InputState& input) {
                                input.mouseX = x;
                                input.mouseY = y;
                            },
                            [x, y](InputState& input) {
                                input.mouseX = x;
                                input.mouseY = y;
                                input.mouseDown = true;
                                input.mousePressed = true;
                            },
                            [](InputState& input) {
                                input.mouseDown = false;
                                input.mouseReleased = true;
                            },
                        },
            request);
    } else if (method == "input.mouse") {
        auto x = static_cast<float>(agent::numberParam(request, "x", window->input().mouseX));
        auto y = static_cast<float>(agent::numberParam(request, "y", window->input().mouseY));
        std::string action = agent::stringParam(request, "action", "click");
        auto moveTo = [x, y](InputState& input) {
            input.mouseX = x;
            input.mouseY = y;
        };
        std::vector<std::function<void(InputState&)>> steps { moveTo };
        if (action == "click" || action == "down") {
            steps.push_back([](InputState& input) {
                input.mouseDown = true;
                input.mousePressed = true;
            });
        }
        if (action == "click" || action == "up") {
            steps.push_back([](InputState& input) {
                input.mouseDown = false;
                input.mouseReleased = true;
            });
        }
        if (action == "right_click" || action == "right_down") {
            steps.push_back([](InputState& input) {
                input.rightMouseDown = true;
                input.rightMousePressed = true;
            });
        }
        if (action == "right_click" || action == "right_up") {
            steps.push_back([](InputState& input) { input.rightMouseDown = false; });
        }
        if (action == "middle_click") {
            steps.push_back([](InputState& input) { input.middleMousePressed = true; });
        }
        if (steps.size() == 1 && action != "move") {
            agentServer->fail(request, "action must be move, click, down, up, right_click, right_down, right_up or middle_click");
            return true;
        }
        queueAgentInput(std::move(steps), request);
    } else if (method == "input.scroll") {
        auto amount = static_cast<float>(agent::numberParam(request, "amount", -1.0));
        std::optional<float> x;
        std::optional<float> y;
        if (request.param("x") && request.param("y")) {
            x = static_cast<float>(agent::numberParam(request, "x", 0.0));
            y = static_cast<float>(agent::numberParam(request, "y", 0.0));
        }
        queueAgentInput({ [amount, x, y](InputState& input) {
            if (x && y) {
                input.mouseX = *x;
                input.mouseY = *y;
            }
            input.wheel += amount;
        } },
            request);
    } else if (method == "input.key") {
        Key key = keyByName(agent::stringParam(request, "key"));
        if (key == Key::None) {
            agentServer->fail(request, "Unknown key; use names like W, Space, Escape, Enter, Ctrl, F3, 1");
            return true;
        }
        std::string action = agent::stringParam(request, "action", "press");
        if (action == "down") {
            queueAgentInput({ [key](InputState& input) { pressKey(input, key); } }, request);
        } else if (action == "up") {
            queueAgentInput({ [key](InputState& input) { input.setKey(key, false); } }, request);
        } else if (action == "press") {
            queueAgentInput({ [key](InputState& input) { pressKey(input, key); }, [key](InputState& input) { input.setKey(key, false); } }, request);
        } else {
            agentServer->fail(request, "action must be press, down or up");
            return true;
        }
    } else if (method == "input.text") {
        std::u32string text = codepointsOf(agent::stringParam(request, "text"));
        bool submit = agent::boolParam(request, "submit", false);
        std::vector<std::function<void(InputState&)>> steps { [text](InputState& input) { input.text += text; } };
        if (submit) {
            steps.push_back([](InputState& input) { pressKey(input, Key::Enter); });
            steps.push_back([](InputState& input) { input.setKey(Key::Enter, false); });
        }
        queueAgentInput(std::move(steps), request);
    } else if (method == "input.releaseAll") {
        queueAgentInput({ [](InputState& input) {
            input.releaseKeys();
            input.mouseDown = false;
            input.rightMouseDown = false;
        } },
            request);
    } else if (method == "camera.look") {
        float yaw = static_cast<float>(agent::numberParam(request, "yaw", camera.minecraftYaw()) + agent::numberParam(request, "turnYaw", 0.0));
        float pitch = static_cast<float>(agent::numberParam(request, "pitch", camera.minecraftPitch()) + agent::numberParam(request, "turnPitch", 0.0));
        pitch = std::clamp(pitch, -89.9f, 89.9f);
        camera.placeAt(camera.x(), camera.y(), camera.z(), yaw, pitch);
        writer.beginObject().field("yaw", camera.minecraftYaw()).field("pitch", camera.minecraftPitch()).endObject();
        agentServer->respond(request, writer.take());
    } else if (method == "menu.open") {
        std::string screenName = agent::stringParam(request, "screen");
        std::optional<menu::Screen> screen = byName(ScreenNames, screenName);
        if (!screen) {
            agentServer->fail(request, "screen must be one of " + choices(ScreenNames));
            return true;
        }
        if (std::string page = agent::stringParam(request, "page"); !page.empty()) {
            std::optional<menu::SettingsPage> settingsPage = byName(PageNames, page);
            if (!settingsPage) {
                agentServer->fail(request, "page must be one of " + choices(PageNames));
                return true;
            }
            menu.openSettingsPage(*settingsPage);
        }
        if (std::string tab = agent::stringParam(request, "tab"); !tab.empty()) {
            std::optional<menu::PlayTab> playTab = byName(TabNames, tab);
            if (!playTab) {
                agentServer->fail(request, "tab must be one of " + choices(TabNames));
                return true;
            }
            menu.openPlayTab(*playTab);
        }
        if (*screen == menu::Screen::ServerForm && request.param("index")) {
            auto index = static_cast<size_t>(std::max(0.0, agent::numberParam(request, "index", 0.0)));
            if (index >= store.servers().size()) {
                agentServer->fail(request, "No saved server at that index");
                return true;
            }
            menu.editServer(index);
        } else {
            menu.openScreen(*screen);
        }
        agentServer->respond(request, agentState());
    } else if (method == "menu.dialog") {
        std::optional<menu::Dialog> dialog = byName(DialogNames, agent::stringParam(request, "dialog"));
        if (!dialog || !menu.showDialog(*dialog)) {
            agentServer->fail(request, "dialog must be none, pause, chat (in game), confirm_exit, safe_area or profile_options");
            return true;
        }
        agentServer->respond(request, agentState());
    } else if (method == "menu.back") {
        queueAgentInput({ [](InputState& input) { pressKey(input, Key::Escape); }, [](InputState& input) { input.setKey(Key::Escape, false); } }, request);
    } else if (method == "inventory.open") {
        if (!menu.openInventory()) {
            agentServer->fail(request, "The inventory opens only while playing, with no screen or dialog up");
            return true;
        }
        agentServer->respond(request, "{\"open\":true}");
    } else if (method == "inventory.close") {
        menu.inventoryPanel().close();
        agentServer->respond(request, "{\"open\":false}");
    } else if (method == "forms.list") {
        agentServer->respond(request, agentForms());
    } else if (method == "forms.tree") {
        JsonWriter writer;
        writer.beginObject().field("tree", menu.formPanel().describeScreen()).endObject();
        agentServer->respond(request, writer.take());
    } else if (method == "forms.answer") {
        std::vector<std::pair<uint32_t, std::string>> open = menu.formPanel().openForms();
        if (open.empty()) {
            agentServer->fail(request, "No form is open");
            return true;
        }
        auto id = static_cast<uint32_t>(agent::numberParam(request, "id", open.back().first));
        auto found = std::find_if(open.begin(), open.end(), [id](const auto& form) { return form.first == id; });
        if (found == open.end()) {
            agentServer->fail(request, "No open form has that id");
            return true;
        }
        std::optional<std::string> data;
        std::string type = formType(found->second);
        if (agent::boolParam(request, "close", false)) {
            data.reset();
        } else if (std::string response = agent::stringParam(request, "response"); !response.empty()) {
            data = response;
        } else if (const json::Value* button = request.param("button"); button && button->isNumber()) {
            int index = button->integer();
            data = type == "modal" ? (index == 0 ? "true\n" : "false\n") : std::to_string(index) + "\n";
        } else if (const json::Value* values = request.param("values"); values && values->isArray()) {
            JsonWriter response;
            response.beginArray();
            for (const std::unique_ptr<json::Value>& value : values->mArray) {
                switch (value->mType) {
                case json::Value::Type::Boolean:
                    response.value(value->mBoolean);
                    break;
                case json::Value::Type::Number:
                    response.value(value->mNumber);
                    break;
                case json::Value::Type::String:
                    response.value(value->mString);
                    break;
                default:
                    response.null();
                    break;
                }
            }
            data = response.endArray().take() + "\n";
        } else {
            agentServer->fail(request, "Give button (simple and modal forms), values (custom forms), response (raw JSON) or close");
            return true;
        }
        menu.formPanel().answer(id, data);
        writer.beginObject().field("id", id).field("closed", !data.has_value()).endObject();
        agentServer->respond(request, writer.take());
    } else if (method == "chat.log") {
        std::vector<std::string> lines = menu.chatLog();
        auto limit = static_cast<size_t>(std::clamp(agent::numberParam(request, "limit", 100.0), 1.0, 1000.0));
        writer.beginObject().key("lines").beginArray();
        for (size_t i = lines.size() > limit ? lines.size() - limit : 0; i < lines.size(); ++i) {
            writer.value(lines[i]);
        }
        writer.endArray().endObject();
        agentServer->respond(request, writer.take());
    } else if (method == "settings.get") {
        agentServer->respond(request, agentSettings());
    } else if (method == "settings.set") {
        std::string error;
        if (!applyAgentSettings(request, error)) {
            agentServer->fail(request, error);
            return true;
        }
        agentServer->respond(request, agentSettings());
    } else if (method == "servers.list") {
        agentServer->respond(request, agentServers());
    } else if (method == "servers.add") {
        std::string address = agent::stringParam(request, "address");
        if (menu::normalizeAddress(address).empty()) {
            agentServer->fail(request, "address is required");
            return true;
        }
        size_t index = store.add(agent::stringParam(request, "name", address), address);
        writer.beginObject().field("index", index).endObject();
        agentServer->respond(request, writer.take());
    } else if (method == "servers.update" || method == "servers.remove" || method == "servers.favorite") {
        auto index = static_cast<size_t>(std::max(0.0, agent::numberParam(request, "index", -1.0)));
        if (!request.param("index") || index >= store.servers().size()) {
            agentServer->fail(request, "No saved server at that index");
            return true;
        }
        if (method == "servers.update") {
            const menu::SavedServer& server = store.servers()[index];
            store.update(index, agent::stringParam(request, "name", server.name), agent::stringParam(request, "address", server.address));
        } else if (method == "servers.remove") {
            store.remove(index);
        } else {
            store.toggleFavorite(index);
        }
        agentServer->respond(request, agentServers());
    } else if (method == "servers.ping") {
        std::string address = agent::stringParam(request, "address");
        if (address.empty()) {
            agentServer->fail(request, "address is required");
            return true;
        }
        pinger.request(address);
        std::map<std::string, ServerPing> pings = pinger.results();
        auto found = pings.find(address);
        writer.beginObject().field("address", address);
        if (found == pings.end() || found->second.state == PingState::Checking) {
            writer.field("state", "checking");
        } else {
            const ServerPing& ping = found->second;
            writer.field("state", ping.state == PingState::Online ? "online" : "offline").field("motd", ping.motd).field("version", ping.version);
            writer.field("players", ping.players).field("maxPlayers", ping.maxPlayers).field("latencyMs", ping.latencyMs);
        }
        agentServer->respond(request, writer.endObject().take());
    } else if (method == "debug.info") {
        agentServer->respond(request, agentDebug());
    } else if (method == "app.quit") {
        agentQuit = true;
        agentServer->respond(request, "{\"quitting\":true}");
    } else {
        return false;
    }
    return true;
}

void Client::finishAgentCaptures()
{
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    bool captured = renderer->takeCapture(pixels, width, height);
    for (auto capture = agentCaptures.begin(); capture != agentCaptures.end();) {
        if (!captured) {
            if (--capture->framesLeft <= 0) {
                agentServer->fail(*capture->request, "The frame could not be captured, is the window minimized?");
                capture = agentCaptures.erase(capture);
            } else {
                renderer->requestCapture();
                ++capture;
            }
            continue;
        }
        std::vector<uint8_t> image = pixels;
        uint32_t w = width;
        uint32_t h = height;
        if (capture->crop) {
            cropTo(image, w, h, *capture->crop);
        }
        shrinkToWidth(image, w, h, capture->maxWidth);
        std::string png = ui::encodePng(image, w, h);
        JsonWriter writer;
        writer.beginObject().field("width", w).field("height", h).field("windowWidth", width).field("windowHeight", height);
        writer.field("guiScale", guiScale()).field("png", Base64::encode(png)).endObject();
        agentServer->respond(*capture->request, writer.take());
        capture = agentCaptures.erase(capture);
    }
}

}
