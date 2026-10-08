#pragma once
#include <filesystem>

namespace kestrel::platform {
std::filesystem::path iosResources();
bool iosResourcesReady();
void installIosResources(const std::filesystem::path& archive, const std::filesystem::path& bundle);
}
