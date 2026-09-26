#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kestrel {

struct LocalWorld {
    std::string name;
    std::filesystem::path folder;
    int64_t lastPlayed = 0;
    uint64_t sizeBytes = 0;
};

std::vector<LocalWorld> scanLocalWorlds();

}
