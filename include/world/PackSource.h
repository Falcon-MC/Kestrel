#pragma once

#include "world/ServerPack.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
     * Like readArchived, but only from the game's own layers, never a server
     * pack that happens to ship a file of the same name.
     */
    bool readBaseArchived(const std::string& archive, const std::string& name, std::string& out);

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

    struct LooseFiles {
        std::unordered_set<std::string> files;
        std::unordered_set<std::string> folders;
    };

    const Archive* archive(size_t layer, const std::string& name);
    const LooseFiles& looseFiles(size_t layer);
    const std::vector<size_t>& layersWith(const std::string& folder);

    std::filesystem::path base;
    std::vector<std::filesystem::path> stack;
    std::vector<std::shared_ptr<const PackFiles>> overlays;
    std::unordered_map<std::string, std::unique_ptr<Archive>> archives;
    std::vector<std::optional<LooseFiles>> looseIndex;
    std::unordered_map<std::string, std::vector<size_t>> folderLayers;
};

}
