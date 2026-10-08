#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kestrel::menu {

struct SavedServer {
    std::string name;
    std::string address;
    bool favorite = false;
    int64_t lastJoined = 0;
};

class ServerStore {
public:
    explicit ServerStore(std::filesystem::path file);

    void load();
    void save() const;

    const std::vector<SavedServer>& servers() const
    {
        return entries;
    }

    size_t add(std::string name, std::string address);
    void update(size_t index, std::string name, std::string address);
    void remove(size_t index);
    void toggleFavorite(size_t index);
    void markJoined(size_t index);

    /**
     * Whether a saved server other than except already points at address,
     * both compared once normalized, since the game refuses a second server
     * with the same address and port.
     */
    bool hasAddress(const std::string& address, std::optional<size_t> except) const;

private:
    std::filesystem::path file;
    std::vector<SavedServer> entries;
};

std::string normalizeAddress(std::string address);

/**
 * The port the way the game's server form corrects it when the field loses
 * focus: empty or not a number becomes 19132, above 65535 becomes 65535.
 */
std::string clampPort(const std::string& port);

/**
 * A saved address split into the host and the port the server form edits
 * separately, the port being defaultPort when the address names none or is
 * an IPv6 address.
 */
std::pair<std::string, std::string> splitAddress(const std::string& address, std::string_view defaultPort);

}
