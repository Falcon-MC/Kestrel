#include "client/LocalWorlds.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>

namespace kestrel {

namespace {

namespace fs = std::filesystem;

fs::path environment(const char* name)
{
    const char* value = std::getenv(name);
    return value ? fs::path(value) : fs::path();
}

std::vector<fs::path> worldRoots()
{
    std::vector<fs::path> roots;
    std::error_code error;
#if defined(_WIN32)
    fs::path users = environment("APPDATA") / "Minecraft Bedrock" / "Users";
    if (fs::is_directory(users, error)) {
        for (const fs::directory_entry& user : fs::directory_iterator(users, error)) {
            roots.push_back(user.path() / "games" / "com.mojang" / "minecraftWorlds");
        }
    }
    roots.push_back(environment("LOCALAPPDATA") / "Packages" / "Microsoft.MinecraftUWP_8wekyb3d8bbwe" / "LocalState" / "games" / "com.mojang" / "minecraftWorlds");
#elif defined(__APPLE__)
    roots.push_back(environment("HOME") / "Library" / "Application Support" / "mcpelauncher" / "games" / "com.mojang" / "minecraftWorlds");
#else
    fs::path data = environment("XDG_DATA_HOME");
    if (data.empty()) {
        data = environment("HOME") / ".local" / "share";
    }
    roots.push_back(data / "mcpelauncher" / "games" / "com.mojang" / "minecraftWorlds");
#endif
    return roots;
}

std::string readName(const fs::path& folder)
{
    std::ifstream file(folder / "levelname.txt");
    std::string name;
    std::getline(file, name);
    while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' ')) {
        name.pop_back();
    }
    return name.empty() ? folder.filename().string() : name;
}

int64_t modified(const fs::path& file)
{
    std::error_code error;
    fs::file_time_type time = fs::last_write_time(file, error);
    if (error) {
        return 0;
    }
    auto system = std::chrono::file_clock::to_sys(time);
    return std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
}

uint64_t folderSize(const fs::path& folder)
{
    uint64_t total = 0;
    std::error_code error;
    for (fs::recursive_directory_iterator it(folder, error), end; it != end; it.increment(error)) {
        if (error) {
            break;
        }
        if (it->is_regular_file(error)) {
            total += it->file_size(error);
        }
    }
    return total;
}

}

std::vector<LocalWorld> scanLocalWorlds()
{
    std::vector<LocalWorld> worlds;
    std::error_code error;
    for (const fs::path& root : worldRoots()) {
        if (!fs::is_directory(root, error)) {
            continue;
        }
        for (const fs::directory_entry& entry : fs::directory_iterator(root, error)) {
            if (!entry.is_directory(error) || !fs::exists(entry.path() / "level.dat", error)) {
                continue;
            }
            LocalWorld world;
            world.folder = entry.path();
            world.name = readName(entry.path());
            world.lastPlayed = modified(entry.path() / "level.dat");
            world.sizeBytes = folderSize(entry.path());
            worlds.push_back(std::move(world));
        }
    }
    std::sort(worlds.begin(), worlds.end(), [](const LocalWorld& a, const LocalWorld& b) {
        return a.lastPlayed > b.lastPlayed;
    });
    return worlds;
}

}
