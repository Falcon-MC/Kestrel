#include "Resources.h"
#include "platform/Paths.h"
#include "world/ServerPack.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace kestrel::platform {
namespace fs = std::filesystem;

fs::path iosResources() { return dataDirectory() / "assets"; }

bool iosResourcesReady()
{
    std::error_code error;
    return fs::exists(iosResources() / ".ready", error)
        && fs::exists(iosResources() / "resource_packs/vanilla/blocks.json", error)
        && fs::exists(iosResources() / "resource_packs/vanilla/__brarchive/font.brarchive", error);
}

void installIosResources(const fs::path& archive, const fs::path& bundle)
{
    if (fs::file_size(archive) > 512ull * 1024 * 1024) throw std::runtime_error("Minecraft resource download is too large");
    std::ifstream file(archive, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read downloaded Minecraft resources");
    std::string bytes { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    std::string error;
    auto pack = world::loadServerPackData(std::move(bytes), {}, error, "resource_pack/");
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
    fs::copy(bundle / "resource_packs/vanilla/font", vanilla / "font", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    fs::copy(bundle / "resource_packs/vanilla/__brarchive", vanilla / "__brarchive", fs::copy_options::recursive);
    fs::copy(bundle / "gui", staging / "gui", fs::copy_options::recursive);
    fs::copy_file(bundle / "resource_packs/vanilla/ui/kestrel_touch_controls.json", vanilla / "ui/kestrel_touch_controls.json", fs::copy_options::overwrite_existing);
    std::ofstream marker(staging / ".ready");
    marker << "Mojang/bedrock-samples v1.26.50.4\n";
    marker.close();
    if (!marker) throw std::runtime_error("Cannot finish installing Minecraft resources");
    fs::remove_all(iosResources());
    fs::rename(staging, iosResources());
}
}
