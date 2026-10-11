#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel::util {

bool startsWith(std::string_view value, std::string_view prefix);
bool endsWith(std::string_view value, std::string_view suffix);
bool contains(std::string_view value, std::string_view part);

/**
 * The text in ASCII lower case.
 */
std::string lowercase(std::string text);

/**
 * The text without leading and trailing whitespace.
 */
std::string trim(std::string text);

/**
 * An identifier without its namespace: "minecraft:stone" becomes "stone".
 */
std::string withoutNamespace(const std::string& identifier);

/**
 * The numbers of a version such as 1.26.51.1, so versions compare by value:
 * 1.26 is newer than 1.9.
 */
std::vector<uint64_t> versionNumbers(const std::string& text);

float parseFloat(std::string_view text);

}
