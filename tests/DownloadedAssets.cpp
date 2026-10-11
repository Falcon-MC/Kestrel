#include "world/DownloadedAssets.h"

#include <zlib.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
namespace fs = std::filesystem;
void check(bool value, const char* message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
void little(std::string& out, uint32_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i) out.push_back(static_cast<char>(value >> (i * 8)));
}
std::string archive(bool blocks)
{
    std::vector<std::pair<std::string, std::string>> entries {
        {"behavior_pack/manifest.json", "{\"header\":{}}"},
        {"behavior_pack/blocks.json", "wrong pack"},
        {"resource_pack/manifest.json", "{\"header\":{}}"},
        {"resource_pack/textures/test.txt", "synthetic texture"},
        {"resource_pack/../escape.txt", "unsafe"}
    };
    if (blocks) entries.emplace_back("resource_pack/blocks.json", "{}");
    std::string out, directory;
    for (const auto& [name, data] : entries) {
        auto offset = static_cast<uint32_t>(out.size());
        auto size = static_cast<uint32_t>(data.size());
        auto crc = crc32(0, reinterpret_cast<const Bytef*>(data.data()), size);
        little(out, 0x04034b50, 4); little(out, 20, 2);
        out.append(8, '\0'); little(out, crc, 4); little(out, size, 4); little(out, size, 4);
        little(out, name.size(), 2); little(out, 0, 2); out += name; out += data;
        little(directory, 0x02014b50, 4); little(directory, 20, 2); little(directory, 20, 2);
        directory.append(8, '\0'); little(directory, crc, 4); little(directory, size, 4); little(directory, size, 4);
        little(directory, name.size(), 2); directory.append(12, '\0'); little(directory, offset, 4); directory += name;
    }
    auto offset = out.size(); out += directory;
    little(out, 0x06054b50, 4); out.append(4, '\0');
    little(out, entries.size(), 2); little(out, entries.size(), 2);
    little(out, directory.size(), 4); little(out, offset, 4); little(out, 0, 2);
    return out;
}
struct Scratch {
    fs::path path = fs::temp_directory_path() / ("kestrel-assets-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Scratch() { std::error_code error; fs::remove_all(path, error); }
};
}
int main()
{
    using namespace kestrel::world;
    Scratch scratch;
    auto directory = scratch.path / "assets";
    check(!downloadedAssetsReady(directory), "missing cache is not ready");
    try {
        installDownloadedAssets(archive(false), directory);
        check(false, "missing blocks must fail");
    } catch (const std::runtime_error&) {}
    check(!fs::exists(directory), "invalid pack is rejected before writing");
    installDownloadedAssets(archive(true), directory);
    check(fs::is_regular_file(directory / "resource_packs/vanilla/blocks.json"), "resource pack installed");
    check(fs::is_regular_file(directory / "resource_packs/vanilla/textures/test.txt"), "nested resource extracted");
    check(!fs::exists(directory / "escape.txt"), "archive traversal is not extracted");
    check(!fs::exists(directory / "resource_packs/vanilla/behavior_pack"), "behavior pack is not extracted");
    check(!downloadedAssetsReady(directory), "cache without verified fonts is not ready");
    std::atomic<bool> cancel {true};
    try {
        downloadVanillaAssets(directory, cancel, [](AssetDownloadProgress) {});
        check(false, "cancelled download must fail");
    } catch (const std::runtime_error&) {}
    check(fs::is_regular_file(directory / "resource_packs/vanilla/blocks.json"), "cancel preserves existing cache");
    for (const auto& entry : fs::directory_iterator(scratch.path)) {
        check(entry.path() == directory, "cancel removes staging directory");
    }
}
