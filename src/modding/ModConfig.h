#pragma once

#include "mod/Config.h"

#include <filesystem>
#include <map>

namespace kestrel::modding {

class ModConfig final : public mod::Config {
public:
    explicit ModConfig(std::filesystem::path file);

    std::optional<std::string> find(std::string_view key) const override;
    void put(std::string_view key, std::string value) override;
    void remove(std::string_view key) override;
    std::vector<std::string> keys() const override;
    void save() override;

    void saveIfChanged();

    /**
     * Reads the file again, dropping values not saved yet.
     */
    void load();

private:
    std::filesystem::path file;
    std::map<std::string, std::string, std::less<>> values;
    bool changed = false;
};

}
