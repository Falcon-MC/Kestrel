#pragma once

#include "menu/Menu.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kestrel {

/**
 * The worlds saved on this device, one folder each in the game's own layout:
 * level.dat, levelname.txt, world_icon.jpeg and the db folder. A world
 * exported from the game imports as is, and an exported one opens in the
 * game.
 */
class WorldStore {
public:
    explicit WorldStore(std::filesystem::path directory);

    const std::filesystem::path& directory() const
    {
        return root;
    }

    /**
     * Every world, the most recently played first. Icons are left empty for
     * the caller to fill with the sprites it made.
     */
    std::vector<menu::LocalWorld> list() const;
    std::optional<menu::LocalWorld> find(const std::string& id) const;

    std::optional<std::string> create(const menu::WorldSettings& settings);
    bool update(const std::string& id, const menu::WorldSettings& settings);
    bool remove(const std::string& id);
    std::optional<std::string> duplicate(const std::string& id);
    std::optional<std::string> importWorld(const std::filesystem::path& file, std::string& error);
    bool exportWorld(const std::string& id, const std::filesystem::path& file, std::string& error) const;
    bool saveIcon(const std::string& id, const std::string& jpeg);
    std::optional<std::string> readIcon(const std::string& id) const;

private:
    std::filesystem::path root;

    std::filesystem::path folder(const std::string& id) const;
    std::string freeId() const;
};

}
