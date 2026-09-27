#pragma once

#include <cstdint>
#include <string>

namespace kestrel::platform {

struct MemoryUsage {
    uint64_t resident = 0;
    uint64_t physical = 0;
};

MemoryUsage memoryUsage();
std::string processorName();

}
