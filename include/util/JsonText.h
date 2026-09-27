#pragma once

#include "Core/Json/Json.h"

#include <memory>
#include <string>

namespace kestrel::util {

/**
 * The JSON text with its // and block comments removed, the way pack files
 * are allowed to carry them.
 */
std::string stripJsonComments(const std::string& source);

/**
 * Parses a pack JSON file, comments allowed; null when it does not hold an
 * object.
 */
std::unique_ptr<json::Value> parseJsonObject(const std::string& text);

float jsonNumber(const json::Value* value, float fallback);
bool jsonBool(const json::Value* value, bool fallback);

/**
 * A number, or the two ends of a [low, high] pair, into low and high; both
 * stay unchanged for any other value.
 */
void jsonRange(const json::Value* value, float& low, float& high);

}
