#pragma once

#include <string_view>

namespace kestrel::mod {

/**
 * Writes to Kestrel's debug.txt and the console, prefixed with the mod id.
 */
class Logger {
public:
    enum class Level {
        Info,
        Warning,
        Error,
    };

    virtual ~Logger() = default;
    virtual void log(Level level, std::string_view message) = 0;

    void info(std::string_view message)
    {
        log(Level::Info, message);
    }

    void warn(std::string_view message)
    {
        log(Level::Warning, message);
    }

    void error(std::string_view message)
    {
        log(Level::Error, message);
    }
};

}
