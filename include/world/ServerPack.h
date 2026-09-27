#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace kestrel::world {

/**
 * The files of one resource pack sent by a server, unzipped and decrypted,
 * keyed by their path relative to the pack root.
 */
struct PackFiles {
    std::unordered_map<std::string, std::string> files;

    const std::string* find(const std::string& path) const
    {
        auto found = files.find(path);
        return found == files.end() ? nullptr : &found->second;
    }
};

std::shared_ptr<const PackFiles> loadServerPack(const std::filesystem::path& archive, const std::string& contentKey, std::string& error);

}
