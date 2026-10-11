#include "world/DownloadedAssets.h"
#include "world/ServerPack.h"
#include "FallbackFonts.h"
#include "Network/Http/HttpClient.h"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace kestrel::world {
namespace {
namespace fs = std::filesystem;
constexpr size_t MaxDownloadSize = 512ull * 1024 * 1024;
constexpr double ResourceSize = 169385576.0;

struct FontDownload { std::string name, hash, url; };
std::vector<FontDownload> fonts()
{
    std::istringstream lines(KestrelFallbackFonts);
    std::vector<FontDownload> result;
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream fields(line);
        FontDownload font;
        if (!(fields >> font.name >> font.hash >> font.url)) throw std::runtime_error("Invalid fallback font index");
        result.push_back(std::move(font));
    }
    return result;
}

std::string digest(const std::string& bytes)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> hash {};
    unsigned size = 0;
    if (!EVP_Digest(bytes.data(), bytes.size(), hash.data(), &size, EVP_sha256(), nullptr)) {
        throw std::runtime_error("Cannot verify downloaded fonts");
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned i = 0; i < size; ++i) {
        result += hex[hash[i] >> 4];
        result += hex[hash[i] & 15];
    }
    return result;
}

void save(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) throw std::runtime_error("Cannot save Minecraft resources: " + path.string());
}

std::string download(std::string url, size_t maximum, const std::atomic<bool>& cancel,
    const std::function<void(size_t)>& progress = {})
{
    for (int redirect = 0; redirect <= 5; ++redirect) {
        if (cancel) throw std::runtime_error("Minecraft resource download cancelled");
        if (!url.starts_with("https://")) throw std::runtime_error("Resource download requires HTTPS");
        HttpResponse response;
        std::string error;
        if (!HttpClient::get(url, {{"User-Agent", "Kestrel"}}, response, error, 30 * 60 * 1000, maximum, &cancel, progress)) {
            throw std::runtime_error("Cannot download Minecraft resources: " + error + ". Restart Kestrel to retry.");
        }
        if (response.mStatus == 301 || response.mStatus == 302 || response.mStatus == 303
            || response.mStatus == 307 || response.mStatus == 308) {
            url = response.getHeader("location");
            continue;
        }
        if (response.mStatus != 200) {
            throw std::runtime_error("Minecraft resource download failed (HTTP " + std::to_string(response.mStatus) + "). Restart Kestrel to retry.");
        }
        return std::move(response.mBody);
    }
    throw std::runtime_error("Minecraft resource download redirected too many times");
}

bool packReady(const fs::path& directory)
{
    std::ifstream marker(directory / ".ready");
    std::string installed;
    std::getline(marker, installed);
    std::error_code error;
    return installed == VanillaResourceUrl && fs::is_regular_file(directory / "resource_packs/vanilla/blocks.json", error);
}
}

bool downloadedAssetsReady(const fs::path& directory)
{
    if (!packReady(directory)) return false;
    for (const auto& font : fonts()) {
        auto path = directory / "fonts" / font.name;
        std::error_code error;
        auto size = fs::file_size(path, error);
        if (error || size > 16 * 1024 * 1024) return false;
        std::ifstream input(path, std::ios::binary);
        std::string bytes {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (!input || digest(bytes) != font.hash) return false;
    }
    return true;
}

void installDownloadedAssets(std::string archive, const fs::path& directory)
{
    std::string error;
    auto pack = loadServerPackData(std::move(archive), {}, error, "resource_pack/");
    if (!pack) throw std::runtime_error("Cannot open Mojang's resource pack: " + error);
    if (!pack->find("blocks.json")) throw std::runtime_error("Mojang's download has no block definitions");
    for (const auto& path : pack->paths()) {
        auto bytes = pack->find(path);
        if (!bytes) throw std::runtime_error("Cannot extract Minecraft resource: " + path);
        save(directory / "resource_packs/vanilla" / path, *bytes);
    }
    save(directory / ".ready", std::string(VanillaResourceUrl) + '\n');
}

void downloadVanillaAssets(const fs::path& directory, const std::atomic<bool>& cancel,
    const std::function<void(AssetDownloadProgress)>& progress)
{
    if (downloadedAssetsReady(directory)) return;
    // A failed download never publishes an incomplete pack as the active cache.
    fs::create_directories(directory.parent_path());
    fs::path staging;
    std::random_device random;
    do {
        staging = directory;
        staging += ".staging-" + std::to_string(random()) + "-" + std::to_string(random());
    } while (!fs::create_directory(staging));
    try {
        auto catalog = fonts();
        fs::path pixelFont;
        progress({AssetDownloadStage::Fonts, 0.0f, 0, catalog.size()});
        for (size_t i = 0; i < catalog.size(); ++i) {
            const auto& font = catalog[i];
            auto bytes = download(font.url, 16 * 1024 * 1024, cancel);
            if (digest(bytes) != font.hash) throw std::runtime_error("Downloaded font failed SHA-256 verification: " + font.name);
            save(staging / "fonts" / font.name, bytes);
            if (font.name == "Monocraft.ttf") pixelFont = staging / "fonts" / font.name;
            progress({AssetDownloadStage::Fonts, 0.05f * (i + 1) / catalog.size(), i + 1, catalog.size(), pixelFont});
        }
        progress({AssetDownloadStage::Resources, 0.05f, 0, static_cast<size_t>(ResourceSize), pixelFont});
        auto bytes = download(VanillaResourceUrl, MaxDownloadSize, cancel, [&](size_t received) {
            progress({AssetDownloadStage::Resources,
                0.05f + 0.85f * static_cast<float>(std::min(1.0, received / ResourceSize)), received,
                static_cast<size_t>(ResourceSize), pixelFont});
        });
        if (cancel) throw std::runtime_error("Minecraft resource download cancelled");
        progress({AssetDownloadStage::Installing, 0.9f, bytes.size(), bytes.size(), pixelFont});
        installDownloadedAssets(std::move(bytes), staging);
        if (cancel) throw std::runtime_error("Minecraft resource download cancelled");
        if (downloadedAssetsReady(directory)) {
            fs::remove_all(staging);
        } else {
            fs::remove_all(directory);
            fs::rename(staging, directory);
        }
        progress({AssetDownloadStage::Ready, 1.0f, 0, 0, directory / "fonts/Monocraft.ttf"});
    } catch (...) {
        std::error_code error;
        fs::remove_all(staging, error);
        throw;
    }
}
}
