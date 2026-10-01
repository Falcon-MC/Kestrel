#pragma once

#include "world/ServerPack.h"

#include <array>
#include <deque>
#include <filesystem>
#include <future>
#include <map>
#include <string>
#include <vector>

namespace kestrel::world {

struct GlobalPackEntry {
    std::string id, name, description, version, file, error, selectedSubPack;
    std::vector<std::pair<std::string, std::string>> subPacks;
    std::shared_ptr<const std::string> icon;
    uint64_t bytes = 0;
    bool active = false;
};

struct GlobalPackAction {
    enum class Kind { Import, Refresh, Activate, Deactivate, Up, Down, Reload, Remove, SubPack, OpenFolder };
    Kind kind = Kind::Refresh;
    std::string id, value;
};

class GlobalResources {
public:
    explicit GlobalResources(std::filesystem::path directory);
    ~GlobalResources();
    void request(GlobalPackAction action);
    bool poll();
    bool busy() const { return job.valid() || !requests.empty(); }
    const std::vector<GlobalPackEntry>& entries() const { return shown; }
    const std::vector<std::shared_ptr<const PackFiles>>& packs() const { return activePacks; }
    const std::string& message() const { return status; }
    const std::filesystem::path& directory() const { return root; }
    uint64_t revision() const { return serial; }

private:
    struct Record {
        GlobalPackEntry entry;
        std::array<int, 3> version {};
        std::vector<std::pair<std::string, std::array<int, 3>>> dependencies;
        std::shared_ptr<const PackFiles> pack;
    };
    struct State {
        std::vector<Record> records;
        std::vector<std::string> active;
        std::map<std::string, std::string> subPacks;
        std::string message;
    };
    static Record read(const std::filesystem::path& file);
    static State process(std::filesystem::path root, State state, GlobalPackAction action);
    void publish();
    std::filesystem::path root;
    State state;
    std::future<State> job;
    std::deque<GlobalPackAction> requests;
    std::vector<GlobalPackEntry> shown;
    std::vector<std::shared_ptr<const PackFiles>> activePacks;
    std::vector<std::pair<std::shared_ptr<const PackFiles>, std::string>> activeSources;
    std::string status;
    uint64_t serial = 0;
};

}
