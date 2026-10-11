#include "Resources.h"
#include "platform/Paths.h"
#include "world/PackSource.h"

namespace kestrel::platform {
namespace fs = std::filesystem;

namespace {
constexpr const char* TouchControlsFile = "resource_packs/vanilla/ui/kestrel_touch_controls.json";
}

void refreshTouchControls(const fs::path& bundle)
{
    fs::path vanilla = world::PackSource::locateVanilla();
    fs::create_directories(vanilla / "ui");
    fs::copy_file(bundle / TouchControlsFile, vanilla / "ui/kestrel_touch_controls.json", fs::copy_options::overwrite_existing);
}

fs::path importedFile(FileKind kind)
{
    fs::path directory = dataDirectory() / "imports";
    std::error_code error;
    fs::create_directories(directory, error);
    return directory / (kind == FileKind::Png ? "picked.png" : "picked.mcpack");
}

}
