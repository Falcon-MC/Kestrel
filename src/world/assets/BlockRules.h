#pragma once

#include "Core/Json/Json.h"
#include "Core/NBT/Tag.h"
#include "world/BlockAssets.h"
#include "world/BlockRegistry.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kestrel::world::rules {

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

struct Flipbook {
    std::string texturePath;
    std::string atlasTile;
    int32_t atlasIndex = -1;
    uint32_t ticksPerFrame = 1;
    std::vector<uint32_t> frames;
    bool blendFrames = true;
};

bool isTorchName(const std::string& name);
bool isAquaticName(const std::string& name);
bool isCropName(const std::string& name);
bool isCrossName(const std::string& name);
bool isShelfName(const std::string& name);

/**
 * Blocks whose look comes from their block entity: chests, beds, banners,
 * brewing stands, campfires, copper golem statues, decorated pots, enchanting
 * tables, item frames, hoppers, lecterns and skulls.
 */
bool isDeferredName(const std::string& name);
Family classify(const std::string& name);
ModelKind modelKind(const std::string& name);
const char* legacyAlias(const std::string& name);
uint8_t blockTint(const std::string& name, int face);
bool isTranslucentName(const std::string& name);

/**
 * Quarter turns of a model facing south by default: south, west, north, east.
 */
uint8_t facingRotation(const std::string& cardinal);
uint8_t facingDirectionRotation(int32_t facing);

/**
 * Blocks drawn from their block entity: the kind, and in variant the state
 * part of their look (rotation, bed half, skull type).
 */
bool classifyBlockEntity(const std::string& name, const Tag& states, BlockVisual& visual);

std::string stateString(const Tag& states, const std::string& key);
std::optional<int32_t> stateInt(const Tag& states, const std::string& key);
Axis stateAxis(const Tag& states);
std::optional<Face> stateFacing(const Tag& states);
bool stateMatches(const Tag& states, const json::Value& expected);
float tagNumber(const Tag* value, float fallback = 0.0f);
void collectComponents(const Tag* components, std::map<std::string, const Tag*>& out);

/**
 * Light emission and filter of every vanilla state from the measured block
 * light table: a per-block default, then the first override whose states all
 * match. Blocks the table does not list filter fully when they occlude.
 */
void applyBlockLight(const BlockRegistry& registry, std::vector<BlockVisual>& visuals);

/**
 * Every state of a server-declared block in client palette order: the explicit
 * properties first, then the properties added by its traits, with the last
 * property varying fastest.
 */
std::vector<Tag> enumerateCustomStates(const Tag& definition);

std::pair<Face, bool> orientFace(Face face, Axis axis);
const char* faceKey(Face face);
bool isHorizontal(Face face);
std::optional<Face> faceFromName(const std::string& value);
Face opposite(Face face);
int horizontalIndex(Face face);
Face horizontalFace(int index);
std::string explicitFaceKey(const json::Value& textures, Face face);
std::optional<Face> frontFace(const json::Value& textures);
Face sourceForFacing(Face world, Face reference, Face facing);
std::string resolveTextureKey(const json::Value* textures, Face face, Axis axis, std::optional<Face> facing, bool& rotate);
size_t terrainVariantCount(const json::Value& entry);
std::string terrainPath(const json::Value& entry, size_t index);
std::optional<Flipbook> parseFlipbook(const json::Value& item);

}
