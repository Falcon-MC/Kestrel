#include "util/Text.h"

#include <cctype>
#include <charconv>
#if defined(__APPLE__)
#include <cstdlib>
#include <locale.h>
#endif

namespace kestrel::util {

float parseFloat(std::string_view text)
{
#if defined(__APPLE__)
    static locale_t locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
    return strtof_l(std::string(text).c_str(), nullptr, locale);
#else
    float value = 0.0f;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
#endif
}

bool startsWith(std::string_view value, std::string_view prefix)
{
    return value.substr(0, prefix.size()) == prefix;
}

bool endsWith(std::string_view value, std::string_view suffix)
{
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

bool contains(std::string_view value, std::string_view part)
{
    return value.find(part) != std::string_view::npos;
}

std::string lowercase(std::string text)
{
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string trim(std::string text)
{
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(start, end - start);
}

std::string withoutNamespace(const std::string& identifier)
{
    size_t colon = identifier.find(':');
    return colon == std::string::npos ? identifier : identifier.substr(colon + 1);
}

std::vector<uint64_t> versionNumbers(const std::string& text)
{
    std::vector<uint64_t> numbers;
    uint64_t current = 0;
    bool inNumber = false;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            current = current * 10 + uint64_t(c - '0');
            inNumber = true;
        } else if (inNumber) {
            numbers.push_back(current);
            current = 0;
            inNumber = false;
        }
    }
    if (inNumber) {
        numbers.push_back(current);
    }
    return numbers;
}

}
