#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel::mod {

/**
 * A mod's own settings as key=value lines in mods/<id>/config.txt. Changes
 * are written when the mod unloads, or right away with save().
 */
class Config {
public:
    virtual ~Config() = default;

    virtual std::optional<std::string> find(std::string_view key) const = 0;
    virtual void put(std::string_view key, std::string value) = 0;
    virtual void remove(std::string_view key) = 0;
    virtual std::vector<std::string> keys() const = 0;
    virtual void save() = 0;

    bool contains(std::string_view key) const
    {
        return find(key).has_value();
    }

    std::string getString(std::string_view key, std::string fallback = {}) const
    {
        std::optional<std::string> value = find(key);
        return value ? *value : std::move(fallback);
    }

    bool getBool(std::string_view key, bool fallback) const
    {
        std::optional<std::string> value = find(key);
        if (!value) {
            return fallback;
        }
        return *value == "true" || *value == "1";
    }

    long long getInt(std::string_view key, long long fallback) const
    {
        std::optional<std::string> value = find(key);
        try {
            return value ? std::stoll(*value) : fallback;
        } catch (const std::exception&) {
            return fallback;
        }
    }

    double getDouble(std::string_view key, double fallback) const
    {
        std::optional<std::string> value = find(key);
        try {
            return value ? std::stod(*value) : fallback;
        } catch (const std::exception&) {
            return fallback;
        }
    }

    void set(std::string_view key, std::string_view value)
    {
        put(key, std::string(value));
    }

    void set(std::string_view key, const char* value)
    {
        put(key, value);
    }

    void set(std::string_view key, bool value)
    {
        put(key, value ? "true" : "false");
    }

    void set(std::string_view key, long long value)
    {
        put(key, std::to_string(value));
    }

    void set(std::string_view key, int value)
    {
        put(key, std::to_string(value));
    }

    void set(std::string_view key, double value)
    {
        put(key, std::to_string(value));
    }
};

}
