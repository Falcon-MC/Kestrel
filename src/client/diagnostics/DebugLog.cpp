#include "client/DebugLog.h"

#include "platform/Paths.h"

#include <chrono>
#include <cstdio>
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

double processMilliseconds()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - logStart).count();
}

StartupTimer::StartupTimer()
    : last(processMilliseconds())
{
}

void StartupTimer::mark(const std::string& step)
{
    double now = processMilliseconds();
    char amount[32];
    std::snprintf(amount, sizeof(amount), " %.1f ms", now - last);
    debugLog("startup " + step + amount);
    last = now;
}

void resetDebugLog()
{
    std::lock_guard<std::mutex> guard(logMutex);
    std::error_code error;
    const auto path = logPath();
    if (std::filesystem::exists(path, error)) {
        auto previous = path.parent_path() / "debug.previous.txt";
        std::filesystem::copy_file(path, previous, std::filesystem::copy_options::overwrite_existing, error);
    }
    std::ofstream file(logPath(), std::ios::trunc);
}

}
