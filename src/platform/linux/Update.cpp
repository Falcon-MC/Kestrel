#include "platform/Update.h"
#include "platform/PosixUpdate.h"

namespace kestrel::platform {

std::filesystem::path executablePath()
{
    std::error_code error;
    return std::filesystem::read_symlink("/proc/self/exe", error);
}

std::string updatePlatform()
{
#if defined(__x86_64__)
    return "linux-x64.tar.gz";
#else
    return {};
#endif
}

bool launchUpdate(const std::filesystem::path& replacement, std::string& error)
{
    return launchPosixUpdate(executablePath(), replacement, error);
}

}
