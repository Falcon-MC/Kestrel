#include "platform/Update.h"
#include "platform/PosixUpdate.h"

#include <libproc.h>

namespace kestrel::platform {

std::filesystem::path executablePath()
{
    char path[PROC_PIDPATHINFO_MAXSIZE] {};
    return proc_pidpath(getpid(), path, sizeof(path)) > 0 ? std::filesystem::path(path) : std::filesystem::path();
}

std::string updatePlatform()
{
#if defined(__aarch64__) || defined(__arm64__)
    return "macos-arm64.tar.gz";
#else
    return {};
#endif
}

bool launchUpdate(const std::filesystem::path& replacement, std::string& error)
{
    return launchPosixUpdate(executablePath(), replacement, error);
}

}
