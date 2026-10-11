#pragma once

#include <filesystem>
#include <string>

namespace kestrel::platform {

std::filesystem::path executablePath();
std::string updatePlatform();
bool launchUpdate(const std::filesystem::path& replacement, std::string& error);

}
