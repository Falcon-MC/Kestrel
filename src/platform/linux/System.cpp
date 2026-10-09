#include "platform/System.h"

#include <fstream>
#include <thread>

#include <unistd.h>

namespace kestrel::platform {

void waitUntil(std::chrono::steady_clock::time_point deadline)
{
    std::this_thread::sleep_until(deadline);
}

MemoryUsage memoryUsage()
{
    MemoryUsage usage;
    long page = sysconf(_SC_PAGESIZE);
    long pages = sysconf(_SC_PHYS_PAGES);
    if (page > 0 && pages > 0) {
        usage.physical = static_cast<uint64_t>(page) * static_cast<uint64_t>(pages);
    }
    std::ifstream statm("/proc/self/statm");
    uint64_t size = 0;
    uint64_t resident = 0;
    if (page > 0 && statm >> size >> resident) {
        usage.resident = resident * static_cast<uint64_t>(page);
    }
    return usage;
}

std::string processorName()
{
    std::ifstream info("/proc/cpuinfo");
    std::string line;
    while (std::getline(info, line)) {
        if (line.rfind("model name", 0) != 0) {
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            break;
        }
        size_t start = line.find_first_not_of(" \t", colon + 1);
        return start == std::string::npos ? std::string() : line.substr(start);
    }
    return {};
}

}
