#include "client/session/SessionData.h"

#include "world/BlockBreaking.h"
#include "world/BlockCollisions.h"

#include <algorithm>
#include <iterator>

namespace kestrel {

namespace {

constexpr size_t MaxBlockChanges = 256;
constexpr size_t MaxPlayerTicks = 64;
constexpr size_t MaxBreakProgress = 64;

/**
 * The block of a value as movement collides with it: its state in the
 * collision table, the server's own shape, nothing for air, and a full cube
 * otherwise. Without models, a block a model draws has no collision, the way
 * the outline reads it.
 */
const world::CollisionState* collisionState(const LoadedBlocks& area, uint32_t value, bool models)
{
    if (!area.assets || value == world::ImplicitAir) {
        return nullptr;
    }
    const world::BlockCollisions& table = world::BlockCollisions::shared();
    if (const world::CollisionState* state = table.find(area.assets->stateHash(value, area.ids.hashed, area.ids.sequential.get()))) {
        return state;
    }
    if (const world::CollisionState* custom = nullptr; area.assets->customCollision(value, area.ids.hashed, area.ids.sequential.get(), custom)) {
        return custom;
    }
    const world::BlockVisual& visual = area.assets->visual(value, area.ids.hashed, area.ids.sequential.get());
    if ((visual.flags & world::FlagAir) || (!models && visual.hasModel())) {
        return nullptr;
    }
    return table.fullBlock();
}

world::SubChunkKey keyOf(int32_t dimension, int32_t x, int32_t y, int32_t z)
{
    return { dimension, x >> 4, y >> 4, z >> 4 };
}

std::string qualified(const std::string& name)
{
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

}

/**
 * Whether the block's column is loaded and the height lies within the world.
 * A sub-chunk that is all air may never be sent, so a missing one inside a
 * loaded column reads as air rather than as unloaded.
 */
bool LoadedBlocks::loaded(int32_t x, int32_t y, int32_t z) const
{
    if (subChunks.count(keyOf(dimension, x, y, z)) != 0) {
        return true;
    }
    std::array<int32_t, 2> column { x >> 4, z >> 4 };
    if (!std::binary_search(columns.begin(), columns.end(), column)) {
        return false;
    }
    world::DimensionRange range;
    if (!world::vanillaDimensionRange(dimension, range)) {
        return false;
    }
    int32_t section = y >> 4;
    return section >= range.baseSubChunkY && section < range.baseSubChunkY + range.subChunkCount;
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

std::vector<world::CollisionBox> LoadedBlocks::collision(int32_t x, int32_t y, int32_t z) const
{
    std::vector<world::CollisionBox> boxes;
    const world::BlockCollisions& table = world::BlockCollisions::shared();
    const world::CollisionState* state = collisionState(*this, value(x, y, z), true);
    if (!state || table.named(state, "powder_snow")) {
        return boxes;
    }
    world::BlockCollisions::Lookup lookup = [this](int32_t nx, int32_t ny, int32_t nz) {
        return collisionState(*this, value(nx, ny, nz), true);
    };
    table.boxes(*state, x, y, z, lookup, boxes);
    return boxes;
}

std::optional<world::CollisionBox> LoadedBlocks::outline(int32_t x, int32_t y, int32_t z) const
{
    if (!selectable(x, y, z)) {
        return std::nullopt;
    }
    uint32_t found = value(x, y, z);
    std::vector<world::CollisionBox> boxes;
    if (const world::CollisionState* state = collisionState(*this, found, false)) {
        world::BlockCollisions::Lookup lookup = [this](int32_t nx, int32_t ny, int32_t nz) {
            return collisionState(*this, value(nx, ny, nz), true);
        };
        world::BlockCollisions::shared().boxes(*state, x, y, z, lookup, boxes);
        for (world::CollisionBox& box : boxes) {
            box = { box.minX - float(x), box.minY - float(y), box.minZ - float(z), box.maxX - float(x), box.maxY - float(y), box.maxZ - float(z) };
        }
    }
    world::CollisionBox box = session::selectionBounds(*assets, ids, found, boxes);
    return world::CollisionBox { box.minX + float(x), box.minY + float(y), box.minZ + float(z), box.maxX + float(x), box.maxY + float(y), box.maxZ + float(z) };
}

/**
 * Reads what a block is from its visual, its collision state and the
 * breaking and movement tables.
 */
LoadedBlocks::Properties LoadedBlocks::properties(int32_t x, int32_t y, int32_t z) const
{
    static constexpr std::string_view Hazards[] = {
        "lava", "flowing_lava", "fire", "soul_fire", "magma", "cactus", "sweet_berry_bush", "powder_snow", "wither_rose", "campfire", "soul_campfire",
    };
    static constexpr std::string_view Falling[] = {
        "sand", "red_sand", "gravel", "suspicious_sand", "suspicious_gravel", "anvil", "chipped_anvil", "damaged_anvil", "dragon_egg", "scaffolding", "pointed_dripstone",
    };
    Properties properties;
    uint32_t found = value(x, y, z);
    if (!assets || found == world::ImplicitAir) {
        return properties;
    }
    const world::BlockVisual& visual = assets->visual(found, ids.hashed, ids.sequential.get());
    if (visual.flags & world::FlagAir) {
        return properties;
    }
    std::string full = name(x, y, z);
    std::string_view bare = full;
    if (bare.starts_with("minecraft:")) {
        bare.remove_prefix(10);
    }
    const world::CollisionState* state = world::BlockCollisions::shared().find(assets->stateHash(found, ids.hashed, ids.sequential.get()));
    properties.air = false;
    properties.liquid = visual.liquid != 0 || (state && (state->flags & world::CollisionLiquid));
    properties.water = properties.liquid && bare.find("water") != std::string_view::npos;
    properties.lava = properties.liquid && bare.find("lava") != std::string_view::npos;
    properties.liquidLevel = properties.liquid ? int(visual.liquidLevel) : -1;
    std::vector<world::CollisionBox> boxes = collision(x, y, z);
    properties.solid = !boxes.empty();
    properties.fullCube = boxes.size() == 1 && boxes.front().minX == float(x) && boxes.front().minY == float(y) && boxes.front().minZ == float(z)
        && boxes.front().maxX == float(x + 1) && boxes.front().maxY == float(y + 1) && boxes.front().maxZ == float(z + 1);
    properties.climbable = state && (state->flags & world::CollisionClimbable);
    properties.hazard = std::find(std::begin(Hazards), std::end(Hazards), bare) != std::end(Hazards);
    properties.replaceable = session::replaceableBlock(full);
    properties.gravity = bare.ends_with("concrete_powder") || std::find(std::begin(Falling), std::end(Falling), bare) != std::end(Falling);
    properties.hardness = world::BlockHardnessTable::shared().find(bare).hardness;
    properties.friction = PlayerMotion::blockFriction(bare);
    return properties;
}

MotionCell LoadedBlocks::motionCell(int32_t x, int32_t y, int32_t z) const
{
    MotionCell cell;
    auto found = subChunks.find(keyOf(dimension, x, y, z));
    if (found == subChunks.end() || !found->second) {
        return cell;
    }
    const world::SubChunk& subChunk = *found->second;
    cell.primary = collisionState(*this, subChunk.runtimeId(0, uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15)), true);
    cell.extra = collisionState(*this, subChunk.runtimeId(1, uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15)), true);
    return cell;
}

/**
 * Shares every loaded sub-chunk of the player's dimension with the client.
 * The chunk store counts its changes, so a tick where nothing moved costs one
 * comparison and keeps the map already shared.
 */
void Session::publishLoaded()
{
    uint64_t storeRevision = world.store().revision();
    std::shared_ptr<const LoadedBlocks> previous;
    {
        std::lock_guard<std::mutex> guard(mutex);
        previous = current.loaded;
    }
    if (previous && storeRevision == loadedStoreRevision && previous->dimension == motionDimension && previous->assets == assets
        && previous->ids.hashed == ids.hashed && previous->ids.sequential == ids.sequential && previous->ids.hidden == ids.hidden) {
        return;
    }
    auto area = std::make_shared<LoadedBlocks>();
    area->dimension = motionDimension;
    area->assets = assets;
    area->ids = ids;
    for (auto& [key, subChunk] : world.store().allSubChunks()) {
        if (key.dimension == motionDimension) {
            area->subChunks.emplace(key, std::move(subChunk));
        }
    }
    for (const world::ChunkKey& key : world.store().columns()) {
        if (key.dimension == motionDimension) {
            area->columns.push_back({ key.x, key.z });
        }
    }
    std::sort(area->columns.begin(), area->columns.end());
    area->revision = ++loadedRevision;
    loadedStoreRevision = storeRevision;
    std::lock_guard<std::mutex> guard(mutex);
    current.loaded = std::move(area);
}

/**
 * Hands block changes to the main thread with full names, leaving out the
 * ones that change nothing, such as the server confirming a break the
 * client already predicted.
 */
void Session::recordBlockChanges(std::vector<BlockChangeView> changes)
{
    std::vector<BlockChangeView> kept;
    for (BlockChangeView& change : changes) {
        change.oldName = change.oldName.empty() ? "minecraft:air" : qualified(change.oldName);
        change.newName = change.newName.empty() ? "minecraft:air" : qualified(change.newName);
        if (change.oldName != change.newName) {
            kept.push_back(std::move(change));
        }
    }
    if (kept.empty()) {
        return;
    }
    std::lock_guard<std::mutex> guard(mutex);
    auto list = std::make_shared<std::vector<BlockChangeView>>(current.blockChanges ? *current.blockChanges : std::vector<BlockChangeView> {});
    for (BlockChangeView& change : kept) {
        list->push_back(std::move(change));
    }
    if (list->size() > MaxBlockChanges) {
        list->erase(list->begin(), list->end() - static_cast<std::ptrdiff_t>(MaxBlockChanges));
    }
    current.blockChangeSerial += kept.size();
    current.blockChanges = std::move(list);
}

void Session::recordPlayerTick(const PlayerTickView& tick)
{
    std::lock_guard<std::mutex> guard(mutex);
    auto list = std::make_shared<std::vector<PlayerTickView>>(current.playerTicks ? *current.playerTicks : std::vector<PlayerTickView> {});
    list->push_back(tick);
    if (list->size() > MaxPlayerTicks) {
        list->erase(list->begin());
    }
    ++current.playerTickSerial;
    current.playerTicks = std::move(list);
}

void Session::recordBreakProgress(const std::vector<BreakProgress>& progress)
{
    if (progress.empty()) {
        return;
    }
    std::lock_guard<std::mutex> guard(mutex);
    auto list = std::make_shared<std::vector<BreakProgress>>(current.breakProgress ? *current.breakProgress : std::vector<BreakProgress> {});
    list->insert(list->end(), progress.begin(), progress.end());
    if (list->size() > MaxBreakProgress) {
        list->erase(list->begin(), list->end() - static_cast<std::ptrdiff_t>(MaxBreakProgress));
    }
    current.breakProgressSerial += progress.size();
    current.breakProgress = std::move(list);
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
