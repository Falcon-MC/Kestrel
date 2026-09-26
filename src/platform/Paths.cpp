#include "platform/Paths.h"

#include <cstdlib>

namespace kestrel::platform {

std::filesystem::path dataDirectory()
{
    std::filesystem::path root;
#if defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA")) {
        root = appData;
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        root = std::filesystem::path(home) / "Library" / "Application Support";
    }
#else
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data) {
        root = data;
    } else if (const char* home = std::getenv("HOME")) {
        root = std::filesystem::path(home) / ".local" / "share";
    }
#endif
    if (root.empty()) {
        root = std::filesystem::current_path();
    }
    std::filesystem::path directory = root / "Kestrel";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory;
}

}
