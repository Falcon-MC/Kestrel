#include "JsonUiInternal.h"

#include <cstdlib>

namespace kestrel::ui::jsonui {

namespace {

    constexpr int MaxNesting = 1000;

    /**
     * Reads ui/*.json the way the game's comment tolerant jsoncpp reader does:
     * comments separate tokens, numbers may carry leading zeros, strings may
     * hold raw control characters and text after the root value is ignored,
     * while trailing commas and bare names stay errors.
     */
    class Reader {
    public:
        explicit Reader(std::string_view source)
            : text(source)
        {
        }

        std::unique_ptr<json::Value> value()
        {
            if (!skipInsignificant()) {
                return nullptr;
            }
            if (pos >= text.size()) {
                return nullptr;
            }
            switch (text[pos]) {
            case '{':
                return nested(&Reader::object);
            case '[':
                return nested(&Reader::array);
            case '"': {
                std::string out;
                if (!string(out)) {
                    return nullptr;
                }
                return json::Value::ofString(std::move(out));
            }
            case 't':
                return literal("true", json::Value::ofBoolean(true));
            case 'f':
                return literal("false", json::Value::ofBoolean(false));
            case 'n':
                return literal("null", json::Value::ofNull());
            default:
                if (text[pos] == '-' || (text[pos] >= '0' && text[pos] <= '9')) {
                    return number();
                }
                return nullptr;
            }
        }

    private:
        char peek(size_t ahead = 0) const
        {
            return pos + ahead < text.size() ? text[pos + ahead] : '\0';
        }

        bool skipInsignificant()
        {
            for (;;) {
                while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n')) {
                    ++pos;
                }
                if (peek() != '/') {
                    return true;
                }
                if (peek(1) == '/') {
                    while (pos < text.size() && text[pos] != '\n') {
                        ++pos;
                    }
                } else if (peek(1) == '*') {
                    pos += 2;
                    for (;;) {
                        if (pos >= text.size()) {
                            return false;
                        }
                        if (text[pos] == '*' && peek(1) == '/') {
                            pos += 2;
                            break;
                        }
                        ++pos;
                    }
                } else {
                    return false;
                }
            }
        }

        std::unique_ptr<json::Value> nested(std::unique_ptr<json::Value> (Reader::*read)())
        {
            if (depth >= MaxNesting) {
                return nullptr;
            }
            ++depth;
            std::unique_ptr<json::Value> result = (this->*read)();
            --depth;
            return result;
        }

        std::unique_ptr<json::Value> literal(std::string_view word, std::unique_ptr<json::Value> result)
        {
            if (text.substr(pos, word.size()) != word) {
                return nullptr;
            }
            pos += word.size();
            return result;
        }

        std::unique_ptr<json::Value> object()
        {
            ++pos;
            std::unique_ptr<json::Value> result = json::Value::ofObject();
            std::string name;
            for (;;) {
                if (!skipInsignificant()) {
                    return nullptr;
                }
                if (peek() == '}' && name.empty()) {
                    ++pos;
                    return result;
                }
                if (peek() != '"') {
                    return nullptr;
                }
                name.clear();
                if (!string(name)) {
                    return nullptr;
                }
                if (!skipInsignificant() || peek() != ':') {
                    return nullptr;
                }
                ++pos;
                std::unique_ptr<json::Value> member = value();
                if (!member) {
                    return nullptr;
                }
                result->set(name, std::move(member));
                if (!skipInsignificant()) {
                    return nullptr;
                }
                if (peek() == ',') {
                    ++pos;
                } else if (peek() == '}') {
                    ++pos;
                    return result;
                } else {
                    return nullptr;
                }
            }
        }

        std::unique_ptr<json::Value> array()
        {
            ++pos;
            std::unique_ptr<json::Value> result = json::Value::ofArray();
            while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n')) {
                ++pos;
            }
            if (peek() == ']') {
                ++pos;
                return result;
            }
            for (;;) {
                std::unique_ptr<json::Value> item = value();
                if (!item) {
                    return nullptr;
                }
                result->push(std::move(item));
                if (!skipInsignificant()) {
                    return nullptr;
                }
                if (peek() == ',') {
                    ++pos;
                } else if (peek() == ']') {
                    ++pos;
                    return result;
                } else {
                    return nullptr;
                }
            }
        }

        /**
         * The characters jsoncpp's number token spans, read as an integer when
         * it has no fraction or exponent, else as its longest leading float.
         */
        std::unique_ptr<json::Value> number()
        {
            size_t start = pos;
            while (pos < text.size()) {
                char c = text[pos];
                if (!((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')) {
                    break;
                }
                ++pos;
            }
            std::string token(text.substr(start, pos - start));
            bool integral = token.find_first_of(".eE+-", 1) == std::string::npos;
            if (integral) {
                char* end = nullptr;
                long long whole = std::strtoll(token.c_str(), &end, 10);
                if (end && *end == '\0' && !token.empty() && token != "-") {
                    return json::Value::ofInteger(whole);
                }
            }
            for (size_t length = token.size(); length > 0; --length) {
                std::string part = token.substr(0, length);
                char* end = nullptr;
                double parsed = std::strtod(part.c_str(), &end);
                if (end != part.c_str() && *end == '\0') {
                    return json::Value::ofNumber(parsed);
                }
            }
            return json::Value::ofNumber(0.0);
        }

        bool hex4(uint32_t& out)
        {
            if (pos + 4 > text.size()) {
                return false;
            }
            out = 0;
            for (int i = 0; i < 4; ++i) {
                char digit = text[pos++];
                out <<= 4;
                if (digit >= '0' && digit <= '9') {
                    out |= static_cast<uint32_t>(digit - '0');
                } else if (digit >= 'a' && digit <= 'f') {
                    out |= static_cast<uint32_t>(digit - 'a' + 10);
                } else if (digit >= 'A' && digit <= 'F') {
                    out |= static_cast<uint32_t>(digit - 'A' + 10);
                } else {
                    return false;
                }
            }
            return true;
        }

        static void appendUtf8(std::string& out, uint32_t code)
        {
            if (code < 0x80) {
                out.push_back(static_cast<char>(code));
            } else if (code < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else if (code < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
        }

        bool string(std::string& out)
        {
            ++pos;
            for (;;) {
                if (pos >= text.size()) {
                    return false;
                }
                char c = text[pos++];
                if (c == '"') {
                    return true;
                }
                if (c != '\\') {
                    out.push_back(c);
                    continue;
                }
                if (pos >= text.size()) {
                    return false;
                }
                char escape = text[pos++];
                switch (escape) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(escape);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    uint32_t high = 0;
                    if (!hex4(high)) {
                        return false;
                    }
                    uint32_t code = high;
                    if (high >= 0xD800 && high < 0xDC00) {
                        if (peek() != '\\' || peek(1) != 'u') {
                            return false;
                        }
                        pos += 2;
                        uint32_t low = 0;
                        if (!hex4(low)) {
                            return false;
                        }
                        code = 0x10000 + ((high & 0x3FF) << 10) + (low & 0x3FF);
                    }
                    if (code > 0x10FFFF || (code >= 0xD800 && code < 0xE000)) {
                        return false;
                    }
                    appendUtf8(out, code);
                    break;
                }
                default:
                    return false;
                }
            }
        }

        std::string_view text;
        size_t pos = 0;
        int depth = 0;
    };

}

std::unique_ptr<json::Value> readUiJson(std::string_view source)
{
    constexpr std::string_view Bom = "\xEF\xBB\xBF";
    if (source.substr(0, Bom.size()) == Bom) {
        source.remove_prefix(Bom.size());
    }
    Reader reader(source);
    return reader.value();
}

}
