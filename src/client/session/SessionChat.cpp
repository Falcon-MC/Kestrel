#include "SessionData.h"

#include "Network/BedrockConnection.h"
#include "Protocol/Packets/AvailableCommandsPacket.h"
#include "Protocol/Packets/CommandRequestPacket.h"
#include "Protocol/Packets/TextPacket.h"

#include <algorithm>
#include <random>

namespace kestrel {

namespace {

constexpr size_t MaxPendingChat = 256;

// The game gives every command request a fresh version 4 UUID.
Uuid randomUuid()
{
    std::random_device device;
    uint64_t most = (uint64_t(device()) << 32) | device();
    uint64_t least = (uint64_t(device()) << 32) | device();
    most = (most & ~0xF000ull) | 0x4000ull;
    least = (least & ~(0xC000ull << 48)) | (0x8000ull << 48);
    return { most, least };
}

// The labels the game prints for argument types in command usage.
const char* typeLabel(CommandParamType type)
{
    switch (type) {
    case CommandParamType::Int:
        return "int";
    case CommandParamType::Float:
        return "float";
    case CommandParamType::Value:
        return "value";
    case CommandParamType::WildcardInt:
        return "wildcard int";
    case CommandParamType::Operator:
        return "operator";
    case CommandParamType::CompareOperator:
        return "compare operator";
    case CommandParamType::Target:
        return "target";
    case CommandParamType::WildcardTarget:
        return "wildcard target";
    case CommandParamType::Filepath:
        return "filepath";
    case CommandParamType::IntegerRange:
        return "integer range";
    case CommandParamType::EquipmentSlots:
        return "equipment slots";
    case CommandParamType::String:
        return "string";
    case CommandParamType::BlockPosition:
    case CommandParamType::Position:
        return "x y z";
    case CommandParamType::Message:
        return "message";
    case CommandParamType::RawText:
        return "text";
    case CommandParamType::Json:
        return "json";
    case CommandParamType::BlockStates:
        return "block states";
    case CommandParamType::Command:
        return "command";
    }
    return "value";
}

menu::ChatParameter chatParameter(const CommandParamData& data)
{
    menu::ChatParameter parameter;
    parameter.name = data.mName;
    parameter.optional = data.mOptional;
    if (data.mHasEnumData) {
        parameter.type = data.mEnumData.mIsSoft ? "string" : data.mEnumData.mName;
        parameter.values = data.mEnumData.mValues;
        parameter.literal = parameter.values.size() == 1;
        return parameter;
    }
    parameter.type = typeLabel(data.mType);
    parameter.target = data.mType == CommandParamType::Target || data.mType == CommandParamType::WildcardTarget;
    parameter.words = data.mType == CommandParamType::Position || data.mType == CommandParamType::BlockPosition ? 3 : 1;
    parameter.rest = data.mType == CommandParamType::Message || data.mType == CommandParamType::RawText || data.mType == CommandParamType::Json || data.mType == CommandParamType::Command;
    return parameter;
}

std::shared_ptr<const std::vector<menu::ChatCommand>> chatCommands(const AvailableCommandsPacket& packet)
{
    auto commands = std::make_shared<std::vector<menu::ChatCommand>>();
    commands->reserve(packet.mCommands.size());
    for (const CommandData& data : packet.mCommands) {
        menu::ChatCommand& command = commands->emplace_back();
        command.name = data.mName;
        for (const std::string& alias : data.mAliases.mValues) {
            if (alias != data.mName) {
                command.aliases.push_back(alias);
            }
        }
        for (const CommandOverloadData& overload : data.mOverloads) {
            menu::ChatOverload& target = command.overloads.emplace_back();
            for (const CommandParamData& parameter : overload.mParameters) {
                target.parameters.push_back(chatParameter(parameter));
            }
        }
    }
    std::sort(commands->begin(), commands->end(), [](const menu::ChatCommand& a, const menu::ChatCommand& b) { return a.name < b.name; });
    return commands;
}

}

std::vector<ChatMessage> Session::takeChatMessages()
{
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<ChatMessage> messages = std::move(pendingChat);
    pendingChat.clear();
    return messages;
}

void Session::sendChat(std::string text)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (current.state == SessionState::Joined) {
        outgoingChat.push_back(std::move(text));
    }
}

void Session::handleChatPacket(const std::shared_ptr<Packet>& packet)
{
    if (auto available = std::dynamic_pointer_cast<AvailableCommandsPacket>(packet)) {
        std::shared_ptr<const std::vector<menu::ChatCommand>> commands = chatCommands(*available);
        std::lock_guard<std::mutex> guard(mutex);
        current.commands = std::move(commands);
        return;
    }
    auto text = std::dynamic_pointer_cast<TextPacket>(packet);
    if (!text) {
        return;
    }
    ChatMessage message;
    message.kind = static_cast<ChatMessage::Kind>(text->mType);
    message.source = std::move(text->mSourceName);
    message.message = std::move(text->mMessage);
    message.parameters = std::move(text->mParameters);
    message.translate = text->mNeedsTranslation;
    std::lock_guard<std::mutex> guard(mutex);
    if (pendingChat.size() < MaxPendingChat) {
        pendingChat.push_back(std::move(message));
    }
}

void Session::flushChat()
{
    std::vector<std::string> lines;
    std::string sender;
    {
        std::lock_guard<std::mutex> guard(mutex);
        lines = std::move(outgoingChat);
        outgoingChat.clear();
        sender = current.displayName;
    }
    for (std::string& line : lines) {
        if (line.empty()) {
            continue;
        }
        if (line.front() == '/') {
            CommandRequestPacket request;
            request.mCommand = std::move(line);
            request.mOrigin.mOrigin = CommandOriginType::Player;
            request.mOrigin.mUuid = randomUuid();
            connection->send(request);
        } else {
            TextPacket chat;
            chat.mType = TextPacket::Type::Chat;
            chat.mSourceName = sender;
            chat.mMessage = std::move(line);
            chat.mXuid = localXuid;
            connection->send(chat);
        }
    }
}

}
