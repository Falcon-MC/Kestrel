#include "client/ChatText.h"

#include "Core/Json/Json.h"
#include "ui/Localization.h"

namespace kestrel {

std::string rawText(const std::string& message)
{
    std::unique_ptr<json::Value> root = json::parse(message);
    return root && root->isObject() ? ui::rawText(*root) : message;
}

std::string messageBody(const ChatMessage& message)
{
    std::string body = message.translate ? ui::Localization::shared().translateMessage(message.message, message.parameters) : message.message;
    return message.commandError ? std::string("\xC2\xA7" "c") + body : body;
}

std::string chatLine(const ChatMessage& message, const std::string& body)
{
    switch (message.kind) {
    case ChatMessage::Kind::Chat:
        return message.source.empty() ? body : ui::trf("chat.type.text", "<%s> %s", { message.source, body });
    case ChatMessage::Kind::Whisper:
        return message.source.empty() ? body : ui::trf("commands.message.display.incoming", "%1$s whispers to you: %2$s", { message.source, body });
    case ChatMessage::Kind::Announcement: {
        if (message.source.empty()) return body;
        std::string prefix = ui::trf("chat.type.announcement", "[%s] %s", { message.source, "" });
        return body.starts_with(prefix) ? body : prefix + body;
    }
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
