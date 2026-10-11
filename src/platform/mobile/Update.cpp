#include "platform/Update.h"

namespace kestrel::platform {

std::filesystem::path executablePath()
{
    return {};
}

std::string updatePlatform()
{
    // Mobile applications are replaced by their package installer, not by the desktop updater.
    return {};
}

bool launchUpdate(const std::filesystem::path&, std::string& error)
{
    error = "Install the updated application package to update Kestrel on mobile";
    return false;
}

}
