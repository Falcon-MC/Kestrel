#pragma once

#include "world/ServerPack.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace kestrel::world {

class PackSource {
public:
    static std::filesystem::path locateVanilla();

    explicit PackSource(std::filesystem::path root);

    const std::filesystem::path& root() const
    {
        return base;
    }

    const std::vector<std::filesystem::path>& layers() const
    {
        return stack;
    }

    void setOverlays(std::vector<std::shared_ptr<const PackFiles>> packs)
    {
        overlays = std::move(packs);
    }

    const std::vector<std::shared_ptr<const PackFiles>>& overlayPacks() const
    {
        return overlays;
    }

    bool readText(const std::string& relative, std::string& out) const;
    std::vector<std::string> readTextLayers(const std::string& relative) const;
    bool readTexture(const std::string& texturePath, std::string& out);
    std::vector<std::string> archiveEntries(const std::string& archive);
    bool readArchived(const std::string& archive, const std::string& name, std::string& out);

    /**
     * The archived file from every layer that has it, highest priority first.
     */
    std::vector<std::string> readArchivedLayers(const std::string& archive, const std::string& name);

private:
    struct Archive {
        std::string data;
        std::unordered_map<std::string, std::pair<size_t, size_t>> entries;
        size_t dataStart = 0;
    };

    const Archive* archive(const std::filesystem::path& file);

    std::filesystem::path base;
    std::vector<std::filesystem::path> stack;
    std::vector<std::shared_ptr<const PackFiles>> overlays;
    std::map<std::filesystem::path, std::unique_ptr<Archive>> archives;
};

}
