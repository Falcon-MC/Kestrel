#pragma once

#include <filesystem>

namespace kestrel::platform {

std::filesystem::path dataDirectory();
std::filesystem::path startupFont();
void useDownloadedAssets(const std::filesystem::path& directory);

}
