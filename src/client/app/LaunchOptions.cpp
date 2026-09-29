#include "client/LaunchOptions.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace kestrel {

namespace {

uint32_t dimension(const std::string& text, uint32_t low)
{
    unsigned long value = std::strtoul(text.c_str(), nullptr, 10);
    return std::clamp<uint32_t>(static_cast<uint32_t>(std::min<unsigned long>(value, 16384)), low, 16384);
}

}

LaunchOptions LaunchOptions::parse(const std::vector<std::string>& arguments)
{
    LaunchOptions options;
    auto next = [&](size_t& i) -> const std::string& {
        if (i + 1 >= arguments.size()) {
            throw std::runtime_error(arguments[i] + " needs a value");
        }
        return arguments[++i];
    };
    for (size_t i = 0; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        if (argument == "--connect") {
            options.connect = next(i);
        } else if (argument == "--name") {
            options.name = next(i);
        } else if (argument == "--agent") {
            options.agent = true;
        } else if (argument == "--agent-port") {
            unsigned long port = std::strtoul(next(i).c_str(), nullptr, 10);
            if (port > 65535) {
                throw std::runtime_error("--agent-port must be below 65536");
            }
            options.agent = true;
            options.agentPort = static_cast<uint16_t>(port);
        } else if (argument == "--hidden") {
            options.hidden = true;
        } else if (argument == "--headless") {
            options.headless = true;
        } else if (argument == "--size") {
            const std::string& size = next(i);
            size_t cross = size.find('x');
            if (cross == std::string::npos) {
                throw std::runtime_error("--size takes <width>x<height>");
            }
            options.width = dimension(size.substr(0, cross), 320);
            options.height = dimension(size.substr(cross + 1), 240);
        } else {
            throw std::runtime_error("Unknown option " + argument);
        }
    }
    if (const char* token = std::getenv("KESTREL_AGENT_TOKEN"); token && *token) {
        options.agentToken = token;
    }
    return options;
}

}
