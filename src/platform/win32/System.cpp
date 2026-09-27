#include "platform/System.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

namespace kestrel::platform {

MemoryUsage memoryUsage()
{
    MemoryUsage usage;
    MEMORYSTATUSEX status {};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        usage.physical = status.ullTotalPhys;
    }
    PROCESS_MEMORY_COUNTERS counters {};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        usage.resident = counters.WorkingSetSize;
    }
    return usage;
}

std::string processorName()
{
    char name[128] {};
    DWORD size = sizeof(name);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &size) != ERROR_SUCCESS) {
        return {};
    }
    std::string text(name);
    size_t start = text.find_first_not_of(' ');
    return start == std::string::npos ? std::string() : text.substr(start);
}

}
