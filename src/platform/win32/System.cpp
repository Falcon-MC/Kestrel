#include "platform/System.h"

#include <thread>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

namespace kestrel::platform {

int bedrockDeviceOS()
{
    return 8;
}

void waitUntil(std::chrono::steady_clock::time_point deadline)
{
    auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return;
    }

    struct Timer {
        HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
        ~Timer()
        {
            if (handle) {
                CloseHandle(handle);
            }
        }
    };
    thread_local Timer timer;
    if (timer.handle) {
        using TimerTicks = std::chrono::duration<long long, std::ratio<1, 10000000>>;
        while (now < deadline) {
            LARGE_INTEGER due;
            due.QuadPart = -std::chrono::ceil<TimerTicks>(deadline - now).count();
            if (!SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE)
                || WaitForSingleObject(timer.handle, INFINITE) != WAIT_OBJECT_0) {
                break;
            }
            now = std::chrono::steady_clock::now();
        }
    }
    if (now < deadline) {
        std::this_thread::sleep_until(deadline);
    }
}

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
