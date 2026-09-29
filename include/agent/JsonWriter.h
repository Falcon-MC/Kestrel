#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace kestrel::agent {

/**
 * Builds JSON text in one pass. Commas are placed for you; keys only make
 * sense inside objects, and nothing checks that, so keep the nesting honest.
 */
class JsonWriter {
public:
    JsonWriter& beginObject();
    JsonWriter& endObject();
    JsonWriter& beginArray();
    JsonWriter& endArray();
    JsonWriter& key(std::string_view name);

    JsonWriter& value(std::string_view text);
    JsonWriter& value(const char* text);
    JsonWriter& value(const std::string& text);
    JsonWriter& value(bool flag);

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
    JsonWriter& value(T number)
    {
        separate();
        out += std::to_string(number);
        return *this;
    }

    JsonWriter& value(double number);
    JsonWriter& value(float number);
    JsonWriter& null();

    /**
     * Inserts text that already is valid JSON.
     */
    JsonWriter& raw(std::string_view json);

    template <typename T>
    JsonWriter& field(std::string_view name, const T& item)
    {
        return key(name).value(item);
    }

    std::string take();

private:
    void separate();

    std::string out;
    std::vector<bool> filled;
    bool keyed = false;
};

void appendEscaped(std::string& out, std::string_view text);

}
