#pragma once

#include "Core/Json/Json.h"
#include "Core/NBT/Tag.h"
#include "world/BlockAssets.h"
#include "world/BlockRegistry.h"

#include <array>
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
    Honey,
    Piston,
    Azalea,
    CandleCake,
    SculkSensor,
    SculkShrieker,
    SeaPickle,
    Chorus,
    Dripleaf,
    SmallDripleaf,
    CoralFan,
    SporeBlossom,
    Sunflower,
    CropStem,
    PitcherCrop,
    DriedGhast,
    RedstoneWire,
    Tripwire,
    TripwireHook,
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
    Lantern,
    Candle,
    TurtleEgg,
    Cauldron,
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
    Chain,
    Shape,
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
bool isLanternName(const std::string& name);
bool isCandleName(const std::string& name);
bool isCauldronName(const std::string& name);
bool isAquaticName(const std::string& name);
bool isCropName(const std::string& name);
bool isCrossName(const std::string& name);
bool isShelfName(const std::string& name);

/**
 * Blocks whose surface is the end starfield rather than a texture: the end
 * portal and the end gateway.
 */
bool isEndPortalName(const std::string& name);

/**
 * Blocks whose look comes from a block entity model with an entity texture:
 * chests, beds, banners, shulker boxes and skulls.
 */
bool isDeferredName(const std::string& name);

/**
 * One box of a hand built block shape in block pixels (0..16). Its faces take
 * the block's own face textures, or all of them the one of side, or all of
 * them the terrain texture named by texture.
 */
struct ShapeBox {
    std::array<int16_t, 3> min {};
    std::array<int16_t, 3> max {};
    int side = -1;
    const char* texture = nullptr;
    // Which of the block's side textures each face takes, in West, East, Down, Up, North,
    // South order, for boxes mixing them; -1 falls back to side.
    std::array<int8_t, 6> faceSides { -1, -1, -1, -1, -1, -1 };
    // Pixel rects per face when the box uses part of a texture, like an end rod's rod.
    std::optional<std::array<std::array<uint16_t, 4>, 6>> uvs;
    uint8_t hidden = 0;
    uint16_t uvSize = 16;
    uint16_t textureVariant = 0;
    std::array<int16_t, 3> offset {};
};

/**
 * The game draws some blocks with geometry of its own that no pack carries.
 * This is their look built from boxes facing south, turned by quarter turns
 * like signs, with an optional cross of the given side's texture (a campfire's
 * flames) and an optional flat plane one pixel up (petals).
 */
struct BlockShape {
    std::vector<ShapeBox> boxes;
    uint32_t turns = 0;
    // The side a model built pointing up is turned to point at, after its turns.
    int facing = -1;
    int crossSide = -1;
    int planeSide = -1;
    int16_t planeHeight = 16;
};

bool isShapeName(const std::string& name);
BlockShape blockShape(const std::string& name, const Tag& states);
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
