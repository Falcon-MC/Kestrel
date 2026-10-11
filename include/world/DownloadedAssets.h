#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>

namespace kestrel::world {

inline constexpr const char* VanillaResourceVersion = "v1.26.50.4";
inline constexpr const char* VanillaResourceUrl = "https://github.com/Mojang/bedrock-samples/releases/download/v1.26.50.4/bedrock-samples-v1.26.50.4-full.zip";

bool downloadedAssetsReady(const std::filesystem::path& directory);
void installDownloadedAssets(std::string archive, const std::filesystem::path& directory);
enum class AssetDownloadStage { Fonts, Resources, Installing, Ready };
struct AssetDownloadProgress {
    AssetDownloadStage stage = AssetDownloadStage::Fonts;
    float fraction = 0.0f;
    size_t received = 0;
    size_t total = 0;
    std::filesystem::path font;
};
void downloadVanillaAssets(const std::filesystem::path& directory, const std::atomic<bool>& cancel,
    const std::function<void(AssetDownloadProgress)>& progress);

}
