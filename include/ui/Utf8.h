#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kestrel::ui {

inline char32_t nextCodepoint(std::string_view text, size_t& i)
{
    uint8_t lead = static_cast<uint8_t>(text[i++]);
    if (lead < 0x80) {
        return lead;
    }

    int extra = 0;
    char32_t cp = 0;
    if ((lead & 0xE0) == 0xC0) {
        extra = 1;
        cp = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        extra = 2;
        cp = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        extra = 3;
        cp = lead & 0x07;
    } else {
        return U'?';
    }

    for (int n = 0; n < extra; ++n) {
        if (i >= text.size() || (static_cast<uint8_t>(text[i]) & 0xC0) != 0x80) {
            return U'?';
        }
        cp = (cp << 6) | (static_cast<uint8_t>(text[i++]) & 0x3F);
    }
    return cp;
}

inline void appendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x110000) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

inline void popUtf8(std::string& text)
{
    if (text.empty()) {
        return;
    }
    size_t i = text.size() - 1;
    while (i > 0 && (static_cast<uint8_t>(text[i]) & 0xC0) == 0x80) {
        --i;
    }
    text.erase(i);
}

inline void dropFirstUtf8(std::string& text)
{
    if (text.empty()) {
        return;
    }
    size_t i = 0;
    nextCodepoint(text, i);
    text.erase(0, i);
}

}
