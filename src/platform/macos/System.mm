#include "platform/System.h"

#include <mach/mach.h>
#include <sys/sysctl.h>

namespace kestrel::platform {

MemoryUsage memoryUsage()
{
    MemoryUsage usage;
    uint64_t physical = 0;
    size_t length = sizeof(physical);
    if (sysctlbyname("hw.memsize", &physical, &length, nullptr, 0) == 0) {
        usage.physical = physical;
    }
    mach_task_basic_info_data_t info {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        usage.resident = info.resident_size;
    }
    return usage;
}

std::string processorName()
{
    char name[128] {};
    size_t length = sizeof(name);
    if (sysctlbyname("machdep.cpu.brand_string", name, &length, nullptr, 0) != 0) {
        return {};
    }
    return name;
}

}
