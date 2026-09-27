#include "BlockRules.h"

#include "util/Text.h"

#include <algorithm>
#include <iterator>

namespace kestrel::world::rules {

using util::contains;
using util::endsWith;
using util::startsWith;

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

bool isDeferredName(const std::string& name)
{
    return name == "chest" || name == "trapped_chest" || name == "ender_chest" || endsWith(name, "copper_chest") || name == "bed"
        || endsWith(name, "_bed") || name == "standing_banner" || name == "wall_banner" || contains(name, "shulker_box")
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
    if (name == "barrier" || name == "structure_void" || startsWith(name, "light_block") || name == "invisible_bedrock" || name == "moving_block"
        || endsWith(name, "piston_arm_collision") || name == "client_request_placeholder_block") {
        return Family::Invisible;
    }
    if (name == "bone_block" || name == "hay_block" || name == "chiseled_quartz_block" || name == "purpur_block" || name == "quartz_block" || name == "smooth_quartz" || name == "tnt" || endsWith(name, "_glazed_terracotta")) {
        return Family::Cube;
    }
    if (isShapeName(name) || name == "soul_sand" || name == "mud"
        || contains(name, "trapdoor") || endsWith(name, "_door") || name == "wooden_door" || endsWith(name, "_stairs")
        || contains(name, "slab") || contains(name, "fence_gate") || endsWith(name, "_wall") || name == "cobblestone_wall"
        || endsWith(name, "_fence") || name == "fence" || name == "nether_brick_fence" || contains(name, "glass_pane")
        || endsWith(name, "_pane") || endsWith(name, "_bars") || endsWith(name, "_bed") || name == "bed"
        || contains(name, "chest") || contains(name, "sign") || contains(name, "rail") || isTorchName(name)
        || endsWith(name, "_button") || name == "stone_button" || contains(name, "pressure_plate")
        || endsWith(name, "_carpet") || name == "carpet" || name == "snow_layer"
        || isAquaticName(name) || isCropName(name) || name == "vine" || name == "glow_lichen" || name == "sculk_vein" || name == "resin_clump" || name == "cactus"
        || name == "cake" || name == "farmland" || isCrossName(name) || name == "ladder" || name == "waterlily" || name == "lily_pad" || name == "bamboo"
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

ModelKind modelKind(const std::string& name)
{
    if (isShapeName(name)) {
        return ModelKind::Shape;
    }
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
    if (name == "farmland" || name == "grass_path" || name == "dirt_path") {
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
        { "short_grass", "tallgrass" },
        { "fern", "tallgrass" },
        { "sunflower", "double_plant" },
        { "lilac", "double_plant" },
        { "tall_grass", "double_plant" },
        { "large_fern", "double_plant" },
        { "rose_bush", "double_plant" },
        { "peony", "double_plant" },
    };
    auto found = aliases.find(name);
    return found == aliases.end() ? nullptr : found->second;
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
    if (name == "short_grass" || name == "tall_grass" || name == "fern" || name == "large_fern" || name == "bush") {
        return Grass;
    }
    return 0;
}

bool isTranslucentName(const std::string& name)
{
    return contains(name, "stained_glass") || name == "water" || name == "flowing_water" || name == "ice" || name == "slime"
        || name == "honey_block" || name == "portal" || name == "tinted_glass";
}

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

bool classifyBlockEntity(const std::string& name, const Tag& states, BlockVisual& visual)
{
    static constexpr const char* Skulls[SkullKinds] = { "skeleton_skull", "wither_skeleton_skull", "zombie_head", "creeper_head", "player_head", "piglin_head", "dragon_head" };
    static constexpr const char* ShulkerColors[DyeColors] = {
        "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
        "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black",
    };
    if (name == "chest" || name == "trapped_chest" || name == "ender_chest" || endsWith(name, "copper_chest")) {
        std::string cardinal = stateString(states, "minecraft:cardinal_direction");
        visual.variant = cardinal.empty() ? facingDirectionRotation(stateInt(states, "facing_direction").value_or(3)) : facingRotation(cardinal);
        if (endsWith(name, "copper_chest")) {
            uint32_t oxidation = contains(name, "exposed") ? 1 : contains(name, "weathered") ? 2 : contains(name, "oxidized") ? 3 : 0;
            visual.blockEntity = EntityCopperChest;
            visual.variant |= oxidation << 2;
        } else {
            visual.blockEntity = name == "chest" ? EntityChest : name == "trapped_chest" ? EntityTrappedChest : EntityEnderChest;
        }
        return true;
    }
    if (contains(name, "shulker_box")) {
        visual.blockEntity = EntityShulkerBox;
        visual.variant = DyeColors;
        for (uint32_t color = 0; color < DyeColors; ++color) {
            if (name == std::string(ShulkerColors[color]) + "_shulker_box") {
                visual.variant = color;
            }
        }
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

}
