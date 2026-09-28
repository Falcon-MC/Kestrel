#include "client/Client.h"

#include "Core/Json/Json.h"
#include "ui/Localization.h"
#include "ui/Utf8.h"

namespace kestrel {

namespace {

// From the game's HUD screen controller: a popup holds 0.04 seconds per character before
// fading, while jukebox popups keep the one second default of item_name_text_root.
constexpr float PopupHoldPerCharacter = 0.04f;
constexpr float JukeboxHoldSeconds = 1.0f;

std::string rawText(const std::string& message)
{
    std::unique_ptr<json::Value> root = json::parse(message);
    return root && root->isObject() ? ui::rawText(*root) : message;
}

std::string messageBody(const ChatMessage& message)
{
    return message.translate ? ui::Localization::shared().translateMessage(message.message, message.parameters) : message.message;
}

size_t codepoints(std::string_view text)
{
    size_t count = 0;
    for (size_t i = 0; i < text.size(); ++count) {
        ui::nextCodepoint(text, i);
    }
    return count;
}

/**
 * The line a text packet puts in chat, formatted the way the game formats
 * each kind. Popups and tips show above the hotbar instead, so they give none.
 */
std::string chatLine(const ChatMessage& message, const std::string& body)
{
    switch (message.kind) {
    case ChatMessage::Kind::Chat:
        return message.source.empty() ? body : ui::trf("chat.type.text", "<%s> %s", { message.source, body });
    case ChatMessage::Kind::Whisper:
        return message.source.empty() ? body : ui::trf("commands.message.display.incoming", "%1$s whispers to you: %2$s", { message.source, body });
    case ChatMessage::Kind::Announcement:
        return message.source.empty() ? body : ui::trf("chat.type.announcement", "[%s] %s", { message.source, body });
    case ChatMessage::Kind::Translation:
        return ui::Localization::shared().translateMessage(message.message, message.parameters);
    case ChatMessage::Kind::Json:
    case ChatMessage::Kind::WhisperJson:
    case ChatMessage::Kind::AnnouncementJson:
        return rawText(message.message);
    case ChatMessage::Kind::Popup:
    case ChatMessage::Kind::JukeboxPopup:
    case ChatMessage::Kind::Tip:
        return {};
    case ChatMessage::Kind::Raw:
    case ChatMessage::Kind::System:
        break;
    }
    return body;
}

}

void Client::syncChat()
{
    for (const ChatMessage& message : session.takeChatMessages()) {
        std::string body = messageBody(message);
        if (std::string line = chatLine(message, body); !line.empty()) {
            menu.addChatLine(std::move(line));
        } else {
            showHudText(message, std::move(body));
        }
    }
    if (std::optional<ActionbarText> actionbar = session.takeActionbar()) {
        actionbarMessage = { actionbar->json ? rawText(actionbar->text) : std::move(actionbar->text), secondsNow() };
    }
    for (TitleRequest& request : session.takeTitles()) {
        applyTitle(std::move(request));
    }
    for (ToastRequest& toast : session.takeToasts()) {
        menu.pushToast(std::move(toast.title), std::move(toast.content));
    }
    for (std::string& text : menu.takeChatMessages()) {
        session.sendChat(std::move(text));
    }
}

/**
 * Puts a popup or tip on the HUD the way the game's GuiData builds them: a
 * popup is its translated source and the message on two lines, a jukebox
 * popup the message over an empty line, and a tip only the message.
 */
void Client::showHudText(const ChatMessage& message, std::string body)
{
    HudMessage shown;
    shown.shown = secondsNow();
    switch (message.kind) {
    case ChatMessage::Kind::Popup:
        shown.text = (message.source.empty() ? std::string() : ui::tr(message.source, message.source)) + "\n" + body;
        shown.hold = PopupHoldPerCharacter * static_cast<float>(codepoints(shown.text));
        popupMessage = std::move(shown);
        break;
    case ChatMessage::Kind::JukeboxPopup:
        shown.text = std::move(body) + "\n";
        shown.hold = JukeboxHoldSeconds;
        shown.jukebox = true;
        popupMessage = std::move(shown);
        break;
    case ChatMessage::Kind::Tip:
        shown.text = std::move(body);
        tipMessage = std::move(shown);
        break;
    default:
        break;
    }
}

/**
 * Keeps the title the way the game's title packet handling does: a title
 * shows with the fade times set last, a subtitle waits for the next title
 * unless one is up, clear takes both away and reset also brings the default
 * times back.
 */
void Client::applyTitle(TitleRequest request)
{
    constexpr float TicksPerSecond = 20.0f;
    std::string text = request.json ? rawText(request.text) : std::move(request.text);
    switch (request.kind) {
    case TitleRequest::Kind::Reset: {
        titleView = menu::HudTitle {};
        break;
    }
    case TitleRequest::Kind::Clear:
        titleView.title.clear();
        titleView.subtitle.clear();
        titleView.serial = 0;
        break;
    case TitleRequest::Kind::Title:
        titleView.title = std::move(text);
        titleView.serial = ++titleSerial;
        titleView.subtitleWithTitle = !titleView.subtitle.empty();
        break;
    case TitleRequest::Kind::Subtitle:
        titleView.subtitle = std::move(text);
        if (titleView.serial != 0) {
            titleView.subtitleSerial = ++titleSerial;
        }
        break;
    case TitleRequest::Kind::Times:
        if (request.fadeIn >= 0) {
            titleView.fadeIn = static_cast<float>(request.fadeIn) / TicksPerSecond;
        }
        if (request.stay >= 0) {
            titleView.stay = static_cast<float>(request.stay) / TicksPerSecond;
        }
        if (request.fadeOut >= 0) {
            titleView.fadeOut = static_cast<float>(request.fadeOut) / TicksPerSecond;
        }
        break;
    }
}

}
