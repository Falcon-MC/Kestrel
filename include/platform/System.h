#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace kestrel::platform {

struct MemoryUsage {
    uint64_t resident = 0;
    uint64_t physical = 0;
};

MemoryUsage memoryUsage();
int bedrockDeviceOS();
std::string processorName();
void waitUntil(std::chrono::steady_clock::time_point deadline);

}
