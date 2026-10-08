#include "Resources.h"
#include "platform/Paths.h"
#include "world/ServerPack.h"

#include <fstream>
#include <stdexcept>

namespace kestrel::platform {
namespace fs = std::filesystem;

namespace {

constexpr size_t MaxDownloadSize = 512ull * 1024 * 1024;
constexpr const char* TouchControlsFile = "resource_packs/vanilla/ui/kestrel_touch_controls.json";

/**
 * The game's own fonts and HTML menus only ship in builds made next to an installed game; without them the
 * app draws text with the free fonts and leaves the HTML menu icons out.
 */
void copyIfShipped(const fs::path& source, const fs::path& destination)
{
    std::error_code error;
    if (!fs::exists(source, error)) {
        return;
    }
    fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
}

}

fs::path mobileResources() { return dataDirectory() / "assets"; }

bool mobileResourcesReady()
{
    std::error_code error;
    std::ifstream marker(mobileResources() / ".ready");
    std::string installed;
    std::getline(marker, installed);
    return installed == MobileResourceUrl && fs::exists(mobileResources() / "resource_packs/vanilla/blocks.json", error);
}

void installMobileResources(std::string archive, const fs::path& bundle)
{
    if (archive.size() > MaxDownloadSize) throw std::runtime_error("Minecraft resource download is too large");
    std::string error;
    auto pack = world::loadServerPackData(std::move(archive), {}, error, "resource_pack/");
    if (!pack) throw std::runtime_error("Cannot open Mojang's resource pack: " + error);
    if (!pack->find("blocks.json")) throw std::runtime_error("Mojang's download has no block definitions");
    fs::path staging = dataDirectory() / "assets-staging";
    fs::remove_all(staging);
    fs::path vanilla = staging / "resource_packs/vanilla";
    fs::create_directories(vanilla);
    for (const auto& path : pack->paths()) {
        auto data = pack->find(path);
        if (!data) throw std::runtime_error("Cannot extract Minecraft resource: " + path);
        fs::path destination = vanilla / path;
        fs::create_directories(destination.parent_path());
        std::ofstream output(destination, std::ios::binary);
        output.write(data->data(), static_cast<std::streamsize>(data->size()));
        if (!output) throw std::runtime_error("Cannot save Minecraft resources");
    }
    copyIfShipped(bundle / "resource_packs/vanilla/font", vanilla / "font");
    copyIfShipped(bundle / "resource_packs/vanilla/__brarchive", vanilla / "__brarchive");
    copyIfShipped(bundle / "gui", staging / "gui");
    fs::copy_file(bundle / TouchControlsFile, staging / TouchControlsFile, fs::copy_options::overwrite_existing);
    std::ofstream marker(staging / ".ready");
    marker << MobileResourceUrl << '\n';
    marker.close();
    if (!marker) throw std::runtime_error("Cannot finish installing Minecraft resources");
    fs::remove_all(mobileResources());
    fs::rename(staging, mobileResources());
}

void refreshTouchControls(const fs::path& bundle)
{
    fs::copy_file(bundle / TouchControlsFile, mobileResources() / TouchControlsFile, fs::copy_options::overwrite_existing);
}

fs::path importedFile(FileKind kind)
{
    fs::path directory = dataDirectory() / "imports";
    std::error_code error;
    fs::create_directories(directory, error);
    return directory / (kind == FileKind::Png ? "picked.png" : "picked.mcpack");
}

}
