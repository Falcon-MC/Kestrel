#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace kestrel::world {
class PackFiles {
public:
    struct Storage;
    explicit PackFiles(std::shared_ptr<Storage> storage);
    std::shared_ptr<const std::string> find(const std::string& path) const;
    std::vector<std::string> paths() const;
    std::shared_ptr<const PackFiles> withSubPack(const std::string& name) const;
    size_t archiveBytes() const;
    uint64_t expandedBytes() const;
private:
    std::shared_ptr<Storage> storage;
    std::string subPack;
};
std::shared_ptr<const PackFiles> loadServerPack(const std::filesystem::path& archive, const std::string& contentKey, std::string& error);
std::shared_ptr<const PackFiles> loadServerPackData(std::string archive, const std::string& contentKey, std::string& error);
}
