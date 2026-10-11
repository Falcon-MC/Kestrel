#include "platform/Paths.h"

#include <cstdlib>

namespace kestrel::platform {

std::filesystem::path startupFont()
{
#if defined(KESTREL_MOBILE)
    if (const char* directory = std::getenv("KESTREL_FONTS")) {
        auto path = std::filesystem::path(directory) / "Monocraft.ttf";
        if (std::filesystem::is_regular_file(path)) return path;
    }
#endif
    // The first-run screen must be readable before any game or fallback fonts exist.
#if defined(_WIN32)
    if (const char* root = std::getenv("WINDIR")) {
        for (const char* name : {"segoeui.ttf", "arial.ttf"}) {
            auto path = std::filesystem::path(root) / "Fonts" / name;
            if (std::filesystem::is_regular_file(path)) return path;
        }
    }
#elif defined(__APPLE__)
    for (const char* path : {"/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf"}) {
        if (std::filesystem::is_regular_file(path)) return path;
    }
#else
    for (const char* path : {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"}) {
        if (std::filesystem::is_regular_file(path)) return path;
    }
#endif
    return {};
}

void useDownloadedAssets(const std::filesystem::path& directory)
{
    auto vanilla = directory / "resource_packs/vanilla";
    auto fonts = directory / "fonts";
#if defined(_WIN32)
    _putenv_s("KESTREL_VANILLA_PACK", vanilla.string().c_str());
    _putenv_s("KESTREL_FONTS", fonts.string().c_str());
#else
    setenv("KESTREL_VANILLA_PACK", vanilla.c_str(), 1);
    setenv("KESTREL_FONTS", fonts.c_str(), 1);
#endif
}

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
