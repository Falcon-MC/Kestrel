#pragma once

#include "Core/NBT/Tag.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kestrel::world {

struct BlockRecord {
    std::string name;
    Tag states;
    uint32_t networkHash = 0;
};

class BlockRegistry {
public:
    bool load(std::string& error);

    const std::vector<BlockRecord>& records() const
    {
        return entries;
    }

    int32_t resolve(uint32_t networkValue, bool hashed) const;

    bool isDataDriven(const std::string& name) const
    {
        return dataDriven.contains(name);
    }

    static uint64_t nameHash(const std::string& name);

private:
    std::vector<BlockRecord> entries;
    std::unordered_map<uint32_t, uint32_t> byHash;
    std::unordered_set<std::string> dataDriven;
};

}
