#include "client/session/SessionData.h"

#include <algorithm>

namespace kestrel {

namespace {

world::SubChunkKey keyOf(int32_t dimension, int32_t x, int32_t y, int32_t z)
{
    return { dimension, x >> 4, y >> 4, z >> 4 };
}

std::string qualified(const std::string& name)
{
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

}

bool LoadedBlocks::loaded(int32_t x, int32_t y, int32_t z) const
{
    return subChunks.count(keyOf(dimension, x, y, z)) != 0;
}

uint32_t LoadedBlocks::value(int32_t x, int32_t y, int32_t z) const
{
    auto found = subChunks.find(keyOf(dimension, x, y, z));
    if (found == subChunks.end() || !found->second || found->second->storages().empty()) {
        return world::ImplicitAir;
    }
    return found->second->runtimeId(0, uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15));
}

std::string LoadedBlocks::name(int32_t x, int32_t y, int32_t z) const
{
    uint32_t found = value(x, y, z);
    if (!assets || found == world::ImplicitAir) {
        return "minecraft:air";
    }
    return qualified(assets->blockName(found, ids.hashed, ids.sequential.get()));
}

std::vector<std::string> LoadedBlocks::states(int32_t x, int32_t y, int32_t z) const
{
    std::vector<std::string> list;
    uint32_t found = value(x, y, z);
    if (!assets || found == world::ImplicitAir) {
        return list;
    }
    const Tag* tag = assets->blockStates(found, ids.hashed, ids.sequential.get());
    if (!tag || !tag->isCompound()) {
        return list;
    }
    const std::vector<std::string>& keys = tag->getKeys();
    const std::vector<Tag>& values = tag->getValues();
    for (size_t index = 0; index < keys.size() && index < values.size(); ++index) {
        const Tag& state = values[index];
        switch (state.getType()) {
        case Tag::Type::Byte:
            list.push_back(keys[index] + ": " + (state.asByte() ? "true" : "false"));
            break;
        case Tag::Type::Short:
            list.push_back(keys[index] + ": " + std::to_string(state.asShort()));
            break;
        case Tag::Type::Int:
            list.push_back(keys[index] + ": " + std::to_string(state.asInt()));
            break;
        case Tag::Type::String:
            list.push_back(keys[index] + ": " + state.asString());
            break;
        default:
            break;
        }
    }
    return list;
}

bool LoadedBlocks::selectable(int32_t x, int32_t y, int32_t z) const
{
    uint32_t found = value(x, y, z);
    if (!assets || found == world::ImplicitAir || (ids.hidden && ids.hidden->count(found))) {
        return false;
    }
    const world::BlockVisual& visual = assets->visual(found, ids.hashed, ids.sequential.get());
    return !(visual.flags & world::FlagAir) && !visual.liquid;
}

/**
 * Shares every loaded sub-chunk of the player's dimension with the client.
 */
void Session::publishLoaded()
{
    auto chunks = world.store().allSubChunks();
    std::shared_ptr<const LoadedBlocks> previous;
    { std::lock_guard<std::mutex> guard(mutex); previous = current.loaded; }
    if (previous && previous->dimension == motionDimension && previous->assets == assets
        && previous->ids.hashed == ids.hashed && previous->ids.sequential == ids.sequential && previous->ids.hidden == ids.hidden) {
        size_t count = 0;
        bool unchanged = true;
        for (const auto& [key, subChunk] : chunks) {
            if (key.dimension != motionDimension) continue;
            ++count;
            auto found = previous->subChunks.find(key);
            if (found == previous->subChunks.end() || found->second != subChunk) { unchanged = false; break; }
        }
        if (unchanged && count == previous->subChunks.size()) return;
    }
    auto area = std::make_shared<LoadedBlocks>();
    area->dimension = motionDimension;
    area->assets = assets;
    area->ids = ids;
    for (auto& [key, subChunk] : chunks) {
        if (key.dimension == motionDimension) {
            area->subChunks.emplace(key, std::move(subChunk));
        }
    }
    std::lock_guard<std::mutex> guard(mutex);
    current.loaded = std::move(area);
}

void Session::setHiddenBlocks(std::set<std::string> names, bool visibleOnly)
{
    std::set<std::string> normalized;
    for (const std::string& name : names) {
        normalized.insert(qualified(name));
    }
    std::lock_guard<std::mutex> guard(mutex);
    pendingHidden = std::make_pair(std::move(normalized), visibleOnly && !names.empty());
}

/**
 * Adds the values of a sub-chunk's palettes whose block a mod hid to the
 * hidden set the mesher reads, checking each value once.
 */
void Session::hideNewValues(const world::SubChunk* subChunk)
{
    if (!subChunk || (hiddenNames.empty() && !hiddenInverted) || !assets) {
        return;
    }
    std::shared_ptr<std::unordered_set<uint32_t>> grown;
    for (const world::PalettedStorage& storage : subChunk->storages()) {
        for (uint32_t value : storage.palette()) {
            if (value == world::ImplicitAir || hiddenChecked.count(value)) {
                continue;
            }
            std::string name = qualified(assets->blockName(value, ids.hashed, ids.sequential.get()));
            bool listed = hiddenNames.count(name) != 0;
            bool hidden = hiddenInverted ? !listed && name != "minecraft:air" : listed;
            hiddenChecked[value] = hidden;
            if (!hidden) {
                continue;
            }
            if (!grown) {
                grown = std::make_shared<std::unordered_set<uint32_t>>(ids.hidden ? *ids.hidden : std::unordered_set<uint32_t> {});
            }
            grown->insert(value);
        }
    }
    if (grown) {
        ids.hidden = std::move(grown);
    }
}

/**
 * Takes a new hidden block list from the mods, rebuilds the hidden values
 * from every loaded sub-chunk and asks for every mesh again.
 */
void Session::applyHiddenBlocks()
{
    std::optional<std::pair<std::set<std::string>, bool>> rule;
    {
        std::lock_guard<std::mutex> guard(mutex);
        rule = std::move(pendingHidden);
        pendingHidden.reset();
    }
    if (!rule || (rule->first == hiddenNames && rule->second == hiddenInverted)) {
        return;
    }
    hiddenNames = std::move(rule->first);
    hiddenInverted = rule->second;
    hiddenChecked.clear();
    ids.hidden.reset();
    for (const auto& [key, subChunk] : world.store().allSubChunks()) {
        hideNewValues(subChunk.get());
    }
    world.store().markAllDirty();
}

}
