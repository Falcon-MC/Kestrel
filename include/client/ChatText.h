#pragma once

#include "client/Session.h"

#include <string>

namespace kestrel {

/**
 * The plain text of a JSON text component, or the message itself when it
 * does not hold one.
 */
std::string rawText(const std::string& message);

/**
 * The message of a text packet in the menus' language.
 */
std::string messageBody(const ChatMessage& message);

/**
 * The line a text packet puts in chat, formatted the way the game formats
 * each kind. Popups and tips show above the hotbar instead, so they give none.
 */
std::string chatLine(const ChatMessage& message, const std::string& body);

}
