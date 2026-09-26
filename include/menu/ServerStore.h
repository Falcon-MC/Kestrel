#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
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

private:
    std::filesystem::path file;
    std::vector<SavedServer> entries;
};

std::string normalizeAddress(std::string address);

}
