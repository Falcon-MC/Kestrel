#include "client/Client.h"

#include "Core/Json/Json.h"
#include "ui/Localization.h"

namespace kestrel {

namespace {

std::string rawText(const std::string& message)
{
    std::unique_ptr<json::Value> root = json::parse(message);
    return root && root->isObject() ? ui::rawText(*root) : message;
}

/**
 * The line a text packet puts in chat, formatted the way the game formats
 * each kind. Popups and tips show above the hotbar instead, so they give none.
 */
std::string chatLine(const ChatMessage& message)
{
    const ui::Localization& texts = ui::Localization::shared();
    std::string body = message.translate ? texts.translateMessage(message.message, message.parameters) : message.message;
    switch (message.kind) {
    case ChatMessage::Kind::Chat:
        return message.source.empty() ? body : ui::trf("chat.type.text", "<%s> %s", { message.source, body });
    case ChatMessage::Kind::Whisper:
        return message.source.empty() ? body : ui::trf("commands.message.display.incoming", "%1$s whispers to you: %2$s", { message.source, body });
    case ChatMessage::Kind::Announcement:
        return message.source.empty() ? body : ui::trf("chat.type.announcement", "[%s] %s", { message.source, body });
    case ChatMessage::Kind::Translation:
        return texts.translateMessage(message.message, message.parameters);
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
        if (std::string line = chatLine(message); !line.empty()) {
            menu.addChatLine(std::move(line));
        }
    }
    for (std::string& text : menu.takeChatMessages()) {
        session.sendChat(std::move(text));
    }
}

}
