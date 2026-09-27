#include "world/BlockAssets.h"
#include "BlockLightJson.h"

#include "Core/Json/Json.h"
#include "Protocol/BlockStateHasher.h"
#include "ui/Image.h"
#include "world/BlockEntityModels.h"
#include "world/BlockModels.h"
#include "world/Geometry.h"
#include "world/Molang.h"
#include "world/PackSource.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_set>

namespace kestrel::world {

namespace {

enum class Face {
    West,
    East,
    Down,
    Up,
    North,
    South,
};

enum class Axis {
    X,
    Y,
    Z,
};

enum class Family {
    Air,
    Liquid,
    Invisible,
    Cube,
    Leaves,
    TransparentCube,
    Model,
    Deferred,
};

bool startsWith(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}

bool endsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool contains(const std::string& value, const std::string& part)
{
    return value.find(part) != std::string::npos;
}

std::string stripComments(const std::string& source)
{
    std::string out;
    out.reserve(source.size());
    bool inString = false;
    for (size_t i = 0; i < source.size(); ++i) {
        char c = source[i];
        if (inString) {
            out.push_back(c);
            if (c == '\\' && i + 1 < source.size()) {
                out.push_back(source[++i]);
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            out.push_back(c);
        } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') {
                ++i;
            }
            out.push_back('\n');
        } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            i += 2;
            while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/')) {
                ++i;
            }
            ++i;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

bool isTorchName(const std::string& name)
{
    return name == "torch" || name == "copper_torch" || name == "soul_torch" || name == "redstone_torch" || name == "unlit_redstone_torch" || name == "underwater_torch" || startsWith(name, "colored_torch_");
}

bool isAquaticName(const std::string& name)
{
    return name == "seagrass" || name == "tall_seagrass" || name == "kelp" || name == "kelp_plant" || (contains(name, "coral") && !contains(name, "coral_block"));
}

bool isCropName(const std::string& name)
{
    static const char* const crops[] = { "wheat", "carrots", "potatoes", "beetroot", "nether_wart", "sweet_berry_bush", "torchflower_crop", "pitcher_crop", "melon_stem", "pumpkin_stem" };
    return std::find(std::begin(crops), std::end(crops), name) != std::end(crops);
}

bool isCrossName(const std::string& name)
{
    static const char* const crosses[] = {
        "short_grass", "tall_grass", "short_dry_grass", "tall_dry_grass", "fern", "large_fern", "deadbush", "bush",
        "red_flower", "yellow_flower", "dandelion", "poppy", "blue_orchid", "allium", "azure_bluet", "oxeye_daisy",
        "cornflower", "lily_of_the_valley", "wither_rose", "sunflower", "lilac", "rose_bush", "peony", "brown_mushroom",
        "red_mushroom", "crimson_fungus", "warped_fungus", "crimson_roots", "warped_roots", "nether_sprouts",
        "mangrove_propagule", "hanging_roots", "pale_hanging_moss", "firefly_bush", "reeds", "weeping_vines",
        "twisting_vines", "web", "fire", "soul_fire", "torchflower",
    };
    if (name == "chorus_flower") {
        return false;
    }
    if (std::find(std::begin(crosses), std::end(crosses), name) != std::end(crosses) || startsWith(name, "cave_vines")) {
        return true;
    }
    return !contains(name, "flower_pot") && (endsWith(name, "_flower") || endsWith(name, "_sapling"));
}

bool isShelfName(const std::string& name)
{
    return endsWith(name, "_shelf");
}

/**
 * Blocks whose look comes from their block entity: chests, beds, banners,
 * brewing stands, campfires, copper golem statues, decorated pots, enchanting
 * tables, item frames, hoppers, lecterns and skulls. They are drawn as the
 * diagnostic cube until their block entity renderer exists.
 */
bool isDeferredName(const std::string& name)
{
    return name == "chest" || name == "trapped_chest" || name == "ender_chest" || name == "bed" || endsWith(name, "_bed")
        || name == "standing_banner" || name == "wall_banner" || name == "brewing_stand" || name == "campfire"
        || name == "soul_campfire" || contains(name, "copper_golem_statue") || name == "decorated_pot"
        || name == "enchanting_table" || name == "frame" || name == "glow_frame" || name == "hopper" || name == "lectern"
        || name == "skull" || endsWith(name, "_skull") || endsWith(name, "_head");
}

Family classify(const std::string& name)
{
    if (name == "air") {
        return Family::Air;
    }
    if (name == "water" || name == "flowing_water" || name == "lava" || name == "flowing_lava") {
        return Family::Liquid;
    }
    if (isDeferredName(name)) {
        return Family::Deferred;
    }
    if (name == "barrier" || name == "structure_void" || startsWith(name, "light_block")) {
        return Family::Invisible;
    }
    if (name == "bone_block" || name == "hay_block" || name == "chiseled_quartz_block" || name == "purpur_block" || name == "quartz_block" || name == "smooth_quartz" || name == "tnt" || endsWith(name, "_glazed_terracotta")) {
        return Family::Cube;
    }
    if (contains(name, "copper_golem_statue") || name == "dragon_egg" || name == "soul_sand" || name == "mud"
        || contains(name, "trapdoor") || endsWith(name, "_door") || name == "wooden_door" || endsWith(name, "_stairs")
        || contains(name, "slab") || contains(name, "fence_gate") || endsWith(name, "_wall") || name == "cobblestone_wall"
        || endsWith(name, "_fence") || name == "fence" || name == "nether_brick_fence" || contains(name, "glass_pane")
        || endsWith(name, "_pane") || endsWith(name, "_bars") || endsWith(name, "_bed") || name == "bed"
        || contains(name, "chest") || contains(name, "sign") || contains(name, "rail") || isTorchName(name)
        || name == "lever" || endsWith(name, "_button") || name == "stone_button" || contains(name, "pressure_plate")
        || endsWith(name, "_carpet") || name == "carpet" || name == "snow_layer" || name == "leaf_litter"
        || isAquaticName(name) || name == "cocoa" || isCropName(name) || name == "wildflowers" || name == "pink_petals"
        || name == "vine" || name == "glow_lichen" || name == "sculk_vein" || name == "resin_clump" || name == "cactus"
        || name == "cake" || name == "farmland" || isShelfName(name) || isCrossName(name) || contains(name, "shulker_box")
        || name == "ladder" || name == "waterlily" || name == "lily_pad" || name == "bamboo"
        || name == "amethyst_cluster" || endsWith(name, "_amethyst_bud")) {
        return Family::Model;
    }
    if (endsWith(name, "leaves") || endsWith(name, "leaves_flowered")) {
        return Family::Leaves;
    }
    if (contains(name, "stained_glass") || name == "glass" || name == "tinted_glass" || name == "ice" || name == "frosted_ice"
        || name == "slime" || name == "honey_block" || endsWith(name, "copper_grate") || name == "mob_spawner"
        || name == "trial_spawner" || name == "vault" || name == "beacon") {
        return Family::TransparentCube;
    }
    return Family::Cube;
}

enum class ModelKind {
    None,
    Slab,
    Stair,
    Fence,
    Pane,
    Wall,
    Door,
    Trapdoor,
    Gate,
    Carpet,
    SnowLayer,
    PressurePlate,
    Button,
    Cross,
    Torch,
    Cactus,
    Farmland,
    Cake,
    Rail,
    Ladder,
    Vine,
    Multiface,
    Lily,
    Chest,
    Sign,
    SinkingCube,
    Bamboo,
    Cluster,
};

ModelKind modelKind(const std::string& name)
{
    if (endsWith(name, "standing_sign") || endsWith(name, "wall_sign") || endsWith(name, "hanging_sign")) {
        return ModelKind::Sign;
    }
    if (name == "soul_sand" || name == "mud") {
        return ModelKind::SinkingCube;
    }
    if (name == "bamboo") {
        return ModelKind::Bamboo;
    }
    if (name == "amethyst_cluster" || endsWith(name, "_amethyst_bud")) {
        return ModelKind::Cluster;
    }
    if (contains(name, "trapdoor")) {
        return ModelKind::Trapdoor;
    }
    if (endsWith(name, "_door") || name == "wooden_door") {
        return ModelKind::Door;
    }
    if (contains(name, "fence_gate")) {
        return ModelKind::Gate;
    }
    if (contains(name, "slab")) {
        return ModelKind::Slab;
    }
    if (endsWith(name, "_stairs")) {
        return ModelKind::Stair;
    }
    if (endsWith(name, "_wall") || name == "cobblestone_wall") {
        return ModelKind::Wall;
    }
    if (endsWith(name, "_fence") || name == "fence") {
        return ModelKind::Fence;
    }
    if (contains(name, "glass_pane") || endsWith(name, "_pane") || endsWith(name, "_bars")) {
        return ModelKind::Pane;
    }
    if (endsWith(name, "_carpet") || name == "carpet") {
        return ModelKind::Carpet;
    }
    if (name == "snow_layer") {
        return ModelKind::SnowLayer;
    }
    if (contains(name, "pressure_plate")) {
        return ModelKind::PressurePlate;
    }
    if (endsWith(name, "_button") || name == "stone_button") {
        return ModelKind::Button;
    }
    if (isTorchName(name)) {
        return ModelKind::Torch;
    }
    if (name == "cactus") {
        return ModelKind::Cactus;
    }
    if (name == "farmland") {
        return ModelKind::Farmland;
    }
    if (name == "cake") {
        return ModelKind::Cake;
    }
    if (contains(name, "rail")) {
        return ModelKind::Rail;
    }
    if (name == "ladder") {
        return ModelKind::Ladder;
    }
    if (name == "vine") {
        return ModelKind::Vine;
    }
    if (name == "glow_lichen" || name == "sculk_vein" || name == "resin_clump") {
        return ModelKind::Multiface;
    }
    if (name == "waterlily" || name == "lily_pad") {
        return ModelKind::Lily;
    }
    if (name == "chest" || name == "trapped_chest" || name == "ender_chest") {
        return ModelKind::Chest;
    }
    if (isCrossName(name) || isCropName(name) || isAquaticName(name)) {
        return ModelKind::Cross;
    }
    return ModelKind::None;
}

const char* legacyAlias(const std::string& name)
{
    static const std::map<std::string, const char*> aliases = {
        { "grass_block", "grass" },
        { "sea_lantern", "seaLantern" },
        { "dandelion", "yellow_flower" },
        { "hard_glass_pane", "glass_pane" },
        { "oak_door", "wooden_door" },
        { "oak_trapdoor", "trapdoor" },
        { "oak_fence_gate", "fence_gate" },
        { "oak_button", "wooden_button" },
        { "oak_pressure_plate", "wooden_pressure_plate" },
        { "iron_chain", "chain" },
        { "lily_pad", "waterlily" },
    };
    auto found = aliases.find(name);
    return found == aliases.end() ? nullptr : found->second;
}

std::string stateString(const Tag& states, const std::string& key)
{
    const Tag* value = states.get(key);
    if (!value || value->getType() != Tag::Type::String) {
        return {};
    }
    return value->asString();
}

Axis stateAxis(const Tag& states)
{
    std::string axis = stateString(states, "pillar_axis");
    if (axis.empty()) {
        axis = stateString(states, "axis");
    }
    if (axis == "x") {
        return Axis::X;
    }
    if (axis == "z") {
        return Axis::Z;
    }
    return Axis::Y;
}

std::pair<Face, bool> orientFace(Face face, Axis axis)
{
    switch (axis) {
    case Axis::X:
        switch (face) {
        case Face::West:
            return { Face::Down, false };
        case Face::East:
            return { Face::Up, false };
        case Face::Down:
            return { Face::East, true };
        case Face::Up:
            return { Face::West, true };
        default:
            return { face, true };
        }
    case Axis::Z:
        switch (face) {
        case Face::North:
            return { Face::Down, false };
        case Face::South:
            return { Face::Up, false };
        case Face::Down:
            return { Face::South, true };
        case Face::Up:
            return { Face::North, true };
        default:
            return { face, true };
        }
    case Axis::Y:
        break;
    }
    return { face, false };
}

const char* faceKey(Face face)
{
    switch (face) {
    case Face::West:
        return "west";
    case Face::East:
        return "east";
    case Face::Down:
        return "down";
    case Face::Up:
        return "up";
    case Face::North:
        return "north";
    case Face::South:
        return "south";
    }
    return "side";
}

bool isHorizontal(Face face)
{
    return face != Face::Up && face != Face::Down;
}

std::optional<Face> faceFromName(const std::string& value)
{
    static const std::pair<const char*, Face> names[] = {
        { "down", Face::Down },
        { "up", Face::Up },
        { "north", Face::North },
        { "south", Face::South },
        { "west", Face::West },
        { "east", Face::East },
    };
    for (const auto& [name, face] : names) {
        if (value == name) {
            return face;
        }
    }
    return std::nullopt;
}

std::optional<int32_t> stateInt(const Tag& states, const std::string& key)
{
    const Tag* value = states.get(key);
    if (!value) {
        return std::nullopt;
    }
    switch (value->getType()) {
    case Tag::Type::Byte:
        return static_cast<int32_t>(value->asByte());
    case Tag::Type::Short:
        return static_cast<int32_t>(value->asShort());
    case Tag::Type::Int:
        return value->asInt();
    default:
        return std::nullopt;
    }
}

/**
 * Quarter turns of a model facing south by default: south, west, north, east.
 */
uint8_t facingRotation(const std::string& cardinal)
{
    static constexpr const char* Order[4] = { "south", "west", "north", "east" };
    for (uint8_t rotation = 0; rotation < 4; ++rotation) {
        if (cardinal == Order[rotation]) {
            return rotation;
        }
    }
    return 0;
}

uint8_t facingDirectionRotation(int32_t facing)
{
    switch (facing) {
    case 2:
        return 2;
    case 4:
        return 1;
    case 5:
        return 3;
    default:
        return 0;
    }
}

/**
 * Blocks drawn from their block entity: the kind, and in variant the state
 * part of their look (rotation, bed half, skull type).
 */
bool classifyBlockEntity(const std::string& name, const Tag& states, BlockVisual& visual)
{
    static constexpr const char* Skulls[SkullKinds] = { "skeleton_skull", "wither_skeleton_skull", "zombie_head", "creeper_head", "player_head" };
    if (name == "chest" || name == "trapped_chest" || name == "ender_chest") {
        visual.blockEntity = name == "chest" ? EntityChest : name == "trapped_chest" ? EntityTrappedChest : EntityEnderChest;
        std::string cardinal = stateString(states, "minecraft:cardinal_direction");
        visual.variant = cardinal.empty() ? facingDirectionRotation(stateInt(states, "facing_direction").value_or(3)) : facingRotation(cardinal);
        return true;
    }
    if (name == "bed" || endsWith(name, "_bed")) {
        visual.blockEntity = EntityBed;
        visual.variant = uint32_t(stateInt(states, "direction").value_or(0) & 3) | (stateInt(states, "head_piece_bit").value_or(0) ? 4u : 0u);
        return true;
    }
    if (name == "standing_banner") {
        visual.blockEntity = EntityStandingBanner;
        visual.variant = uint32_t(stateInt(states, "ground_sign_direction").value_or(0) & 15);
        return true;
    }
    if (name == "wall_banner") {
        visual.blockEntity = EntityWallBanner;
        visual.variant = facingDirectionRotation(stateInt(states, "facing_direction").value_or(3));
        return true;
    }
    for (uint32_t kind = 0; kind < SkullKinds; ++kind) {
        if (name == Skulls[kind]) {
            int32_t facing = stateInt(states, "facing_direction").value_or(1);
            visual.blockEntity = facing <= 1 ? EntityFloorSkull : EntityWallSkull;
            visual.variant = kind | (uint32_t(facingDirectionRotation(facing)) << 4);
            return true;
        }
    }
    return false;
}

bool stateMatches(const Tag& states, const json::Value& expected)
{
    for (const std::string& key : expected.mKeys) {
        const json::Value& value = *expected.mObject.at(key);
        const Tag* actual = states.get(key);
        if (!actual) {
            return false;
        }
        if (value.isString()) {
            if (actual->getType() != Tag::Type::String || actual->asString() != value.mString) {
                return false;
            }
        } else if (value.mType == json::Value::Type::Boolean) {
            if (stateInt(states, key).value_or(-1) != (value.mBoolean ? 1 : 0)) {
                return false;
            }
        } else if (stateInt(states, key).value_or(INT32_MIN) != value.integer()) {
            return false;
        }
    }
    return true;
}

/**
 * Light emission and filter of every vanilla state from the measured block
 * light table: a per-block default, then the first override whose states all
 * match. Blocks the table does not list filter fully when they occlude.
 */
void applyBlockLight(const BlockRegistry& registry, std::vector<BlockVisual>& visuals)
{
    std::string text(reinterpret_cast<const char*>(KestrelBlockLightData::kBlockLightJson), KestrelBlockLightData::kBlockLightJsonSize);
    std::unique_ptr<json::Value> root = json::parse(text);
    std::unordered_map<std::string, const json::Value*> byName;
    if (const json::Value* blocks = root ? root->get("blocks") : nullptr; blocks && blocks->isArray()) {
        for (const std::unique_ptr<json::Value>& block : blocks->mArray) {
            if (const json::Value* name = block->get("name"); name && name->isString()) {
                byName.emplace(name->mString, block.get());
            }
        }
    }
    auto level = [](const json::Value& entry, const char* key) {
        const json::Value* value = entry.get(key);
        return static_cast<uint8_t>(std::clamp(value ? value->integer() : 0, 0, 15));
    };
    for (size_t i = 0; i < registry.records().size() && i < visuals.size(); ++i) {
        const BlockRecord& record = registry.records()[i];
        BlockVisual& visual = visuals[i];
        auto found = byName.find(record.name);
        if (found == byName.end()) {
            visual.lightFilter = (visual.flags & FlagOccludesFullFace) ? 15 : 0;
            continue;
        }
        const json::Value* chosen = found->second;
        if (const json::Value* overrides = chosen->get("overrides"); overrides && overrides->isArray()) {
            for (const std::unique_ptr<json::Value>& entry : overrides->mArray) {
                const json::Value* states = entry->get("states");
                if (states && states->isObject() && stateMatches(record.states, *states)) {
                    chosen = entry.get();
                    break;
                }
            }
        }
        visual.lightEmission = level(*chosen, "emission");
        visual.lightFilter = level(*chosen, "filter");
    }
}

std::optional<Face> stateFacing(const Tag& states)
{
    for (const char* key : { "minecraft:cardinal_direction", "minecraft:facing_direction" }) {
        if (std::optional<Face> face = faceFromName(stateString(states, key))) {
            return face;
        }
    }
    if (std::optional<int32_t> facing = stateInt(states, "facing_direction")) {
        static constexpr Face byIndex[] = { Face::Down, Face::Up, Face::North, Face::South, Face::West, Face::East };
        if (*facing >= 0 && *facing < 6) {
            return byIndex[*facing];
        }
    }
    if (std::optional<int32_t> direction = stateInt(states, "direction")) {
        static constexpr Face byIndex[] = { Face::South, Face::West, Face::North, Face::East };
        if (*direction >= 0 && *direction < 4) {
            return byIndex[*direction];
        }
    }
    return std::nullopt;
}

Face opposite(Face face)
{
    switch (face) {
    case Face::West:
        return Face::East;
    case Face::East:
        return Face::West;
    case Face::Down:
        return Face::Up;
    case Face::Up:
        return Face::Down;
    case Face::North:
        return Face::South;
    case Face::South:
        return Face::North;
    }
    return face;
}

int horizontalIndex(Face face)
{
    switch (face) {
    case Face::North:
        return 0;
    case Face::East:
        return 1;
    case Face::South:
        return 2;
    case Face::West:
        return 3;
    default:
        return -1;
    }
}

Face horizontalFace(int index)
{
    static constexpr Face order[] = { Face::North, Face::East, Face::South, Face::West };
    return order[((index % 4) + 4) % 4];
}

std::string explicitFaceKey(const json::Value& textures, Face face)
{
    if (const json::Value* key = textures.get(faceKey(face))) {
        return key->string();
    }
    if (isHorizontal(face)) {
        if (const json::Value* side = textures.get("side")) {
            return side->string();
        }
    }
    return {};
}

std::optional<Face> frontFace(const json::Value& textures)
{
    for (Face face : { Face::South, Face::North, Face::East, Face::West }) {
        const json::Value* key = textures.get(faceKey(face));
        if (!key || !key->isString()) {
            continue;
        }
        const std::string& value = key->string();
        if (contains(value, "front") || endsWith(value, "_face") || endsWith(value, "_top")) {
            return face;
        }
    }
    return std::nullopt;
}

Face sourceForFacing(Face world, Face reference, Face facing)
{
    if (isHorizontal(facing)) {
        if (!isHorizontal(world)) {
            return world;
        }
        int steps = horizontalIndex(facing) - horizontalIndex(reference);
        return horizontalFace(horizontalIndex(world) - steps);
    }
    Face back = opposite(reference);
    if (facing == Face::Up) {
        if (world == Face::Up) {
            return reference;
        }
        if (world == Face::Down) {
            return back;
        }
        if (world == reference) {
            return Face::Down;
        }
        if (world == back) {
            return Face::Up;
        }
        return world;
    }
    if (world == Face::Down) {
        return reference;
    }
    if (world == Face::Up) {
        return back;
    }
    if (world == back) {
        return Face::Down;
    }
    if (world == reference) {
        return Face::Up;
    }
    return world;
}

std::string resolveTextureKey(const json::Value* textures, Face face, Axis axis, std::optional<Face> facing, bool& rotate)
{
    rotate = false;
    if (!textures) {
        return {};
    }
    if (textures->isString()) {
        return textures->string();
    }
    if (!textures->isObject()) {
        return {};
    }
    std::optional<Face> reference = facing ? frontFace(*textures) : std::nullopt;
    if (reference) {
        std::string frontKey = explicitFaceKey(*textures, *reference);
        if (!isHorizontal(*facing) && contains(frontKey, "front_horizontal")) {
            if (face == *facing) {
                for (Face candidate : { Face::East, Face::West, Face::North, Face::South }) {
                    std::string key = explicitFaceKey(*textures, candidate);
                    if (contains(key, "front_vertical")) {
                        return key;
                    }
                }
                return frontKey;
            }
            return explicitFaceKey(*textures, Face::Up);
        }
        return explicitFaceKey(*textures, sourceForFacing(face, *reference, *facing));
    }
    auto [source, rotateUv] = orientFace(face, axis);
    rotate = rotateUv;
    return explicitFaceKey(*textures, source);
}

size_t terrainVariantCount(const json::Value& entry)
{
    const json::Value* textures = entry.get("textures");
    if (!textures) {
        return 0;
    }
    return textures->isArray() ? textures->mArray.size() : 1;
}

std::string terrainPath(const json::Value& entry, size_t index)
{
    const json::Value* textures = entry.get("textures");
    if (!textures) {
        return {};
    }
    const json::Value* first = textures;
    if (textures->isArray()) {
        if (textures->mArray.empty()) {
            return {};
        }
        first = textures->mArray[std::min(index, textures->mArray.size() - 1)].get();
    }
    if (first->isString()) {
        return first->string();
    }
    if (const json::Value* path = first->get("path")) {
        return path->string();
    }
    return {};
}

struct Flipbook {
    std::string texturePath;
    std::string atlasTile;
    int32_t atlasIndex = -1;
    uint32_t ticksPerFrame = 1;
    std::vector<uint32_t> frames;
    bool blendFrames = true;
};

std::optional<Flipbook> parseFlipbook(const json::Value& item)
{
    const json::Value* texture = item.get("flipbook_texture");
    const json::Value* tile = item.get("atlas_tile");
    if (!texture || !tile || !texture->isString() || !tile->isString()) {
        return std::nullopt;
    }
    Flipbook flipbook;
    flipbook.texturePath = texture->string();
    flipbook.atlasTile = tile->string();
    if (const json::Value* index = item.get("atlas_index")) {
        flipbook.atlasIndex = std::max(0, index->integer(0));
    }
    if (const json::Value* ticks = item.get("ticks_per_frame")) {
        flipbook.ticksPerFrame = static_cast<uint32_t>(std::max(1, ticks->integer(1)));
    }
    if (const json::Value* blend = item.get("blend_frames")) {
        flipbook.blendFrames = blend->boolean(true);
    }
    if (const json::Value* frames = item.get("frames"); frames && frames->isArray()) {
        for (const auto& frame : frames->mArray) {
            flipbook.frames.push_back(static_cast<uint32_t>(std::max(0, frame->integer(0))));
        }
    }
    return flipbook;
}

std::vector<uint8_t> normalizeTexture(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height);

std::vector<std::vector<uint8_t>> sliceFrames(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height)
{
    uint32_t frameSize = std::min(width, height);
    uint32_t count = std::max(width, height) / frameSize;
    bool horizontal = width > height;
    std::vector<std::vector<uint8_t>> frames;
    for (uint32_t frame = 0; frame < count; ++frame) {
        std::vector<uint8_t> pixels(size_t(frameSize) * frameSize * 4);
        for (uint32_t row = 0; row < frameSize; ++row) {
            uint32_t x = horizontal ? frame * frameSize : 0;
            uint32_t y = horizontal ? row : frame * frameSize + row;
            const uint8_t* source = rgba.data() + (size_t(y) * width + x) * 4;
            std::copy(source, source + size_t(frameSize) * 4, pixels.data() + size_t(row) * frameSize * 4);
        }
        frames.push_back(normalizeTexture(pixels, frameSize, frameSize));
    }
    return frames;
}

void applyTint(std::vector<uint8_t>& pixels, uint32_t rgb)
{
    uint32_t tint[3] = { (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF };
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            pixels[i + c] = static_cast<uint8_t>(pixels[i + c] * tint[c] / 255);
        }
        pixels[i + 3] = static_cast<uint8_t>(std::min<uint32_t>(pixels[i + 3], 180));
    }
}

float tagNumber(const Tag* value, float fallback = 0.0f)
{
    if (!value) {
        return fallback;
    }
    switch (value->getType()) {
    case Tag::Type::Byte:
        return value->asByte();
    case Tag::Type::Short:
        return value->asShort();
    case Tag::Type::Int:
        return static_cast<float>(value->asInt());
    case Tag::Type::Long:
        return static_cast<float>(value->asLong());
    case Tag::Type::Float:
        return value->asFloat();
    case Tag::Type::Double:
        return static_cast<float>(value->asDouble());
    default:
        return fallback;
    }
}

void collectComponents(const Tag* components, std::map<std::string, const Tag*>& out)
{
    if (!components || components->getType() != Tag::Type::Compound) {
        return;
    }
    const std::vector<std::string>& keys = components->getKeys();
    const std::vector<Tag>& values = components->getValues();
    for (size_t i = 0; i < keys.size(); ++i) {
        out[keys[i]] = &values[i];
    }
}

/**
 * Every state of a server-declared block in client palette order: the explicit
 * properties first, then the properties added by its traits, with the last
 * property varying fastest.
 */
std::vector<Tag> enumerateCustomStates(const Tag& definition)
{
    std::vector<std::pair<std::string, std::vector<Tag>>> properties;
    if (const Tag* list = definition.get("properties"); list && list->getType() == Tag::Type::List) {
        for (const Tag& property : list->getList()) {
            const Tag* name = property.get("name");
            const Tag* values = property.get("enum");
            if (name && values && name->getType() == Tag::Type::String && values->getType() == Tag::Type::List && !values->getList().empty()) {
                properties.emplace_back(name->asString(), values->getList());
            }
        }
    }
    if (const Tag* traits = definition.get("traits"); traits && traits->getType() == Tag::Type::List) {
        auto strings = [](std::initializer_list<const char*> values) {
            std::vector<Tag> tags;
            for (const char* value : values) {
                tags.push_back(Tag::ofString(value));
            }
            return tags;
        };
        for (const Tag& trait : traits->getList()) {
            const Tag* name = trait.get("name");
            const Tag* enabled = trait.get("enabled_states");
            if (!name || name->getType() != Tag::Type::String) {
                continue;
            }
            auto isEnabled = [&](const char* state) {
                const Tag* flag = enabled ? enabled->get(state) : nullptr;
                return flag && tagNumber(flag) != 0.0f;
            };
            std::string traitName = name->asString();
            if (traitName == "minecraft:connection" && isEnabled("cardinal_connections")) {
                for (const char* direction : { "north", "south", "west", "east" }) {
                    properties.emplace_back(std::string("minecraft:connection_") + direction, std::vector<Tag> { Tag::ofByte(0), Tag::ofByte(1) });
                }
            } else if (traitName == "minecraft:multi_block" && isEnabled("multi_block_part")) {
                int32_t parts = static_cast<int32_t>(tagNumber(trait.get("parts")));
                if (parts >= 2 && parts <= 4) {
                    std::vector<Tag> values;
                    for (int32_t part = 0; part < parts; ++part) {
                        values.push_back(Tag::ofInt(part));
                    }
                    properties.emplace_back("minecraft:multi_block_part", std::move(values));
                }
            } else if (traitName == "minecraft:placement_direction") {
                if (isEnabled("cardinal_direction") || isEnabled("corner_and_cardinal_direction")) {
                    properties.emplace_back("minecraft:cardinal_direction", strings({ "south", "north", "west", "east" }));
                }
                if (isEnabled("facing_direction")) {
                    properties.emplace_back("minecraft:facing_direction", strings({ "down", "up", "south", "north", "west", "east" }));
                }
                if (isEnabled("corner_and_cardinal_direction")) {
                    properties.emplace_back("minecraft:corner", strings({ "none", "inner_left", "inner_right", "outer_left", "outer_right" }));
                }
            } else if (traitName == "minecraft:placement_position") {
                if (isEnabled("block_face")) {
                    properties.emplace_back("minecraft:block_face", strings({ "down", "up", "south", "north", "west", "east" }));
                }
                if (isEnabled("vertical_half")) {
                    properties.emplace_back("minecraft:vertical_half", strings({ "bottom", "top" }));
                }
            }
        }
    }

    std::vector<Tag> states { Tag::ofCompound() };
    for (const auto& [name, values] : properties) {
        std::vector<Tag> next;
        next.reserve(states.size() * values.size());
        for (const Tag& state : states) {
            for (const Tag& value : values) {
                Tag expanded = state;
                expanded.put(name, value);
                next.push_back(std::move(expanded));
            }
        }
        states = std::move(next);
    }
    return states;
}

uint8_t blockTint(const std::string& name, int face)
{
    constexpr uint8_t Grass = uint8_t(TintKind::Grass);
    constexpr uint8_t Foliage = uint8_t(TintKind::Foliage);
    if (name == "grass_block") {
        static constexpr uint8_t Faces[6] = { Grass | TintOverlay, Grass | TintOverlay, 0, Grass, Grass | TintOverlay, Grass | TintOverlay };
        return Faces[face];
    }
    if (name == "water" || name == "flowing_water") {
        return uint8_t(TintKind::Water);
    }
    if (name == "oak_leaves" || name == "dark_oak_leaves" || name == "jungle_leaves" || name == "acacia_leaves" || name == "mangrove_leaves" || name == "vine") {
        return Foliage;
    }
    if (name == "birch_leaves") {
        return Foliage | (uint8_t(FoliageVariant::Birch) << TintVariantShift);
    }
    if (name == "spruce_leaves") {
        return Foliage | (uint8_t(FoliageVariant::Evergreen) << TintVariantShift);
    }
    if (name == "short_grass" || name == "tall_grass" || name == "fern" || name == "large_fern") {
        return Grass;
    }
    return 0;
}

bool isTranslucentName(const std::string& name)
{
    return contains(name, "stained_glass") || name == "water" || name == "flowing_water" || name == "ice" || name == "slime"
        || name == "honey_block" || name == "portal" || name == "tinted_glass";
}

std::vector<uint8_t> normalizeTexture(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height)
{
    uint32_t side = std::min(width, height);
    std::vector<uint8_t> out(TextureSize * TextureSize * 4);
    for (uint32_t y = 0; y < TextureSize; ++y) {
        uint32_t y0 = y * side / TextureSize;
        uint32_t y1 = std::max(y0 + 1, (y + 1) * side / TextureSize);
        for (uint32_t x = 0; x < TextureSize; ++x) {
            uint32_t x0 = x * side / TextureSize;
            uint32_t x1 = std::max(x0 + 1, (x + 1) * side / TextureSize);
            uint32_t sum[4] = {};
            uint32_t count = 0;
            for (uint32_t sy = y0; sy < y1; ++sy) {
                for (uint32_t sx = x0; sx < x1; ++sx) {
                    const uint8_t* texel = rgba.data() + (size_t(sy) * width + sx) * 4;
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += texel[c];
                    }
                    ++count;
                }
            }
            for (int c = 0; c < 4; ++c) {
                out[(size_t(y) * TextureSize + x) * 4 + c] = static_cast<uint8_t>(sum[c] / count);
            }
        }
    }
    return out;
}

std::vector<uint8_t> diagnosticTexture()
{
    std::vector<uint8_t> out(TextureSize * TextureSize * 4);
    for (uint32_t y = 0; y < TextureSize; ++y) {
        for (uint32_t x = 0; x < TextureSize; ++x) {
            bool magenta = ((x / 8) + (y / 8)) % 2 == 0;
            uint8_t* texel = out.data() + (size_t(y) * TextureSize + x) * 4;
            texel[0] = magenta ? 248 : 0;
            texel[1] = 0;
            texel[2] = magenta ? 248 : 0;
            texel[3] = 255;
        }
    }
    return out;
}

void buildMips(TextureArray& array, const std::vector<std::vector<uint8_t>>& layers, const std::vector<bool>& overlayLayers)
{
    array.layers = static_cast<uint32_t>(layers.size());
    uint32_t size = TextureSize;
    for (uint32_t level = 0; level < TextureMipLevels; ++level) {
        std::vector<uint8_t>& target = array.mips[level];
        target.assign(size_t(size) * size * 4 * layers.size(), 0);
        for (size_t layer = 0; layer < layers.size(); ++layer) {
            uint8_t* destination = target.data() + layer * size * size * 4;
            if (level == 0) {
                std::copy(layers[layer].begin(), layers[layer].end(), destination);
                continue;
            }
            uint32_t parent = size * 2;
            const uint8_t* source = array.mips[level - 1].data() + layer * parent * parent * 4;
            bool overlay = layer < overlayLayers.size() && overlayLayers[layer];
            for (uint32_t y = 0; y < size; ++y) {
                for (uint32_t x = 0; x < size; ++x) {
                    uint32_t alphaSum = 0;
                    uint32_t colour[3] = {};
                    for (uint32_t dy = 0; dy < 2; ++dy) {
                        for (uint32_t dx = 0; dx < 2; ++dx) {
                            const uint8_t* texel = source + ((size_t(y) * 2 + dy) * parent + x * 2 + dx) * 4;
                            alphaSum += texel[3];
                            for (int c = 0; c < 3; ++c) {
                                colour[c] += uint32_t(texel[c]) * (overlay ? 255u : texel[3]);
                            }
                        }
                    }
                    uint8_t* out = destination + (size_t(y) * size + x) * 4;
                    uint32_t weight = overlay ? 4u * 255u : alphaSum;
                    for (int c = 0; c < 3; ++c) {
                        out[c] = weight ? static_cast<uint8_t>(colour[c] / weight) : 0;
                    }
                    out[3] = static_cast<uint8_t>(alphaSum / 4);
                }
            }
        }
        size /= 2;
    }
}

}

std::shared_ptr<const BlockAssets> BlockAssets::shared(std::string& error)
{
    static std::mutex mutex;
    static std::shared_ptr<const BlockAssets> instance;
    static std::string failure;
    std::lock_guard<std::mutex> guard(mutex);
    if (!instance && failure.empty()) {
        auto assets = std::shared_ptr<BlockAssets>(new BlockAssets());
        if (assets->build({}, failure)) {
            instance = assets;
        }
    }
    error = failure;
    return instance;
}

std::shared_ptr<const BlockAssets> BlockAssets::create(const std::vector<std::shared_ptr<const PackFiles>>& packs, const std::vector<CustomBlock>& customBlocks, std::string& error)
{
    if (packs.empty() && customBlocks.empty()) {
        return shared(error);
    }
    auto assets = std::shared_ptr<BlockAssets>(new BlockAssets());
    assets->customs = customBlocks;
    if (!assets->build(packs, error)) {
        return nullptr;
    }
    return assets;
}

const std::string& BlockAssets::nameAt(size_t index) const
{
    if (index < registry.records().size()) {
        return registry.records()[index].name;
    }
    return customs[customStates[index - registry.records().size()].block].name;
}

std::shared_ptr<const SequentialMap> BlockAssets::sequentialMap() const
{
    std::unordered_set<std::string> declared;
    for (const CustomBlock& custom : customs) {
        declared.insert(custom.name);
    }

    std::vector<std::pair<uint64_t, int32_t>> entries;
    entries.reserve(registry.records().size() + customStates.size());
    for (size_t i = 0; i < registry.records().size(); ++i) {
        const std::string& name = registry.records()[i].name;
        if (registry.isDataDriven(name) && !declared.contains(name)) {
            continue;
        }
        entries.emplace_back(BlockRegistry::nameHash(name), static_cast<int32_t>(i));
    }
    for (size_t i = 0; i < customStates.size(); ++i) {
        const std::string& name = customs[customStates[i].block].name;
        entries.emplace_back(BlockRegistry::nameHash(name), static_cast<int32_t>(registry.records().size() + i));
    }

    std::stable_sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    auto map = std::make_shared<SequentialMap>();
    map->reserve(entries.size());
    for (const auto& entry : entries) {
        map->push_back(entry.second);
    }
    return map;
}

std::string BlockAssets::describe(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const
{
    if (networkValue == 0xFFFFFFFFu) {
        return "implicit air";
    }
    int32_t index = -1;
    if (!hashed && sequential) {
        if (networkValue < sequential->size()) {
            index = (*sequential)[networkValue];
        }
    } else {
        index = registry.resolve(networkValue, hashed);
        if (index < 0) {
            auto custom = customByHash.find(networkValue);
            index = custom == customByHash.end() ? -1 : static_cast<int32_t>(custom->second);
        }
    }
    if (index < 0) {
        return "unknown #" + std::to_string(networkValue);
    }
    return nameAt(static_cast<size_t>(index)) + " #" + std::to_string(networkValue);
}

const BlockVisual& BlockAssets::visual(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const
{
    static const BlockVisual air { FlagAir, {} };
    static const BlockVisual diagnostic { FlagDiagnostic, {} };
    if (networkValue == 0xFFFFFFFFu) {
        return air;
    }
    if (!hashed && sequential) {
        if (networkValue >= sequential->size() || (*sequential)[networkValue] < 0) {
            unresolved.fetch_add(1, std::memory_order_relaxed);
            lastUnresolved.store(networkValue, std::memory_order_relaxed);
            return diagnostic;
        }
        return visuals[static_cast<size_t>((*sequential)[networkValue])];
    }
    int32_t index = registry.resolve(networkValue, hashed);
    if (index < 0 && hashed) {
        auto custom = customByHash.find(networkValue);
        index = custom == customByHash.end() ? -1 : static_cast<int32_t>(custom->second);
    }
    if (index < 0) {
        unresolved.fetch_add(1, std::memory_order_relaxed);
        lastUnresolved.store(networkValue, std::memory_order_relaxed);
        return diagnostic;
    }
    return visuals[static_cast<size_t>(index)];
}

bool BlockAssets::build(const std::vector<std::shared_ptr<const PackFiles>>& packs, std::string& error)
{
    if (!registry.load(error)) {
        return false;
    }

    std::filesystem::path root = PackSource::locateVanilla();
    if (root.empty()) {
        error = "no vanilla resource pack found (install Minecraft Bedrock or set KESTREL_VANILLA_PACK)";
        return false;
    }
    PackSource pack(root);
    pack.setOverlays(packs);

    std::vector<std::unique_ptr<json::Value>> documents;
    std::vector<const json::Value*> blockLayers;
    std::vector<const json::Value*> terrainLayers;
    for (const std::string& text : pack.readTextLayers("blocks.json")) {
        std::unique_ptr<json::Value> parsed = json::parse(stripComments(text));
        if (parsed && parsed->isObject()) {
            blockLayers.push_back(parsed.get());
            documents.push_back(std::move(parsed));
        }
    }
    for (const std::string& text : pack.readTextLayers("textures/terrain_texture.json")) {
        std::unique_ptr<json::Value> parsed = json::parse(stripComments(text));
        const json::Value* data = parsed ? parsed->get("texture_data") : nullptr;
        if (data && data->isObject()) {
            terrainLayers.push_back(data);
            documents.push_back(std::move(parsed));
        }
    }
    if (blockLayers.empty() || terrainLayers.empty()) {
        error = "could not read blocks.json or terrain_texture.json from " + root.string();
        return false;
    }
    auto blockEntry = [&](const std::string& key) -> const json::Value* {
        for (const json::Value* layer : blockLayers) {
            if (const json::Value* found = layer->get(key)) {
                return found;
            }
        }
        return nullptr;
    };
    auto terrainKnown = [&](const std::string& key) {
        for (const json::Value* layer : terrainLayers) {
            if (layer->get(key)) {
                return true;
            }
        }
        return false;
    };

    std::vector<std::vector<uint8_t>> layers { diagnosticTexture() };
    std::map<std::string, uint32_t> materialByKey;
    materialTable.push_back({});

    auto terrainEntry = [&](const std::string& textureKey) -> const json::Value* {
        for (const json::Value* data : terrainLayers) {
            if (const json::Value* entry = data->get(textureKey)) {
                return entry;
            }
        }
        return nullptr;
    };

    std::vector<Flipbook> flipbooks;
    for (const std::string& text : pack.readTextLayers("textures/flipbook_textures.json")) {
        std::unique_ptr<json::Value> parsed = json::parse(stripComments(text));
        if (!parsed || !parsed->isArray()) {
            continue;
        }
        for (const auto& item : parsed->mArray) {
            std::optional<Flipbook> flipbook = parseFlipbook(*item);
            if (!flipbook) {
                continue;
            }
            bool shadowed = std::any_of(flipbooks.begin(), flipbooks.end(), [&](const Flipbook& existing) {
                return existing.atlasTile == flipbook->atlasTile && existing.atlasIndex == flipbook->atlasIndex;
            });
            if (!shadowed) {
                flipbooks.push_back(std::move(*flipbook));
            }
        }
    }

    std::map<std::string, std::vector<std::vector<uint8_t>>> decodedFrames;
    auto framesOf = [&](const std::string& path) -> const std::vector<std::vector<uint8_t>>& {
        auto found = decodedFrames.find(path);
        if (found != decodedFrames.end()) {
            return found->second;
        }
        std::vector<std::vector<uint8_t>> frames;
        std::string encoded;
        std::vector<uint8_t> rgba;
        uint32_t width = 0;
        uint32_t height = 0;
        if (pack.readTexture(path, encoded) && ui::decodeImage(encoded, width, height, rgba) && width > 0 && height > 0) {
            frames = sliceFrames(rgba, width, height);
        }
        return decodedFrames.emplace(path, std::move(frames)).first->second;
    };

    std::vector<bool> overlayLayers;
    auto materialFor = [&](const std::string& textureKey, bool rotate, size_t variant = 0, uint32_t tint = 0, uint8_t tintFlags = 0) -> uint32_t {
        const Flipbook* flipbook = nullptr;
        for (const Flipbook& candidate : flipbooks) {
            if (candidate.atlasTile == textureKey && (candidate.atlasIndex < 0 || size_t(candidate.atlasIndex) == variant)) {
                flipbook = &candidate;
                break;
            }
        }
        std::string path;
        if (flipbook) {
            path = flipbook->texturePath;
        } else if (const json::Value* entry = terrainEntry(textureKey)) {
            path = terrainPath(*entry, variant);
        }
        if (path.empty()) {
            return DiagnosticMaterial;
        }

        std::string key = path + '|' + std::to_string(rotate) + '|' + std::to_string(tint) + '|' + std::to_string(flipbook != nullptr) + '|' + std::to_string(tintFlags);
        auto materialFound = materialByKey.find(key);
        if (materialFound != materialByKey.end()) {
            return materialFound->second;
        }

        const std::vector<std::vector<uint8_t>>& frames = framesOf(path);
        uint32_t id = DiagnosticMaterial;
        if (!frames.empty()) {
            Material material;
            material.layer = static_cast<uint32_t>(layers.size());
            material.rotateUv = rotate;
            std::vector<size_t> timeline;
            if (flipbook && !flipbook->frames.empty()) {
                for (uint32_t frame : flipbook->frames) {
                    timeline.push_back(std::min<size_t>(frame, frames.size() - 1));
                }
            } else if (flipbook) {
                for (size_t frame = 0; frame < frames.size(); ++frame) {
                    timeline.push_back(frame);
                }
            } else {
                timeline.push_back(0);
            }
            timeline.resize(std::min<size_t>(timeline.size(), 128));
            for (size_t frame : timeline) {
                std::vector<uint8_t> pixels = frames[frame];
                if (tint) {
                    applyTint(pixels, tint);
                }
                layers.push_back(std::move(pixels));
                overlayLayers.resize(layers.size(), false);
                overlayLayers.back() = (tintFlags & TintOverlay) != 0;
            }
            material.frameCount = static_cast<uint32_t>(timeline.size());
            material.ticksPerFrame = flipbook ? std::clamp<uint32_t>(flipbook->ticksPerFrame, 1, 2048) : 1;
            material.interpolate = flipbook && flipbook->blendFrames;
            material.tint = tintFlags;
            id = static_cast<uint32_t>(materialTable.size());
            materialTable.push_back(material);
        }
        materialByKey.emplace(key, id);
        return id;
    };

    std::map<std::string, uint32_t> templateByKey;
    auto pushTemplate = [&](const std::vector<ModelQuad>& modelQuads, uint32_t flags) {
        templates.push_back({ static_cast<uint32_t>(quads.size()), static_cast<uint32_t>(modelQuads.size()), flags });
        quads.insert(quads.end(), modelQuads.begin(), modelQuads.end());
        return static_cast<uint32_t>(templates.size() - 1);
    };
    auto intern = [&](const std::string& key, const std::function<void()>& build) {
        auto found = templateByKey.find(key);
        if (found != templateByKey.end()) {
            return found->second;
        }
        uint32_t base = static_cast<uint32_t>(templates.size());
        build();
        templateByKey.emplace(key, base);
        return base;
    };
    auto keyOf = [](const std::string& family, const models::Materials& materials, std::initializer_list<uint32_t> parameters) {
        std::string key = family;
        for (uint32_t value : materials) {
            key += ':' + std::to_string(value);
        }
        for (uint32_t value : parameters) {
            key += '/' + std::to_string(value);
        }
        return key;
    };

    auto variantFor = [&](const std::string& textureKey, const std::string& name, const Tag& states) -> size_t {
        const json::Value* entry = terrainEntry(textureKey);
        size_t count = entry ? terrainVariantCount(*entry) : 0;
        if (count <= 1) {
            return 0;
        }
        if (textureKey == "door_lower" || textureKey == "door_upper") {
            static const std::pair<const char*, size_t> doors[] = {
                { "wooden_door", 0 }, { "spruce_door", 1 }, { "birch_door", 2 }, { "jungle_door", 3 },
                { "acacia_door", 4 }, { "dark_oak_door", 5 }, { "iron_door", 6 },
            };
            for (const auto& [door, index] : doors) {
                if (name == door) {
                    return index;
                }
            }
            return 0;
        }
        std::optional<int32_t> growth = stateInt(states, "growth");
        if (!growth) {
            growth = stateInt(states, "growth_stage");
        }
        if (!growth) {
            growth = stateInt(states, "age");
        }
        if (textureKey == "carrots" || textureKey == "potatoes" || textureKey == "beetroot") {
            static constexpr size_t Stages[8] = { 0, 0, 1, 1, 2, 2, 2, 3 };
            return growth && *growth >= 0 && *growth < 8 ? Stages[*growth] : 0;
        }
        if (textureKey == "melon_stem" || textureKey == "pumpkin_stem") {
            std::optional<int32_t> facingDirection = stateInt(states, "facing_direction");
            return facingDirection && *facingDirection >= 2 ? 1 : 0;
        }
        if (textureKey == "torchflower_crop") {
            return growth && *growth >= 4 ? 1 : 0;
        }
        if (!growth || *growth < 0) {
            return 0;
        }
        size_t value = static_cast<size_t>(*growth);
        return std::min(count >= 8 ? value : value * count / 8, count - 1);
    };

    visuals.resize(registry.records().size());
    for (size_t i = 0; i < registry.records().size(); ++i) {
        const BlockRecord& record = registry.records()[i];
        std::string name = startsWith(record.name, "minecraft:") ? record.name.substr(10) : record.name;
        BlockVisual& visual = visuals[i];
        if (name == "air") {
            airSequential = static_cast<uint32_t>(i);
            airHash = record.networkHash;
        }

        Family family = classify(name);
        switch (family) {
        case Family::Air:
            visual.flags = FlagAir;
            continue;
        case Family::Invisible:
            visual.flags = 0;
            continue;
        case Family::Deferred:
            if (classifyBlockEntity(name, record.states, visual)) {
                visual.flags = FlagModel;
            } else {
                visual.flags = FlagDiagnostic;
                ++diagnosticCount;
            }
            continue;
        case Family::Liquid:
            visual.flags = FlagCubeGeometry | FlagCullSame;
            break;
        case Family::Model:
            visual.flags = FlagModel;
            break;
        case Family::Leaves:
            visual.flags = FlagCubeGeometry | FlagLeafModel;
            break;
        case Family::TransparentCube:
            visual.flags = FlagCubeGeometry | FlagCullSame;
            break;
        case Family::Cube:
            visual.flags = FlagCubeGeometry | FlagOccludesFullFace;
            break;
        }
        if (isTranslucentName(name)) {
            visual.flags |= FlagTranslucent;
            if (visual.flags & FlagOccludesFullFace) {
                visual.flags = static_cast<uint8_t>((visual.flags & ~FlagOccludesFullFace) | FlagCullSame);
            }
        }
        uint32_t tint = contains(name, "water") && family == Family::Liquid ? 0xFFFFFFu : 0u;

        const json::Value* entry = blockEntry(name);
        if (!entry) {
            if (const char* alias = legacyAlias(name)) {
                entry = blockEntry(alias);
            }
        }
        const json::Value* textures = entry ? entry->get("textures") : nullptr;
        std::string fallbackKey;
        if (!textures) {
            std::vector<std::string> candidates { name };
            if (startsWith(name, "hard_")) {
                candidates.push_back(name.substr(5));
            }
            if (startsWith(name, "underwater_")) {
                candidates.push_back(name.substr(11));
            }
            for (const std::string& candidate : candidates) {
                if (const json::Value* candidateEntry = blockEntry(candidate)) {
                    if ((textures = candidateEntry->get("textures"))) {
                        break;
                    }
                }
                if (terrainKnown(candidate)) {
                    fallbackKey = candidate;
                    break;
                }
            }
        }
        Axis axis = stateAxis(record.states);

        if (family == Family::Model) {
            static constexpr Face order[] = { Face::West, Face::East, Face::Down, Face::Up, Face::North, Face::South };
            auto faceKeyFor = [&](int face) {
                bool rotate = false;
                return textures ? resolveTextureKey(textures, order[face], Axis::Y, std::nullopt, rotate) : fallbackKey;
            };
            models::Materials materials {};
            bool complete = true;
            for (int face = 0; face < 6; ++face) {
                std::string key = faceKeyFor(face);
                materials[face] = key.empty() ? DiagnosticMaterial : materialFor(key, false, variantFor(key, name, record.states), 0, blockTint(name, face));
                complete &= materials[face] != DiagnosticMaterial;
            }
            auto uniform = [&](int face) {
                models::Materials same;
                same.fill(materials[face]);
                return same;
            };
            auto flag = [&](const char* key) {
                std::optional<int32_t> value = stateInt(record.states, key);
                return value && *value != 0;
            };
            auto cardinal = [&]() -> uint32_t {
                std::string value = stateString(record.states, "minecraft:cardinal_direction");
                if (value == "south") {
                    return 0;
                }
                if (value == "west") {
                    return 1;
                }
                if (value == "north") {
                    return 2;
                }
                if (value == "east") {
                    return 3;
                }
                std::optional<int32_t> direction = stateInt(record.states, "direction");
                return direction ? static_cast<uint32_t>(*direction & 3) : 0;
            };

            ModelKind kind = modelKind(name);
            uint32_t modelTemplate = NoModelTemplate;
            uint32_t variant = 0;
            switch (kind) {
            case ModelKind::Slab: {
                if (!complete) {
                    break;
                }
                uint32_t half = contains(name, "double") ? 2 : 0;
                if (half == 0 && (stateString(record.states, "minecraft:vertical_half") == "top" || flag("top_slot_bit"))) {
                    half = 1;
                }
                modelTemplate = intern(keyOf("slab", materials, { half }), [&] {
                    pushTemplate(models::slab(materials, half), 0);
                });
                if (half == 2) {
                    visual.flags |= FlagOccludesFullFace;
                }
                break;
            }
            case ModelKind::Stair: {
                if (!complete) {
                    break;
                }
                std::optional<int32_t> weirdo = stateInt(record.states, "weirdo_direction");
                bool upside = flag("upside_down_bit");
                modelTemplate = intern(keyOf("stair", materials, { upside }), [&] {
                    for (uint32_t shape = 0; shape < 5; ++shape) {
                        pushTemplate(models::stair(materials, upside, shape), TemplateStair);
                    }
                });
                variant = ((static_cast<uint32_t>(weirdo.value_or(0)) + 2) & 3) | (upside ? 4u : 0u);
                break;
            }
            case ModelKind::Fence: {
                uint32_t material = materials[models::South];
                if (material == DiagnosticMaterial) {
                    break;
                }
                uint32_t fenceFlag = name == "nether_brick_fence" ? TemplateFenceNether : TemplateFenceWood;
                modelTemplate = intern(keyOf("fence", uniform(models::South), { fenceFlag }), [&] {
                    pushTemplate(models::fencePost(material), fenceFlag);
                    for (uint32_t mask = 0; mask < 16; ++mask) {
                        pushTemplate(models::fenceArms(material, mask), fenceFlag);
                    }
                });
                break;
            }
            case ModelKind::Pane: {
                uint32_t body = materials[models::North];
                uint32_t edge = materials[models::East];
                if (body == DiagnosticMaterial || edge == DiagnosticMaterial) {
                    break;
                }
                modelTemplate = intern(keyOf("pane", { body, edge, 0, 0, 0, 0 }, {}), [&] {
                    for (uint32_t mask = 0; mask < 16; ++mask) {
                        pushTemplate(models::pane(body, edge, mask), TemplatePane);
                    }
                });
                break;
            }
            case ModelKind::Wall: {
                if (!complete) {
                    break;
                }
                uint32_t connections = flag("wall_post_bit") ? 1u << 8 : 0u;
                static const std::pair<const char*, uint32_t> sides[] = {
                    { "wall_connection_type_north", 0 },
                    { "wall_connection_type_east", 2 },
                    { "wall_connection_type_south", 4 },
                    { "wall_connection_type_west", 6 },
                };
                for (const auto& [key, shift] : sides) {
                    std::string value = stateString(record.states, key);
                    uint32_t height = value == "short" ? 1 : (value == "tall" ? 2 : 0);
                    connections |= height << shift;
                }
                modelTemplate = intern(keyOf("wall", materials, { connections }), [&] {
                    pushTemplate(models::wall(materials, connections), TemplateWall);
                });
                break;
            }
            case ModelKind::Door: {
                bool upper = flag("upper_block_bit");
                uint32_t material = materials[upper ? models::South : models::Down];
                if (material == DiagnosticMaterial) {
                    break;
                }
                auto [min, max] = models::doorBounds(cardinal(), flag("open_bit"), flag("door_hinge_bit"));
                models::Materials same;
                same.fill(material);
                modelTemplate = intern(keyOf("cuboid", same, { uint32_t(min[0]), uint32_t(min[1]), uint32_t(min[2]), uint32_t(max[0]), uint32_t(max[1]), uint32_t(max[2]) }), [&] {
                    auto faces = models::cuboid(same, min, max);
                    pushTemplate({ faces.begin(), faces.end() }, 0);
                });
                break;
            }
            case ModelKind::Trapdoor: {
                if (!complete) {
                    break;
                }
                std::optional<int32_t> direction = stateInt(record.states, "direction");
                auto [min, max] = models::trapdoorBounds(static_cast<uint32_t>(direction.value_or(0) & 3), flag("open_bit"), flag("upside_down_bit"));
                modelTemplate = intern(keyOf("cuboid", materials, { uint32_t(min[0]), uint32_t(min[1]), uint32_t(min[2]), uint32_t(max[0]), uint32_t(max[1]), uint32_t(max[2]) }), [&] {
                    auto faces = models::cuboid(materials, min, max);
                    pushTemplate({ faces.begin(), faces.end() }, 0);
                });
                break;
            }
            case ModelKind::Gate: {
                if (!complete) {
                    break;
                }
                uint32_t orientation = cardinal();
                bool open = flag("open_bit");
                bool inWall = flag("in_wall_bit");
                bool bamboo = name == "bamboo_fence_gate";
                uint32_t axisFlag = (orientation & 1) == 0 ? TemplateGateAxisZ : TemplateGateAxisX;
                modelTemplate = intern(keyOf("gate", materials, { orientation, open, inWall, bamboo }), [&] {
                    pushTemplate(models::gate(materials, orientation, open, inWall, bamboo), axisFlag);
                });
                break;
            }
            case ModelKind::Carpet:
            case ModelKind::SnowLayer:
            case ModelKind::Farmland:
            case ModelKind::Cake:
            case ModelKind::Cactus:
            case ModelKind::SinkingCube:
            case ModelKind::Chest: {
                if (!complete) {
                    break;
                }
                models::Point min { 0, 0, 0 };
                models::Point max { 256, 256, 256 };
                if (kind == ModelKind::Carpet) {
                    max[1] = 16;
                } else if (kind == ModelKind::SnowLayer) {
                    int32_t layersHigh = std::clamp(stateInt(record.states, "height_in_layers").value_or(0), 0, 7);
                    max[1] = static_cast<int16_t>((layersHigh + 1) * 32);
                } else if (kind == ModelKind::Farmland) {
                    max[1] = 240;
                } else if (kind == ModelKind::SinkingCube) {
                    max[1] = 224;
                } else if (kind == ModelKind::Cake) {
                    int32_t bites = std::clamp(stateInt(record.states, "bite_counter").value_or(0), 0, 6);
                    min = { static_cast<int16_t>(16 + bites * 32), 0, 16 };
                    max = { 240, 128, 240 };
                } else if (kind == ModelKind::Cactus) {
                    min = { 16, 0, 16 };
                    max = { 240, 256, 240 };
                } else {
                    min = { 16, 0, 16 };
                    max = { 240, 224, 240 };
                }
                modelTemplate = intern(keyOf("cuboid", materials, { uint32_t(min[0]), uint32_t(min[1]), uint32_t(min[2]), uint32_t(max[0]), uint32_t(max[1]), uint32_t(max[2]) }), [&] {
                    auto faces = models::cuboid(materials, min, max);
                    if (kind == ModelKind::Cactus) {
                        faces[models::Down].positions = { { { 0, 0, 0 }, { 256, 0, 0 }, { 256, 0, 256 }, { 0, 0, 256 } } };
                        faces[models::Up].positions = { { { 0, 256, 0 }, { 0, 256, 256 }, { 256, 256, 256 }, { 256, 256, 0 } } };
                        faces[models::Down].uvs = { { { 0, 0 }, { 4096, 0 }, { 4096, 4096 }, { 0, 4096 } } };
                        faces[models::Up].uvs = { { { 0, 0 }, { 0, 4096 }, { 4096, 4096 }, { 4096, 0 } } };
                    }
                    pushTemplate({ faces.begin(), faces.end() }, 0);
                });
                break;
            }
            case ModelKind::PressurePlate: {
                if (!complete) {
                    break;
                }
                bool pressed = stateInt(record.states, "redstone_signal").value_or(0) > 0;
                modelTemplate = intern(keyOf("plate", materials, { pressed }), [&] {
                    pushTemplate(models::pressurePlate(materials, pressed), 0);
                });
                break;
            }
            case ModelKind::Button: {
                if (!complete) {
                    break;
                }
                uint32_t orientation = static_cast<uint32_t>(std::clamp(stateInt(record.states, "facing_direction").value_or(1), 0, 5));
                bool pressed = flag("button_pressed_bit");
                modelTemplate = intern(keyOf("button", materials, { orientation, pressed }), [&] {
                    pushTemplate(models::button(materials, orientation, pressed), 0);
                });
                break;
            }
            case ModelKind::Torch: {
                uint32_t material = materials[models::Up];
                if (material == DiagnosticMaterial) {
                    break;
                }
                std::string direction = stateString(record.states, "torch_facing_direction");
                uint32_t facingIndex = direction == "west" ? 1 : direction == "east" ? 2 : direction == "north" ? 3 : direction == "south" ? 4 : 0;
                modelTemplate = intern(keyOf("torch", uniform(models::Up), { facingIndex }), [&] {
                    pushTemplate(models::torch(material, facingIndex), 0);
                });
                break;
            }
            case ModelKind::Rail: {
                int32_t direction = stateInt(record.states, "rail_direction").value_or(0);
                bool curved = direction >= 6;
                uint32_t material = materials[curved ? models::Up : models::Down];
                if (name != "rail") {
                    material = materials[flag("rail_data_bit") ? models::Up : models::Down];
                }
                if (material == DiagnosticMaterial) {
                    break;
                }
                static constexpr uint32_t Rotations[10] = { 0, 1, 1, 1, 0, 0, 0, 1, 2, 3 };
                modelTemplate = intern(keyOf("flat", { material, 0, 0, 0, 0, 0 }, { 16 }), [&] {
                    pushTemplate(models::flatPlane(material, 16), 0);
                });
                variant = Rotations[std::clamp(direction, 0, 9)];
                break;
            }
            case ModelKind::Lily: {
                uint32_t material = materials[models::Up];
                if (material == DiagnosticMaterial) {
                    break;
                }
                modelTemplate = intern(keyOf("flat", uniform(models::Up), { 4 }), [&] {
                    pushTemplate(models::flatPlane(material, 4), 0);
                });
                break;
            }
            case ModelKind::Ladder:
            case ModelKind::Vine:
            case ModelKind::Multiface: {
                uint32_t material = materials[models::South];
                if (material == DiagnosticMaterial) {
                    break;
                }
                uint32_t sides = 0;
                if (kind == ModelKind::Ladder) {
                    static constexpr uint32_t Attached[6] = { models::South, models::South, models::South, models::North, models::East, models::West };
                    sides = 1u << Attached[std::clamp(stateInt(record.states, "facing_direction").value_or(2), 0, 5)];
                } else if (kind == ModelKind::Vine) {
                    int32_t bits = stateInt(record.states, "vine_direction_bits").value_or(0);
                    static constexpr uint32_t VineSides[4] = { models::South, models::West, models::North, models::East };
                    for (int bit = 0; bit < 4; ++bit) {
                        if (bits & (1 << bit)) {
                            sides |= 1u << VineSides[bit];
                        }
                    }
                } else {
                    int32_t bits = stateInt(record.states, "multi_face_direction_bits").value_or(0);
                    static constexpr uint32_t MultiSides[6] = { models::Down, models::Up, models::South, models::West, models::North, models::East };
                    for (int bit = 0; bit < 6; ++bit) {
                        if (bits & (1 << bit)) {
                            sides |= 1u << MultiSides[bit];
                        }
                    }
                }
                if (sides == 0) {
                    sides = 1u << models::Up;
                }
                modelTemplate = intern(keyOf("planes", uniform(models::South), { sides }), [&] {
                    pushTemplate(models::attachedPlanes(material, sides), 0);
                });
                break;
            }
            case ModelKind::Cross: {
                int face = models::Up;
                if (textures && textures->isObject()) {
                    if (name == "sweet_berry_bush") {
                        static constexpr int ByGrowth[4] = { models::Down, models::Up, models::North, models::South };
                        face = ByGrowth[std::clamp(stateInt(record.states, "growth").value_or(0), 0, 3)];
                    } else if (textures->get("up") && textures->get("down") && name != "seagrass") {
                        face = flag("upper_block_bit") ? models::Up : models::Down;
                    }
                }
                uint32_t material = materials[face];
                if (material == DiagnosticMaterial) {
                    break;
                }
                modelTemplate = intern(keyOf("cross", uniform(face), {}), [&] {
                    pushTemplate(models::cross(material, material), 0);
                });
                break;
            }
            case ModelKind::Cluster: {
                uint32_t material = materials[models::Up];
                if (material == DiagnosticMaterial) {
                    break;
                }
                std::string direction = stateString(record.states, "minecraft:block_face");
                static const std::pair<const char*, uint32_t> Sides[] = {
                    { "down", models::Down }, { "up", models::Up }, { "north", models::North },
                    { "south", models::South }, { "west", models::West }, { "east", models::East },
                };
                uint32_t facing = models::Up;
                for (const auto& [label, side] : Sides) {
                    if (direction == label) {
                        facing = side;
                    }
                }
                modelTemplate = intern(keyOf("cluster", uniform(models::Up), { facing }), [&] {
                    pushTemplate(models::orientedCross(material, facing), 0);
                });
                break;
            }
            case ModelKind::Bamboo: {
                uint32_t stem = materials[models::North];
                if (stem == DiagnosticMaterial) {
                    break;
                }
                std::string leafSize = stateString(record.states, "bamboo_leaf_size");
                uint32_t leaves = leafSize == "large_leaves" ? materials[models::Up] : leafSize == "small_leaves" ? materials[models::South] : DiagnosticMaterial;
                bool thick = stateString(record.states, "bamboo_stalk_thickness") == "thick";
                modelTemplate = intern(keyOf("bamboo", { stem, leaves, 0, 0, 0, 0 }, { thick }), [&] {
                    pushTemplate(models::bamboo(stem, leaves, thick), 0);
                });
                break;
            }
            case ModelKind::Sign: {
                uint32_t material = materials[models::South];
                if (material == DiagnosticMaterial) {
                    break;
                }
                uint32_t facing = static_cast<uint32_t>(std::clamp(stateInt(record.states, "facing_direction").value_or(2), 0, 5));
                uint32_t rotation = static_cast<uint32_t>(stateInt(record.states, "ground_sign_direction").value_or(0) & 15);
                if (endsWith(name, "standing_sign")) {
                    modelTemplate = intern(keyOf("sign_standing", uniform(models::South), { rotation }), [&] {
                        pushTemplate(models::standingSign(material, rotation), 0);
                    });
                } else if (endsWith(name, "wall_sign")) {
                    modelTemplate = intern(keyOf("sign_wall", uniform(models::South), { facing }), [&] {
                        pushTemplate(models::wallSign(material, facing), 0);
                    });
                } else if (flag("hanging")) {
                    bool attached = flag("attached_bit");
                    modelTemplate = intern(keyOf("sign_ceiling", uniform(models::South), { rotation, attached }), [&] {
                        pushTemplate(models::hangingCeilingSign(material, rotation, attached), 0);
                    });
                } else {
                    modelTemplate = intern(keyOf("sign_hanging_wall", uniform(models::South), { facing }), [&] {
                        pushTemplate(models::hangingWallSign(material, facing), 0);
                    });
                }
                break;
            }
            case ModelKind::None:
                break;
            }

            if (modelTemplate == NoModelTemplate) {
                visual.flags = FlagDiagnostic;
                visual.faces.fill(DiagnosticMaterial);
                ++diagnosticCount;
            } else {
                visual.faces = materials;
                visual.modelTemplate = modelTemplate;
                visual.variant = variant;
            }
            continue;
        }

        std::optional<Face> facing = stateFacing(record.states);
        bool resolved = true;
        for (int face = 0; face < 6; ++face) {
            static constexpr Face order[] = { Face::West, Face::East, Face::Down, Face::Up, Face::North, Face::South };
            bool rotate = false;
            std::string key = textures ? resolveTextureKey(textures, order[face], axis, facing, rotate) : fallbackKey;
            visual.faces[face] = key.empty() ? DiagnosticMaterial : materialFor(key, rotate, 0, tint, blockTint(name, face));
            resolved &= visual.faces[face] != DiagnosticMaterial;
        }
        if (!resolved) {
            visual.flags = FlagDiagnostic;
            visual.faces.fill(DiagnosticMaterial);
            ++diagnosticCount;
        } else if (family == Family::Liquid) {
            visual.liquid = contains(name, "lava") ? 2 : 1;
            visual.liquidLevel = static_cast<uint8_t>(std::clamp(stateInt(record.states, "liquid_depth").value_or(0), 0, 15));
            visual.flags = static_cast<uint8_t>(visual.flags & FlagTranslucent);
        }
    }

    std::unordered_set<std::string> vanillaNames;
    for (const BlockRecord& record : registry.records()) {
        vanillaNames.insert(record.name);
    }
    GeometryLibrary geometries;
    geometries.load(packs);
    static constexpr const char* FaceNames[6] = { "west", "east", "down", "up", "north", "south" };

    for (size_t block = 0; block < customs.size(); ++block) {
        const CustomBlock& custom = customs[block];
        if (vanillaNames.contains(custom.name)) {
            continue;
        }
        for (Tag& states : enumerateCustomStates(custom.definition)) {
            std::map<std::string, const Tag*> components;
            collectComponents(custom.definition.get("components"), components);
            if (const Tag* permutations = custom.definition.get("permutations"); permutations && permutations->getType() == Tag::Type::List) {
                for (const Tag& permutation : permutations->getList()) {
                    const Tag* condition = permutation.get("condition");
                    if (condition && condition->getType() == Tag::Type::String && evaluateCondition(condition->asString(), states)) {
                        collectComponents(permutation.get("components"), components);
                    }
                }
            }

            const Tag* instances = components.contains("minecraft:material_instances") ? components["minecraft:material_instances"] : nullptr;
            const Tag* materialMap = instances ? instances->get("materials") : nullptr;
            const Tag* mappings = instances ? instances->get("mappings") : nullptr;
            bool translucent = false;
            bool cutout = false;
            auto instanceMaterial = [&](const std::string& instanceName, int side) -> uint32_t {
                if (!materialMap || materialMap->getType() != Tag::Type::Compound) {
                    return DiagnosticMaterial;
                }
                std::vector<std::string> candidates;
                if (!instanceName.empty()) {
                    candidates.push_back(instanceName);
                }
                candidates.push_back(FaceNames[side]);
                candidates.push_back("*");
                for (std::string candidate : candidates) {
                    if (mappings && mappings->getType() == Tag::Type::Compound) {
                        if (const Tag* mapped = mappings->get(candidate); mapped && mapped->getType() == Tag::Type::String) {
                            candidate = mapped->asString();
                        }
                    }
                    const Tag* instance = materialMap->get(candidate);
                    const Tag* texture = instance ? instance->get("texture") : nullptr;
                    if (!texture || texture->getType() != Tag::Type::String) {
                        continue;
                    }
                    if (const Tag* method = instance->get("render_method"); method && method->getType() == Tag::Type::String) {
                        translucent |= method->asString() == "blend";
                        cutout |= method->asString() != "opaque";
                    }
                    return materialFor(texture->asString(), false);
                }
                return DiagnosticMaterial;
            };

            std::string geometryName = "minecraft:geometry.full_block";
            if (const Tag* geometry = components.contains("minecraft:geometry") ? components["minecraft:geometry"] : nullptr) {
                if (const Tag* identifier = geometry->get("identifier"); identifier && identifier->getType() == Tag::Type::String) {
                    geometryName = identifier->asString();
                }
            }
            BlockTransform transform;
            if (const Tag* transformation = components.contains("minecraft:transformation") ? components["minecraft:transformation"] : nullptr) {
                transform.rotation = { tagNumber(transformation->get("RX")) * 90.0f, tagNumber(transformation->get("RY")) * 90.0f, tagNumber(transformation->get("RZ")) * 90.0f };
                transform.scale = { tagNumber(transformation->get("SX"), 1.0f), tagNumber(transformation->get("SY"), 1.0f), tagNumber(transformation->get("SZ"), 1.0f) };
                transform.translation = { tagNumber(transformation->get("TX")), tagNumber(transformation->get("TY")), tagNumber(transformation->get("TZ")) };
            }

            BlockVisual visual;
            if (geometryName == "minecraft:geometry.full_block" || components.contains("minecraft:unit_cube")) {
                for (int side = 0; side < 6; ++side) {
                    visual.faces[side] = instanceMaterial({}, side);
                }
                visual.flags = FlagCubeGeometry | (translucent || cutout ? FlagCullSame : FlagOccludesFullFace) | (translucent ? FlagTranslucent : 0);
            } else if (const Geometry* geometry = geometries.find(geometryName)) {
                std::vector<ModelQuad> modelQuads = buildGeometryQuads(*geometry, transform, instanceMaterial);
                visual.flags = FlagModel | (translucent ? FlagTranslucent : 0);
                visual.faces.fill(modelQuads.empty() ? DiagnosticMaterial : modelQuads.front().material);
                visual.modelTemplate = modelQuads.empty() ? NoModelTemplate : pushTemplate(modelQuads, 0);
            }
            if (visual.flags == 0 || (visual.flags & FlagModel && visual.modelTemplate == NoModelTemplate)) {
                visual = BlockVisual { FlagDiagnostic, {} };
                ++diagnosticCount;
            }
            visual.lightFilter = 15;
            if (const Tag* emission = components.contains("minecraft:light_emission") ? components["minecraft:light_emission"] : nullptr) {
                visual.lightEmission = static_cast<uint8_t>(std::clamp(static_cast<int32_t>(tagNumber(emission->get("emission"))), 0, 15));
            }
            if (const Tag* dampening = components.contains("minecraft:light_dampening") ? components["minecraft:light_dampening"] : nullptr) {
                visual.lightFilter = static_cast<uint8_t>(std::clamp(static_cast<int32_t>(tagNumber(dampening->get("lightLevel"), 15.0f)), 0, 15));
            }

            uint32_t index = static_cast<uint32_t>(visuals.size());
            customByHash.emplace(static_cast<uint32_t>(BlockStateHasher::hash(custom.name, states)), index);
            customStates.push_back({ block, std::move(states) });
            visuals.push_back(visual);
        }
    }

    applyBlockLight(registry, visuals);

    auto loadImage = [&](const std::string& path, uint32_t& width, uint32_t& height, std::vector<uint8_t>& rgba) {
        std::string encoded;
        return pack.readTexture(path, encoded) && ui::decodeImage(encoded, width, height, rgba) && width > 0 && height > 0;
    };
    auto tileOf = [](const std::vector<uint8_t>& rgba, uint32_t width, uint32_t x0, uint32_t y0, uint32_t size) {
        std::vector<uint8_t> tile(size_t(size) * size * 4);
        for (uint32_t y = 0; y < size; ++y) {
            const uint8_t* source = rgba.data() + (size_t(y0 + y) * width + x0) * 4;
            std::copy(source, source + size_t(size) * 4, tile.data() + size_t(y) * size * 4);
        }
        return normalizeTexture(tile, size, size);
    };

    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
    if (loadImage("textures/environment/sun", width, height, rgba)) {
        sun = static_cast<uint32_t>(layers.size());
        layers.push_back(tileOf(rgba, width, 0, 0, std::min(width, height)));
    }
    if (loadImage("textures/environment/moon_phases", width, height, rgba) && width >= 4 && height >= 2) {
        uint32_t size = std::min(width / 4, height / 2);
        for (uint32_t phase = 0; phase < 8; ++phase) {
            moonPhases[phase] = static_cast<uint32_t>(layers.size());
            layers.push_back(tileOf(rgba, width, (phase % 4) * size, (phase / 4) * size, size));
        }
    }

    buildBlockEntityTemplates(pack, layers, overlayLayers, materialByKey, pushTemplate);
    buildEntityModels(pack, packs);
    buildInterfaceAssets(pack);

    std::filesystem::path behaviorRoot = root.parent_path().parent_path() / "behavior_packs" / root.filename();
    PackSource behaviors(behaviorRoot);
    biomes.load(pack, behaviors);

    overlayLayers.resize(layers.size(), false);
    buildMips(textureArray, layers, overlayLayers);
    return true;
}

namespace {

constexpr const char* BedColors[DyeColors] = { "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "silver", "cyan", "purple", "blue", "brown", "green", "red", "black" };
constexpr uint32_t BannerColors[DyeColors] = { 0x1D1D21, 0xB02E26, 0x5E7C16, 0x835432, 0x3C44AA, 0x8932B8, 0x169C9C, 0x9D9D97, 0x474F52, 0xF38BAA, 0x80C71F, 0xFED83D, 0x3AB3DA, 0xC74EBD, 0xF9801D, 0xF9FFFE };
constexpr uint32_t DefaultBedColor = 14;
constexpr uint32_t DefaultBannerColor = 15;

int32_t entityInt(const Tag* data, const char* key, int32_t fallback)
{
    const Tag* value = data ? data->get(key) : nullptr;
    if (!value) {
        return fallback;
    }
    switch (value->getType()) {
    case Tag::Type::Byte:
        return value->asByte();
    case Tag::Type::Short:
        return value->asShort();
    case Tag::Type::Int:
        return value->asInt();
    case Tag::Type::Float:
        return static_cast<int32_t>(std::lround(value->asFloat()));
    default:
        return fallback;
    }
}

float entityFloat(const Tag* data, const char* key)
{
    const Tag* value = data ? data->get(key) : nullptr;
    if (!value) {
        return 0.0f;
    }
    return value->getType() == Tag::Type::Float ? value->asFloat() : static_cast<float>(entityInt(data, key, 0));
}

}

namespace {

std::vector<uint64_t> engineVersion(const std::string& text)
{
    std::vector<uint64_t> numbers;
    uint64_t current = 0;
    bool inNumber = false;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            current = current * 10 + uint64_t(c - '0');
            inNumber = true;
        } else if (inNumber) {
            numbers.push_back(current);
            current = 0;
            inNumber = false;
        }
    }
    if (inNumber) {
        numbers.push_back(current);
    }
    return numbers;
}

std::vector<uint8_t> resizeNearest(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, uint32_t size)
{
    std::vector<uint8_t> out(size_t(size) * size * 4);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const uint8_t* source = rgba.data() + (size_t(y * height / size) * width + x * width / size) * 4;
            std::copy(source, source + 4, out.data() + (size_t(y) * size + x) * 4);
        }
    }
    return out;
}

std::string lowerName(std::string text);

/**
 * A client entity definition: its textures and geometries by lowercase short
 * name, the default of each, and its render controllers with their condition
 * source (empty when unconditional).
 */
struct ClientEntity {
    std::string texture;
    std::string geometry;
    std::map<std::string, std::string> textures;
    std::map<std::string, std::string> geometries;
    std::vector<std::pair<std::string, std::string>> renderControllers;
    std::vector<uint64_t> version;
    std::shared_ptr<const EntityScripts> scripts;
};

std::map<std::string, std::string> readNamedStrings(const json::Value* table)
{
    std::map<std::string, std::string> out;
    if (!table) {
        return out;
    }
    for (const std::string& key : table->mKeys) {
        const json::Value& value = *table->mObject.at(key);
        if (value.isString()) {
            out[lowerName(key)] = value.mString;
        }
    }
    return out;
}

void readClientEntity(const std::string& text, std::map<std::string, ClientEntity>& out)
{
    std::unique_ptr<json::Value> document = json::parse(stripComments(text));
    const json::Value* entity = document ? document->get("minecraft:client_entity") : nullptr;
    const json::Value* description = entity ? entity->get("description") : nullptr;
    const json::Value* identifier = description ? description->get("identifier") : nullptr;
    if (!identifier || !identifier->isString()) {
        return;
    }
    ClientEntity parsed;
    parsed.textures = readNamedStrings(description->get("textures"));
    parsed.geometries = readNamedStrings(description->get("geometry"));
    if (parsed.textures.empty() || parsed.geometries.empty()) {
        return;
    }
    auto texture = parsed.textures.find("default");
    auto model = parsed.geometries.find("default");
    parsed.texture = texture != parsed.textures.end() ? texture->second : parsed.textures.begin()->second;
    parsed.geometry = model != parsed.geometries.end() ? model->second : parsed.geometries.begin()->second;
    if (const json::Value* controllers = description->get("render_controllers"); controllers && controllers->isArray()) {
        for (const std::unique_ptr<json::Value>& entry : controllers->mArray) {
            if (entry->isString()) {
                parsed.renderControllers.emplace_back(lowerName(entry->mString), std::string {});
                continue;
            }
            for (const std::string& key : entry->mKeys) {
                const json::Value& condition = *entry->mObject.at(key);
                parsed.renderControllers.emplace_back(lowerName(key), condition.isString() ? condition.mString : std::string {});
            }
        }
    }
    parsed.scripts = readEntityScripts(*description);
    if (const json::Value* version = description->get("min_engine_version"); version && version->isString()) {
        parsed.version = engineVersion(version->mString);
    }
    auto existing = out.find(identifier->mString);
    if (existing == out.end() || parsed.version >= existing->second.version) {
        out[identifier->mString] = std::move(parsed);
    }
}

std::array<float, 3> rotateEulerAround(std::array<float, 3> point, const std::array<float, 3>& pivot, const std::array<float, 3>& degrees)
{
    constexpr float Radians = 3.14159265f / 180.0f;
    float sx = std::sin(degrees[0] * Radians);
    float cx = std::cos(degrees[0] * Radians);
    float sy = std::sin(degrees[1] * Radians);
    float cy = std::cos(degrees[1] * Radians);
    float sz = std::sin(degrees[2] * Radians);
    float cz = std::cos(degrees[2] * Radians);
    std::array<float, 3> value { point[0] - pivot[0], point[1] - pivot[1], point[2] - pivot[2] };
    value = { value[0], value[1] * cx - value[2] * sx, value[1] * sx + value[2] * cx };
    value = { value[0] * cy + value[2] * sy, value[1], -value[0] * sy + value[2] * cy };
    value = { value[0] * cz - value[1] * sz, value[0] * sz + value[1] * cz, value[2] };
    return { value[0] + pivot[0], value[1] + pivot[1], value[2] + pivot[2] };
}

std::string lowerName(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

/**
 * Turns a geometry into an animatable model. Geometry files mirror the x axis
 * and the x and y rotations, so pivots, cubes and rotations are flipped back
 * into world handedness; each cube is turned around its own pivot and its
 * quads stay unposed, tagged with their bone, with the box or per-face UV
 * unwrap over the whole texture.
 */
void buildEntityRig(const Geometry& geometry, EntityRig& model)
{
    std::map<std::string, int32_t> indexByName;
    for (const GeometryBone& bone : geometry.bones) {
        indexByName.emplace(lowerName(bone.name), static_cast<int32_t>(model.bones.size()));
        EntityBone rigBone;
        rigBone.name = bone.name;
        rigBone.pivot = { -bone.pivot[0], bone.pivot[1], bone.pivot[2] };
        rigBone.rotation = { -bone.rotation[0], -bone.rotation[1], bone.rotation[2] };
        model.bones.push_back(rigBone);
    }
    for (size_t index = 0; index < geometry.bones.size(); ++index) {
        auto found = indexByName.find(lowerName(geometry.bones[index].parent));
        if (found != indexByName.end() && found->second != static_cast<int32_t>(index)) {
            model.bones[index].parent = found->second;
        }
    }
    for (size_t index = 0; index < model.bones.size(); ++index) {
        int32_t walker = model.bones[index].parent;
        for (size_t steps = 0; walker >= 0; ++steps) {
            if (walker == static_cast<int32_t>(index) || steps > model.bones.size()) {
                model.bones[index].parent = -1;
                break;
            }
            walker = model.bones[walker].parent;
        }
    }

    static constexpr std::array<float, 3> FaceNormals[6] = { { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    static constexpr std::array<float, 3> FaceRights[6] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { 1, 0, 0 } };
    static constexpr std::array<float, 3> FaceUps[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    static constexpr float CornerSigns[4][2] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } };
    static constexpr uint32_t FaceIds[6] = { 5, 6, 3, 4, 2, 1 };
    static constexpr int GeometrySides[6] = { 4, 5, 0, 1, 3, 2 };
    float width = geometry.textureWidth > 0.0f ? geometry.textureWidth : 64.0f;
    float height = geometry.textureHeight > 0.0f ? geometry.textureHeight : 64.0f;

    for (size_t boneIndex = 0; boneIndex < geometry.bones.size(); ++boneIndex) {
        const GeometryBone& bone = geometry.bones[boneIndex];
        if (bone.neverRender) {
            continue;
        }
        for (const GeometryCube& cube : bone.cubes) {
            float inflate = cube.inflate + bone.inflate;
            std::array<float, 3> min {
                -(cube.origin[0] + cube.size[0]) - inflate,
                cube.origin[1] - inflate,
                cube.origin[2] - inflate,
            };
            std::array<float, 3> max {
                -cube.origin[0] + inflate,
                cube.origin[1] + cube.size[1] + inflate,
                cube.origin[2] + cube.size[2] + inflate,
            };
            std::array<float, 3> cubePivot { -cube.pivot[0], cube.pivot[1], cube.pivot[2] };
            std::array<float, 3> cubeRotation { -cube.rotation[0], -cube.rotation[1], cube.rotation[2] };
            bool mirror = cube.mirror != bone.mirror;
            float x = cube.size[0];
            float y = cube.size[1];
            float z = cube.size[2];
            for (int face = 0; face < 6; ++face) {
                std::array<float, 4> region {};
                if (cube.boxUv) {
                    float u = cube.uv[0];
                    float v = cube.uv[1];
                    const std::array<float, 4> boxRegions[6] = {
                        { u + z, v + z, x, y },
                        { u + z + x + z, v + z, x, y },
                        { u + z + x, v + z, z, y },
                        { u, v + z, z, y },
                        { u + z, v, x, z },
                        { u + z + x, v, x, z },
                    };
                    int source = face;
                    if (mirror && (face == 2 || face == 3)) {
                        source = face == 2 ? 3 : 2;
                    }
                    region = boxRegions[source];
                } else {
                    const GeometryFace& uv = cube.faces[GeometrySides[face]];
                    if (!uv.present) {
                        continue;
                    }
                    region = { uv.uv[0], uv.uv[1], uv.size[0], uv.size[1] };
                }
                float left = region[0] / width;
                float right = (region[0] + region[2]) / width;
                if (mirror) {
                    std::swap(left, right);
                }
                float top = region[1] / height;
                float bottom = (region[1] + region[3]) / height;
                const std::array<std::array<float, 2>, 4> uvs { { { left, top }, { right, top }, { right, bottom }, { left, bottom } } };
                const std::array<float, 3>& normal = FaceNormals[face];
                const std::array<float, 3>& rightAxis = FaceRights[face];
                const std::array<float, 3>& upAxis = FaceUps[face];
                ModelQuad quad;
                for (size_t corner = 0; corner < 4; ++corner) {
                    std::array<float, 3> point {};
                    for (int axis = 0; axis < 3; ++axis) {
                        float center = (min[axis] + max[axis]) * 0.5f;
                        float half = (max[axis] - min[axis]) * 0.5f;
                        float offset = normal[axis] + rightAxis[axis] * CornerSigns[corner][0] + upAxis[axis] * CornerSigns[corner][1];
                        point[axis] = center + offset * half;
                    }
                    point = rotateEulerAround(point, cubePivot, cubeRotation);
                    for (int axis = 0; axis < 3; ++axis) {
                        quad.positions[corner][axis] = static_cast<int16_t>(std::lround(point[axis] * 16.0f));
                    }
                    const std::array<float, 2>& uv = uvs[corner];
                    quad.uvs[corner] = { static_cast<uint16_t>(std::clamp(uv[0], 0.0f, 15.0f) * 4096.0f), static_cast<uint16_t>(std::clamp(uv[1], 0.0f, 15.0f) * 4096.0f) };
                }
                quad.flags = FaceIds[face] | QuadTwoSided;
                model.quads.push_back(quad);
                model.quadBones.push_back(static_cast<uint16_t>(boneIndex));
            }
        }
    }
}

using ControllerArrays = std::unordered_map<std::string, std::vector<std::string>>;

/**
 * A render controller as read from its file: the geometry and texture
 * selectors rewritten into Molang yielding an index into their lowercase
 * choice names (like texture.white), and the part visibility rules.
 */
struct RenderControllerSource {
    molang::Script geometry;
    std::vector<std::string> geometryChoices;
    molang::Script texture;
    std::vector<std::string> textureChoices;
    std::vector<EntityPartRule> parts;
};

std::string rewriteSelector(const std::string& expression, const ControllerArrays& arrays, const std::string& kind, std::vector<std::string>& choices)
{
    auto isWord = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
    };
    std::string out;
    size_t at = 0;
    while (at < expression.size()) {
        char c = expression[at];
        if (c == '\'') {
            size_t close = expression.find('\'', at + 1);
            size_t end = close == std::string::npos ? expression.size() : close + 1;
            out += expression.substr(at, end - at);
            at = end;
            continue;
        }
        if (!std::isalpha(static_cast<unsigned char>(c)) && c != '_') {
            out += c;
            ++at;
            continue;
        }
        size_t start = at;
        while (at < expression.size() && isWord(expression[at])) {
            ++at;
        }
        std::string word = lowerName(expression.substr(start, at - start));
        if (startsWith(word, "array.")) {
            std::string index = "0";
            size_t next = at;
            while (next < expression.size() && std::isspace(static_cast<unsigned char>(expression[next]))) {
                ++next;
            }
            if (next < expression.size() && expression[next] == '[') {
                int depth = 0;
                size_t close = next;
                for (; close < expression.size(); ++close) {
                    if (expression[close] == '[') {
                        ++depth;
                    } else if (expression[close] == ']' && --depth == 0) {
                        break;
                    }
                }
                index = rewriteSelector(expression.substr(next + 1, close - next - 1), arrays, kind, choices);
                at = std::min(close + 1, expression.size());
            }
            auto found = arrays.find(word);
            if (found == arrays.end() || found->second.empty()) {
                out += "0";
                continue;
            }
            std::string base = std::to_string(choices.size());
            std::string count = std::to_string(found->second.size());
            choices.insert(choices.end(), found->second.begin(), found->second.end());
            out += "(" + base + " + math.mod(math.mod(math.floor(" + index + "), " + count + ") + " + count + ", " + count + "))";
            continue;
        }
        if (startsWith(word, kind + ".")) {
            auto found = std::find(choices.begin(), choices.end(), word);
            size_t index = static_cast<size_t>(found - choices.begin());
            if (found == choices.end()) {
                choices.push_back(word);
            }
            out += std::to_string(index);
            continue;
        }
        out += expression.substr(start, at - start);
    }
    return out;
}

ControllerArrays readControllerArrays(const json::Value* table)
{
    ControllerArrays out;
    if (!table) {
        return out;
    }
    for (const std::string& key : table->mKeys) {
        const json::Value& list = *table->mObject.at(key);
        if (!list.isArray()) {
            continue;
        }
        std::vector<std::string>& entries = out[lowerName(key)];
        for (const std::unique_ptr<json::Value>& entry : list.mArray) {
            if (entry->isString()) {
                entries.push_back(lowerName(entry->mString));
            }
        }
    }
    return out;
}

void readRenderControllers(const std::string& text, std::unordered_map<std::string, RenderControllerSource>& out)
{
    std::unique_ptr<json::Value> document = json::parse(stripComments(text));
    const json::Value* controllers = document ? document->get("render_controllers") : nullptr;
    if (!controllers) {
        return;
    }
    for (const std::string& name : controllers->mKeys) {
        const json::Value& controller = *controllers->mObject.at(name);
        const json::Value* arrays = controller.get("arrays");
        ControllerArrays textureArrays = readControllerArrays(arrays ? arrays->get("textures") : nullptr);
        ControllerArrays geometryArrays = readControllerArrays(arrays ? arrays->get("geometries") : nullptr);
        RenderControllerSource parsed;
        std::string geometry = "Geometry.default";
        if (const json::Value* value = controller.get("geometry"); value && value->isString()) {
            geometry = value->mString;
        }
        parsed.geometry = molang::Script::compile(rewriteSelector(geometry, geometryArrays, "geometry", parsed.geometryChoices));
        std::string texture = "Texture.default";
        if (const json::Value* list = controller.get("textures"); list && list->isArray() && !list->mArray.empty() && list->mArray.front()->isString()) {
            texture = list->mArray.front()->mString;
        }
        parsed.texture = molang::Script::compile(rewriteSelector(texture, textureArrays, "texture", parsed.textureChoices));
        if (const json::Value* visibility = controller.get("part_visibility"); visibility && visibility->isArray()) {
            for (const std::unique_ptr<json::Value>& entry : visibility->mArray) {
                for (const std::string& pattern : entry->mKeys) {
                    const json::Value& value = *entry->mObject.at(pattern);
                    EntityPartRule rule;
                    rule.pattern = lowerName(pattern);
                    if (value.mType == json::Value::Type::Boolean) {
                        rule.visible = molang::Script(value.mBoolean ? 1.0 : 0.0);
                    } else if (value.isString()) {
                        rule.visible = molang::Script::compile(value.mString, 1.0);
                    } else {
                        rule.visible = molang::Script(1.0);
                    }
                    parsed.parts.push_back(std::move(rule));
                }
            }
        }
        out[lowerName(name)] = std::move(parsed);
    }
}

}

std::shared_ptr<const EntityRig> buildSkinRig(const std::string& geometryData, const std::string& resourcePatch)
{
    if (geometryData.empty()) {
        return nullptr;
    }
    std::string name;
    if (std::unique_ptr<json::Value> patch = json::parse(stripComments(resourcePatch))) {
        const json::Value* geometry = patch->get("geometry");
        const json::Value* fallback = geometry ? geometry->get("default") : nullptr;
        if (fallback && fallback->isString()) {
            name = fallback->mString;
        }
    }
    GeometryLibrary library;
    library.parse(stripComments(geometryData));
    library.resolveInheritance();
    const Geometry* geometry = name.empty() ? nullptr : library.find(name);
    if (!geometry) {
        geometry = library.first();
    }
    if (!geometry) {
        return nullptr;
    }
    auto rig = std::make_shared<EntityRig>();
    buildEntityRig(*geometry, *rig);
    if (rig->quads.empty()) {
        return nullptr;
    }
    return rig;
}

/**
 * Entity models from the client entity definitions of the vanilla pack and
 * the server packs: each definition's default geometry as a bone rig, its
 * default texture scaled into one entity texture layer and its scripts, plus
 * one rig per geometry and one layer per texture its render controllers can
 * select, and every animation and animation controller. The player also gets
 * the slim humanoid model for skins that ask for it.
 */
void BlockAssets::buildEntityModels(PackSource& pack, const std::vector<std::shared_ptr<const PackFiles>>& packs)
{
    GeometryLibrary library;
    std::map<std::string, ClientEntity> definitions;
    std::unordered_map<std::string, RenderControllerSource> controllerSources;
    auto parseAnimations = [&](const std::string& text) {
        if (std::unique_ptr<json::Value> document = json::parse(stripComments(text))) {
            animations.parse(*document);
        }
    };
    for (const char* archive : { "models", "models/entity" }) {
        for (const std::string& name : pack.archiveEntries(archive)) {
            std::string text;
            if (pack.readArchived(archive, name, text)) {
                library.parse(stripComments(text));
            }
        }
    }
    for (const std::string& name : pack.archiveEntries("entity")) {
        std::string text;
        if (pack.readArchived("entity", name, text)) {
            readClientEntity(text, definitions);
        }
    }
    for (const std::string& name : pack.archiveEntries("render_controllers")) {
        std::string text;
        if (pack.readArchived("render_controllers", name, text)) {
            readRenderControllers(text, controllerSources);
        }
    }
    for (const char* archive : { "animations", "animation_controllers" }) {
        for (const std::string& name : pack.archiveEntries(archive)) {
            std::string text;
            if (pack.readArchived(archive, name, text)) {
                parseAnimations(text);
            }
        }
    }
    for (auto layer = packs.rbegin(); layer != packs.rend(); ++layer) {
        for (const auto& [path, content] : (*layer)->files) {
            if (!endsWith(path, ".json")) {
                continue;
            }
            if (startsWith(path, "models/")) {
                library.parse(stripComments(content));
            } else if (startsWith(path, "entity/")) {
                readClientEntity(content, definitions);
            } else if (startsWith(path, "animations/") || startsWith(path, "animation_controllers/")) {
                parseAnimations(content);
            } else if (startsWith(path, "render_controllers/")) {
                readRenderControllers(content, controllerSources);
            }
        }
    }

    library.resolveInheritance();

    std::map<std::string, uint32_t> layerByTexture;
    auto textureLayer = [&](const std::string& path) -> std::optional<uint32_t> {
        auto found = layerByTexture.find(path);
        if (found != layerByTexture.end()) {
            return found->second;
        }
        std::string encoded;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;
        if (!pack.readTexture(path, encoded) || !ui::decodeImage(encoded, width, height, rgba) || width == 0 || height == 0) {
            return std::nullopt;
        }
        uint32_t layer = entityTextureLayers();
        std::vector<uint8_t> resized = resizeNearest(rgba, width, height, EntityTextureSize);
        entityPixels.insert(entityPixels.end(), resized.begin(), resized.end());
        layerByTexture.emplace(path, layer);
        return layer;
    };
    auto modelOf = [&](const std::string& geometryName, const ClientEntity& definition, uint32_t layer) -> std::optional<EntityModel> {
        const Geometry* geometry = library.find(geometryName);
        if (!geometry) {
            return std::nullopt;
        }
        EntityModel model;
        model.rigs.emplace_back();
        buildEntityRig(*geometry, model.rigs.back());
        model.scripts = definition.scripts;
        model.layer = layer;
        std::map<std::string, uint32_t> rigByGeometry { { geometryName, 0 } };
        auto rigOf = [&](const std::string& choice) -> uint32_t {
            std::string key = choice.substr(std::string("geometry.").size());
            auto named = definition.geometries.find(key);
            if (named == definition.geometries.end()) {
                return NoEntityChoice;
            }
            const std::string& id = key == "default" ? geometryName : named->second;
            if (auto known = rigByGeometry.find(id); known != rigByGeometry.end()) {
                return known->second;
            }
            const Geometry* found = library.find(id);
            if (!found) {
                return NoEntityChoice;
            }
            uint32_t index = static_cast<uint32_t>(model.rigs.size());
            model.rigs.emplace_back();
            buildEntityRig(*found, model.rigs.back());
            rigByGeometry.emplace(id, index);
            return index;
        };
        auto layerOf = [&](const std::string& choice) -> uint32_t {
            auto named = definition.textures.find(choice.substr(std::string("texture.").size()));
            if (named == definition.textures.end()) {
                return NoEntityChoice;
            }
            std::optional<uint32_t> found = textureLayer(named->second);
            return found ? *found : NoEntityChoice;
        };
        for (const auto& [name, condition] : definition.renderControllers) {
            auto source = controllerSources.find(name);
            if (source == controllerSources.end()) {
                continue;
            }
            EntityRenderController controller;
            if (!condition.empty()) {
                controller.condition = molang::Script::compile(condition, 1.0);
            }
            controller.geometry = source->second.geometry;
            controller.texture = source->second.texture;
            controller.parts = source->second.parts;
            for (const std::string& choice : source->second.geometryChoices) {
                controller.geometryChoices.push_back(rigOf(choice));
            }
            for (const std::string& choice : source->second.textureChoices) {
                controller.textureChoices.push_back(layerOf(choice));
            }
            model.controllers.push_back(std::move(controller));
        }
        return model;
    };

    for (const auto& [identifier, definition] : definitions) {
        std::optional<uint32_t> layer = textureLayer(definition.texture);
        if (!layer) {
            continue;
        }
        if (std::optional<EntityModel> model = modelOf(definition.geometry, definition, *layer)) {
            entityModels.emplace(identifier, std::move(*model));
        }
        if (identifier == "minecraft:player") {
            if (std::optional<EntityModel> slim = modelOf("geometry.humanoid.customSlim", definition, *layer)) {
                entityModels.emplace(identifier + "#slim", std::move(*slim));
            }
        }
    }
}

/**
 * Cuts every face of the chest, bed, skull and banner models out of their
 * entity textures into block texture layers and bakes one model per look and
 * rotation. Blocks whose texture is missing fall back to the diagnostic cube.
 */
void BlockAssets::buildBlockEntityTemplates(PackSource& pack, std::vector<std::vector<uint8_t>>& layers, std::vector<bool>& overlayLayers, std::map<std::string, uint32_t>& materialByKey,
    const std::function<uint32_t(const std::vector<ModelQuad>&, uint32_t)>& pushTemplate)
{
    std::map<std::string, EntityImage> images;
    std::unordered_map<std::string, uint32_t> materialByPixels;
    auto image = [&](const std::string& path) -> const EntityImage* {
        auto found = images.find(path);
        if (found == images.end()) {
            EntityImage loaded;
            std::string encoded;
            if (pack.readTexture(path, encoded)) {
                ui::decodeImage(encoded, loaded.width, loaded.height, loaded.rgba);
            }
            found = images.emplace(path, std::move(loaded)).first;
        }
        return found->second.width ? &found->second : nullptr;
    };
    auto build = [&](const std::string& path, const std::vector<EntityBox>& boxes, float yaw, uint32_t tint) -> uint32_t {
        const EntityImage* source = image(path);
        if (!source) {
            return NoModelTemplate;
        }
        std::vector<ModelQuad> modelQuads = buildEntityQuads(boxes, yaw, [&](const EntityFace& face) {
            std::string key = "entity|" + path + '|' + std::to_string(face.u) + ',' + std::to_string(face.v) + ',' + std::to_string(face.w) + ',' + std::to_string(face.h) + '|'
                + std::to_string(face.turns) + std::to_string(face.flipU) + std::to_string(face.flipV) + '|' + std::to_string(face.tinted ? tint : 0);
            auto found = materialByKey.find(key);
            if (found != materialByKey.end()) {
                return found->second;
            }
            std::vector<uint8_t> pixels = sliceEntityFace(*source, face, tint);
            std::string content(pixels.begin(), pixels.end());
            auto same = materialByPixels.find(content);
            if (same != materialByPixels.end()) {
                materialByKey.emplace(key, same->second);
                return same->second;
            }
            if (layers.size() >= MaxTextureLayers) {
                return DiagnosticMaterial;
            }
            Material material;
            material.layer = static_cast<uint32_t>(layers.size());
            layers.push_back(std::move(pixels));
            overlayLayers.resize(layers.size(), false);
            uint32_t id = static_cast<uint32_t>(materialTable.size());
            materialTable.push_back(material);
            materialByKey.emplace(key, id);
            materialByPixels.emplace(std::move(content), id);
            return id;
        });
        return pushTemplate(modelQuads, 0);
    };

    static constexpr const char* ChestTextures[ChestKinds] = { "normal", "trapped", "ender" };
    static constexpr const char* DoubleChestTextures[ChestKinds] = { "double_normal", "trapped_double", "ender" };
    static constexpr const char* SkullTextures[SkullKinds] = { "textures/entity/skulls/skeleton", "textures/entity/skulls/wither_skeleton", "textures/entity/skulls/zombie", "textures/entity/skulls/creeper", "textures/entity/steve" };
    for (size_t kind = 0; kind < ChestKinds; ++kind) {
        for (uint32_t rotation = 0; rotation < 4; ++rotation) {
            float yaw = 90.0f * float(rotation);
            entityTemplates.chest[kind][rotation] = build(std::string("textures/entity/chest/") + ChestTextures[kind], chestBoxes(false, 0.0f), yaw, 0);
            std::string doublePath = std::string("textures/entity/chest/") + DoubleChestTextures[kind];
            bool doubleTexture = kind != 2;
            entityTemplates.doubleChest[kind][0][rotation] = doubleTexture ? build(doublePath, chestBoxes(true, 0.0f), yaw, 0) : entityTemplates.chest[kind][rotation];
            entityTemplates.doubleChest[kind][1][rotation] = doubleTexture ? build(doublePath, chestBoxes(true, -16.0f), yaw, 0) : entityTemplates.chest[kind][rotation];
        }
    }
    for (size_t color = 0; color < DyeColors; ++color) {
        for (uint32_t rotation = 0; rotation < 4; ++rotation) {
            float yaw = 90.0f * float(rotation);
            for (size_t half = 0; half < 2; ++half) {
                entityTemplates.bed[color][half][rotation] = build(std::string("textures/entity/bed/") + BedColors[color], bedBoxes(half == 1), yaw, 0);
            }
            entityTemplates.wallBanner[color][rotation] = build("textures/entity/banner/banner", bannerBoxes(true), yaw, BannerColors[color]);
        }
        for (uint32_t step = 0; step < FineRotations; ++step) {
            entityTemplates.standingBanner[color][step] = build("textures/entity/banner/banner", bannerBoxes(false), 22.5f * float(step), BannerColors[color]);
        }
    }
    for (size_t kind = 0; kind < SkullKinds; ++kind) {
        for (uint32_t step = 0; step < FineRotations; ++step) {
            entityTemplates.floorSkull[kind][step] = build(SkullTextures[kind], skullBoxes(false), 22.5f * float(step), 0);
        }
        for (uint32_t rotation = 0; rotation < 4; ++rotation) {
            entityTemplates.wallSkull[kind][rotation] = build(SkullTextures[kind], skullBoxes(true), 90.0f * float(rotation), 0);
        }
    }

    for (BlockVisual& visual : visuals) {
        if (visual.blockEntity == EntityNone) {
            continue;
        }
        visual.modelTemplate = blockEntityTemplate(visual, nullptr, {});
        if (visual.modelTemplate == NoModelTemplate) {
            visual.blockEntity = EntityNone;
            visual.flags = FlagDiagnostic;
            ++diagnosticCount;
        }
    }
}

/**
 * The model of a block drawn from its block entity. A double chest is drawn
 * whole by its lead half, so the other half returns no model.
 */
uint32_t BlockAssets::blockEntityTemplate(const BlockVisual& visual, const Tag* data, const std::array<int32_t, 3>& position) const
{
    uint32_t rotation = visual.variant & 3;
    switch (visual.blockEntity) {
    case EntityChest:
    case EntityTrappedChest:
    case EntityEnderChest: {
        size_t kind = size_t(visual.blockEntity - EntityChest);
        if (visual.blockEntity != EntityEnderChest && data && data->get("pairx") && data->get("pairz")) {
            if (!entityInt(data, "pairlead", 0)) {
                return NoModelTemplate;
            }
            int32_t dx = entityInt(data, "pairx", 0) - position[0];
            int32_t dz = entityInt(data, "pairz", 0) - position[2];
            for (uint32_t step = 0; step < rotation; ++step) {
                int32_t turned = dz;
                dz = -dx;
                dx = turned;
            }
            if (dz == 0 && (dx == 1 || dx == -1)) {
                return entityTemplates.doubleChest[kind][dx == 1 ? 0 : 1][rotation];
            }
        }
        return entityTemplates.chest[kind][rotation];
    }
    case EntityBed: {
        uint32_t color = uint32_t(std::clamp(entityInt(data, "color", DefaultBedColor), 0, int32_t(DyeColors) - 1));
        return entityTemplates.bed[color][(visual.variant >> 2) & 1][rotation];
    }
    case EntityStandingBanner:
    case EntityWallBanner: {
        uint32_t color = uint32_t(std::clamp(entityInt(data, "Base", DefaultBannerColor), 0, int32_t(DyeColors) - 1));
        return visual.blockEntity == EntityWallBanner ? entityTemplates.wallBanner[color][rotation] : entityTemplates.standingBanner[color][visual.variant & 15];
    }
    case EntityFloorSkull: {
        uint32_t step = uint32_t(std::lround(entityFloat(data, "Rotation") / 22.5f)) & 15;
        return entityTemplates.floorSkull[visual.variant & 15][step];
    }
    case EntityWallSkull:
        return entityTemplates.wallSkull[visual.variant & 15][(visual.variant >> 4) & 3];
    default:
        return NoModelTemplate;
    }
}

namespace {

std::vector<std::string> itemTexturePaths(const json::Value& definition)
{
    std::vector<std::string> paths;
    const json::Value* textures = definition.get("textures");
    if (!textures) {
        return paths;
    }
    auto pathOf = [](const json::Value& value) -> std::string {
        if (value.isString()) {
            return value.mString;
        }
        const json::Value* path = value.isObject() ? value.get("path") : nullptr;
        return path && path->isString() ? path->mString : std::string();
    };
    if (textures->isArray()) {
        for (const auto& entry : textures->mArray) {
            paths.push_back(pathOf(*entry));
        }
    } else {
        paths.push_back(pathOf(*textures));
    }
    return paths;
}

/**
 * Draws a block as an inventory icon: its top, south and east faces as an
 * isometric cube, shaded brighter on top and darker on the right.
 */
std::vector<uint8_t> isometricIcon(const std::array<const uint8_t*, 3>& faces, const std::array<std::array<uint8_t, 3>, 3>& tints)
{
    constexpr float Size = static_cast<float>(ItemIconSize);
    struct Face {
        std::array<float, 2> origin;
        std::array<float, 2> axisU;
        std::array<float, 2> axisV;
        float shade;
    };
    const float h = Size * 0.5f;
    const float q = Size * 0.25f;
    const std::array<Face, 3> projected { {
        { { 0.0f, q }, { h, -q }, { h, q }, 1.0f },
        { { 0.0f, q }, { h, q }, { 0.0f, h }, 0.8f },
        { { h, h }, { h, -q }, { 0.0f, h }, 0.62f },
    } };
    std::vector<uint8_t> out(size_t(ItemIconSize) * ItemIconSize * 4, 0);
    for (size_t face = 0; face < 3; ++face) {
        if (!faces[face]) {
            continue;
        }
        const Face& f = projected[face];
        float determinant = f.axisU[0] * f.axisV[1] - f.axisU[1] * f.axisV[0];
        if (std::abs(determinant) < 1.0e-6f) {
            continue;
        }
        for (uint32_t y = 0; y < ItemIconSize; ++y) {
            for (uint32_t x = 0; x < ItemIconSize; ++x) {
                float px = x + 0.5f - f.origin[0];
                float py = y + 0.5f - f.origin[1];
                float u = (px * f.axisV[1] - py * f.axisV[0]) / determinant;
                float v = (f.axisU[0] * py - f.axisU[1] * px) / determinant;
                if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) {
                    continue;
                }
                uint32_t tu = std::min(uint32_t(u * TextureSize), TextureSize - 1);
                uint32_t tv = std::min(uint32_t(v * TextureSize), TextureSize - 1);
                const uint8_t* texel = faces[face] + (size_t(tv) * TextureSize + tu) * 4;
                if (texel[3] < 128) {
                    continue;
                }
                uint8_t* pixel = out.data() + (size_t(y) * ItemIconSize + x) * 4;
                for (int channel = 0; channel < 3; ++channel) {
                    pixel[channel] = static_cast<uint8_t>(std::clamp(texel[channel] * f.shade * tints[face][channel] / 255.0f, 0.0f, 255.0f));
                }
                pixel[3] = 255;
            }
        }
    }
    return out;
}

}

/**
 * Loads every item texture named by item_texture.json or found under
 * textures/items, scaled to the icon size, and indexes the default state of
 * every block by name for block item icons.
 */
void BlockAssets::buildInterfaceAssets(PackSource& pack)
{
    struct Decoded {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;
    };
    std::map<std::string, Decoded> decoded;
    auto load = [&](const std::string& path, uint32_t& width, uint32_t& height) -> const std::vector<uint8_t>* {
        auto found = decoded.find(path);
        if (found == decoded.end()) {
            Decoded image;
            std::string encoded;
            if (!pack.readTexture(path, encoded) || !ui::decodeImage(encoded, image.width, image.height, image.rgba) || image.width == 0 || image.height == 0) {
                image = Decoded {};
            }
            found = decoded.emplace(path, std::move(image)).first;
        }
        width = found->second.width;
        height = found->second.height;
        return found->second.rgba.empty() ? nullptr : &found->second.rgba;
    };
    std::vector<std::string> atlases = pack.readTextLayers("textures/item_texture.json");
    if (std::string archived; pack.readArchived("textures", "item_texture.json", archived)) {
        atlases.push_back(std::move(archived));
    }
    for (const std::string& name : pack.archiveEntries("textures/items")) {
        size_t dot = name.rfind('.');
        std::string stem = name.substr(0, dot);
        if (dot == std::string::npos || name.find('/') != std::string::npos || itemFiles.count(stem)) {
            continue;
        }
        uint32_t width = 0;
        uint32_t height = 0;
        const std::vector<uint8_t>* rgba = load("textures/items/" + stem, width, height);
        if (rgba && width == height) {
            itemFiles.emplace(stem, resizeNearest(*rgba, width, height, ItemIconSize));
        }
    }
    for (const std::string& text : atlases) {
        std::unique_ptr<json::Value> parsed = json::parse(stripComments(text));
        const json::Value* data = parsed ? parsed->get("texture_data") : nullptr;
        if (!data || !data->isObject()) {
            continue;
        }
        for (const std::string& alias : data->mKeys) {
            std::string identifier = alias.find(':') == std::string::npos ? "minecraft:" + alias : alias;
            if (itemTextures.count(identifier)) {
                continue;
            }
            std::vector<std::vector<uint8_t>> variants;
            for (const std::string& path : itemTexturePaths(*data->get(alias))) {
                uint32_t width = 0;
                uint32_t height = 0;
                const std::vector<uint8_t>* rgba = path.empty() ? nullptr : load(path, width, height);
                if (!rgba || width != height) {
                    variants.emplace_back();
                    continue;
                }
                variants.push_back(resizeNearest(*rgba, width, height, ItemIconSize));
            }
            itemTextures.emplace(identifier, std::move(variants));
        }
    }

    for (const BlockRecord& record : registry.records()) {
        blockByName.try_emplace(record.name, record.networkHash);
    }
}

/**
 * The inventory icon of an item: its item texture for the aux variant, or for
 * block items an isometric cube of the block's default state. Empty when the
 * item has neither.
 */
std::vector<uint8_t> BlockAssets::itemIcon(const std::string& identifier, int32_t aux, const std::string& iconHint) const
{
    static const std::pair<const char*, const char*> Renames[] = {
        { "totem_of_undying", "totem" },
        { "compass", "compass_item" },
        { "recovery_compass", "recovery_compass_item" },
        { "clock", "clock_item" },
        { "golden_apple", "apple_golden" },
        { "enchanted_golden_apple", "apple_golden" },
        { "writable_book", "book_writable" },
        { "written_book", "book_written" },
        { "enchanted_book", "book_enchanted" },
        { "filled_map", "map_filled" },
        { "empty_map", "map_empty" },
        { "glass_bottle", "potion_bottle_empty" },
        { "potion", "potion_bottle_drinkable" },
        { "splash_potion", "potion_bottle_splash" },
        { "lingering_potion", "potion_bottle_lingering" },
        { "fire_charge", "fireball" },
        { "glistering_melon_slice", "melon_speckled" },
        { "melon_slice", "melon" },
        { "cooked_beef", "beef_cooked" },
        { "cooked_chicken", "chicken_cooked" },
        { "cooked_porkchop", "porkchop_cooked" },
        { "cooked_mutton", "mutton_cooked" },
        { "cooked_rabbit", "rabbit_cooked" },
        { "cooked_cod", "fish_cooked" },
        { "cooked_salmon", "fish_salmon_cooked" },
        { "cod", "fish_raw" },
        { "salmon", "fish_salmon_raw" },
        { "tropical_fish", "fish_clownfish_raw" },
        { "pufferfish", "fish_pufferfish_raw" },
        { "porkchop", "porkchop_raw" },
        { "beef", "beef_raw" },
        { "chicken", "chicken_raw" },
        { "mutton", "mutton_raw" },
        { "rabbit", "rabbit_raw" },
        { "experience_bottle", "experience_bottle" },
        { "snowball", "snowball" },
        { "ender_eye", "ender_eye" },
        { "nether_star", "nether_star" },
        { "gunpowder", "gunpowder" },
        { "wheat_seeds", "seeds_wheat" },
        { "pumpkin_seeds", "seeds_pumpkin" },
        { "melon_seeds", "seeds_melon" },
        { "beetroot_seeds", "seeds_beetroot" },
        { "firework_rocket", "fireworks" },
        { "firework_star", "fireworks_charge" },
        { "chest_minecart", "minecart_chest" },
        { "hopper_minecart", "minecart_hopper" },
        { "tnt_minecart", "minecart_tnt" },
        { "command_block_minecart", "minecart_command_block" },
        { "carrot_on_a_stick", "carrot_on_a_stick" },
        { "spider_eye", "spider_eye" },
        { "fermented_spider_eye", "spider_eye_fermented" },
        { "golden_carrot", "carrot_golden" },
        { "turtle_scute", "turtle_shell_piece" },
        { "rabbit_foot", "rabbit_foot" },
    };
    std::string shortName = identifier.substr(identifier.find(':') == std::string::npos ? 0 : identifier.find(':') + 1);
    std::vector<std::string> names;
    if (!iconHint.empty()) {
        names.push_back(iconHint);
    }
    names.push_back(shortName);
    for (const auto& [from, to] : Renames) {
        if (shortName == from) {
            names.push_back(to);
        }
    }
    for (const auto& [from, to] : { std::pair<const char*, const char*> { "wooden_", "wood_" }, { "golden_", "gold_" } }) {
        size_t length = std::char_traits<char>::length(from);
        if (shortName.rfind(from, 0) == 0) {
            names.push_back(to + shortName.substr(length));
        }
    }
    for (const std::string& name : names) {
        auto found = itemTextures.find(name.find(':') == std::string::npos ? "minecraft:" + name : name);
        if (found == itemTextures.end() || found->second.empty()) {
            continue;
        }
        size_t variant = aux >= 0 && size_t(aux) < found->second.size() ? size_t(aux) : 0;
        if (!found->second[variant].empty()) {
            return found->second[variant];
        }
        if (!found->second.front().empty()) {
            return found->second.front();
        }
    }
    for (const std::string& name : names) {
        if (auto file = itemFiles.find(name); file != itemFiles.end()) {
            return file->second;
        }
    }
    auto block = blockByName.find(identifier);
    if (block == blockByName.end()) {
        return {};
    }
    const BlockVisual& look = visual(block->second, true);
    const std::vector<uint8_t>& texels = textureArray.mips[0];
    size_t layerBytes = size_t(TextureSize) * TextureSize * 4;
    auto facePixels = [&](size_t side, std::array<uint8_t, 3>& tint) -> const uint8_t* {
        tint = { 255, 255, 255 };
        uint32_t material = look.faces[side];
        if (material >= materialTable.size()) {
            return nullptr;
        }
        const Material& entry = materialTable[material];
        if (entry.tint & TintKindMask) {
            tint = { 124, 189, 107 };
        }
        size_t offset = size_t(entry.layer) * layerBytes;
        return offset + layerBytes <= texels.size() ? texels.data() + offset : nullptr;
    };
    std::array<std::array<uint8_t, 3>, 3> tints {};
    if (!look.emitsCubeGeometry() || (look.flags & FlagDiagnostic)) {
        const uint8_t* flat = facePixels(5, tints[0]);
        if (!flat) {
            return {};
        }
        std::vector<uint8_t> icon(size_t(ItemIconSize) * ItemIconSize * 4);
        for (uint32_t y = 0; y < ItemIconSize; ++y) {
            for (uint32_t x = 0; x < ItemIconSize; ++x) {
                const uint8_t* texel = flat + (size_t(y * TextureSize / ItemIconSize) * TextureSize + x * TextureSize / ItemIconSize) * 4;
                uint8_t* pixel = icon.data() + (size_t(y) * ItemIconSize + x) * 4;
                for (int channel = 0; channel < 3; ++channel) {
                    pixel[channel] = static_cast<uint8_t>(texel[channel] * tints[0][channel] / 255);
                }
                pixel[3] = texel[3];
            }
        }
        return icon;
    }
    std::array<const uint8_t*, 3> faces { facePixels(3, tints[0]), facePixels(5, tints[1]), facePixels(1, tints[2]) };
    return isometricIcon(faces, tints);
}

}
