#include "agent/JsonWriter.h"

#include <charconv>
#include <cmath>

namespace kestrel::agent {

void appendEscaped(std::string& out, std::string_view text)
{
    static constexpr char Hex[] = "0123456789abcdef";
    out.push_back('"');
    for (char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += "\\u00";
                out.push_back(Hex[(c >> 4) & 0xF]);
                out.push_back(Hex[c & 0xF]);
            } else {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
}

void JsonWriter::separate()
{
    if (keyed) {
        keyed = false;
        return;
    }
    if (!filled.empty()) {
        if (filled.back()) {
            out.push_back(',');
        }
        filled.back() = true;
    }
}

JsonWriter& JsonWriter::beginObject()
{
    separate();
    out.push_back('{');
    filled.push_back(false);
    return *this;
}

JsonWriter& JsonWriter::endObject()
{
    out.push_back('}');
    filled.pop_back();
    return *this;
}

JsonWriter& JsonWriter::beginArray()
{
    separate();
    out.push_back('[');
    filled.push_back(false);
    return *this;
}

JsonWriter& JsonWriter::endArray()
{
    out.push_back(']');
    filled.pop_back();
    return *this;
}

JsonWriter& JsonWriter::key(std::string_view name)
{
    separate();
    appendEscaped(out, name);
    out.push_back(':');
    keyed = true;
    return *this;
}

JsonWriter& JsonWriter::value(std::string_view text)
{
    separate();
    appendEscaped(out, text);
    return *this;
}

JsonWriter& JsonWriter::value(const char* text)
{
    return value(std::string_view(text));
}

JsonWriter& JsonWriter::value(const std::string& text)
{
    return value(std::string_view(text));
}

JsonWriter& JsonWriter::value(bool flag)
{
    separate();
    out += flag ? "true" : "false";
    return *this;
}

JsonWriter& JsonWriter::value(double number)
{
    separate();
    if (!std::isfinite(number)) {
        out += "null";
        return *this;
    }
    char buffer[32];
    auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), number);
    out.append(buffer, error == std::errc() ? end : buffer);
    return *this;
}

JsonWriter& JsonWriter::value(float number)
{
    return value(static_cast<double>(number));
}

JsonWriter& JsonWriter::null()
{
    separate();
    out += "null";
    return *this;
}

JsonWriter& JsonWriter::raw(std::string_view json)
{
    separate();
    out += json;
    return *this;
}

std::string JsonWriter::take()
{
    filled.clear();
    keyed = false;
    return std::move(out);
}

}
