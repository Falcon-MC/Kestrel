#include "client/DebugLog.h"

#include "platform/Paths.h"

#include <chrono>
#include <fstream>
#include <mutex>

namespace kestrel {

namespace {

std::mutex logMutex;
const auto logStart = std::chrono::steady_clock::now();

std::filesystem::path logPath()
{
    return platform::dataDirectory() / "debug.txt";
}

}

void debugLog(const std::string& line)
{
    std::lock_guard<std::mutex> guard(logMutex);
    std::ofstream file(logPath(), std::ios::app);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - logStart).count();
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "[%9.3f] ", seconds);
    file << stamp << line << '\n';
}

void resetDebugLog()
{
    std::lock_guard<std::mutex> guard(logMutex);
    std::ofstream file(logPath(), std::ios::trunc);
}

}
