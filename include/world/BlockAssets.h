#pragma once

#include "world/BiomeTints.h"
#include "world/BlockRegistry.h"
#include "world/EntityAnimation.h"
#include "world/ServerPack.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kestrel::world {

class PackSource;

enum BlockFlag : uint8_t {
    FlagAir = 1 << 0,
    FlagCubeGeometry = 1 << 1,
    FlagOccludesFullFace = 1 << 2,
    FlagLeafModel = 1 << 3,
    FlagDiagnostic = 1 << 4,
    FlagModel = 1 << 5,
    FlagTranslucent = 1 << 6,
    FlagCullSame = 1 << 7,
};

inline constexpr uint32_t DiagnosticMaterial = 0;
inline constexpr uint32_t TextureSize = 16;
inline constexpr uint32_t TextureMipLevels = 5;
inline constexpr size_t MaxTextureLayers = 4096;
inline constexpr uint32_t NoModelTemplate = 0xFFFFFFFFu;
inline constexpr uint32_t DestroyStages = 10;

enum ModelTemplateFlag : uint32_t {
    TemplateStair = 1 << 0,
    TemplatePane = 1 << 1,
    TemplateFenceWood = 1 << 2,
    TemplateFenceNether = 1 << 3,
    TemplateWall = 1 << 4,
    TemplateGateAxisX = 1 << 5,
    TemplateGateAxisZ = 1 << 6,
};

enum ModelQuadFlag : uint32_t {
    QuadFaceMask = 0x7,
    QuadTwoSided = 1 << 3,
    QuadCullFaceMask = 0x70,
    QuadInward = 1 << 7,
};

/**
 * One model quad in block space: positions in 1/256 block, UVs in 1/4096 texture.
 * Flags hold the face id (1 down, 2 up, 3 west, 4 east, 5 north, 6 south) and the
 * boundary face it touches, which the mesher culls against full neighbours.
 */
struct ModelQuad {
    std::array<std::array<int16_t, 3>, 4> positions {};
    std::array<std::array<uint16_t, 2>, 4> uvs {};
    uint32_t material = 0;
    uint32_t flags = 0;
};

struct ModelTemplate {
    uint32_t quadStart = 0;
    uint32_t quadCount = 0;
    uint32_t flags = 0;
};

enum BlockEntityKind : uint8_t {
    EntityNone,
    EntityChest,
    EntityTrappedChest,
    EntityEnderChest,
    EntityBed,
    EntityStandingBanner,
    EntityWallBanner,
    EntityFloorSkull,
    EntityWallSkull,
    EntityCopperChest,
    EntityShulkerBox,
};

inline constexpr size_t ChestKinds = 7;
inline constexpr size_t CopperChestKind = 3;
inline constexpr size_t SkullKinds = 7;
inline constexpr size_t DyeColors = 16;
inline constexpr size_t FineRotations = 16;
inline constexpr const char* ChestLidMovingKey = "KestrelLidMoving";

/**
 * The lid of a chest drawn on its own while it moves: its model built facing
 * south in block pixels, and the quarter turns that face the chest's way.
 */
struct ChestLid {
    uint32_t modelTemplate = NoModelTemplate;
    uint32_t rotation = 0;
};

/**
 * Pre-built models for blocks drawn from their block entity, one per look
 * and rotation: the mesher picks one from the block state and the block
 * entity compound.
 */
struct BlockEntityTemplates {
    std::array<std::array<uint32_t, 4>, ChestKinds> chest {};
    std::array<std::array<std::array<uint32_t, 4>, 2>, ChestKinds> doubleChest {};
    std::array<std::array<uint32_t, 4>, ChestKinds> chestBody {};
    std::array<std::array<std::array<uint32_t, 4>, 2>, ChestKinds> doubleChestBody {};
    std::array<uint32_t, ChestKinds> chestLid {};
    std::array<std::array<uint32_t, 2>, ChestKinds> doubleChestLid {};
    std::array<std::array<std::array<uint32_t, 4>, 2>, DyeColors> bed {};
    std::array<std::array<uint32_t, FineRotations>, SkullKinds> floorSkull {};
    std::array<std::array<uint32_t, 4>, SkullKinds> wallSkull {};
    std::array<std::array<uint32_t, FineRotations>, DyeColors> standingBanner {};
    std::array<std::array<uint32_t, 4>, DyeColors> wallBanner {};
    std::array<uint32_t, DyeColors + 1> shulkerBox {};
};

struct BlockVisual {
    uint8_t flags = 0;
    uint8_t blockEntity = EntityNone;
    std::array<uint32_t, 6> faces {};
    uint32_t modelTemplate = NoModelTemplate;
    uint32_t variant = 0;
    uint8_t liquid = 0;
    uint8_t liquidLevel = 0;
    uint8_t lightEmission = 0;
    uint8_t lightFilter = 0;

    bool emitsCubeGeometry() const
    {
        return (flags & (FlagCubeGeometry | FlagDiagnostic)) != 0;
    }

    bool hasModel() const
    {
        return (flags & FlagModel) && modelTemplate != NoModelTemplate;
    }
};

/**
 * A texture array reference. Animated materials occupy frameCount consecutive
 * layers starting at layer. The GPU word packs layer (12 bits), UV rotation,
 * frame interpolation, frame count - 1 (7 bits) and ticks per frame - 1 (11 bits).
 */
enum MaterialTint : uint8_t {
    TintKindMask = 0x3,
    TintVariantShift = 2,
    TintVariantMask = 0xC,
    TintOverlay = 1 << 4,
};

struct Material {
    uint32_t layer = 0;
    bool rotateUv = false;
    uint32_t frameCount = 1;
    uint32_t ticksPerFrame = 1;
    bool interpolate = false;
    uint8_t tint = 0;

    TintKind tintKind() const
    {
        return static_cast<TintKind>(tint & TintKindMask);
    }

    FoliageVariant foliageVariant() const
    {
        return static_cast<FoliageVariant>((tint & TintVariantMask) >> TintVariantShift);
    }

    uint32_t gpuWord() const
    {
        return (layer & 0xFFFu) | (rotateUv ? 1u << 12 : 0u) | (interpolate ? 1u << 13 : 0u) | (((frameCount - 1) & 0x7Fu) << 14) | (((ticksPerFrame - 1) & 0x7FFu) << 21);
    }
};

struct TextureArray {
    uint32_t layers = 0;
    std::array<std::vector<uint8_t>, TextureMipLevels> mips;
};

/**
 * A block declared by the server in StartGame: its identifier and the block
 * property compound (properties, traits, components and permutations).
 */
struct CustomBlock {
    std::string name;
    Tag definition;
};

using SequentialMap = std::vector<int32_t>;

inline constexpr uint32_t EntityTextureSize = 128;
inline constexpr uint32_t MaxEntityTiles = 8;
inline constexpr uint32_t ItemIconSize = 32;

/**
 * The plains green a tinted block gets when it is drawn as an item and has no
 * carried texture of its own.
 */
inline constexpr uint32_t ItemTint = 0x79C05A;
inline constexpr uint32_t SkinSlots = 64;
inline constexpr uint32_t DroppedIconSlots = 64;

/**
 * One geometry of an entity: quads in 1/256 block around its feet, facing
 * north, unposed and tagged with the bone they follow, its bone rig and the
 * height to width ratio of the texture its UVs were laid out for.
 */
struct EntityRig {
    std::vector<ModelQuad> quads;
    std::vector<uint16_t> quadBones;
    std::vector<EntityBone> bones;
    float textureAspect = 1.0f;
};

/**
 * The model a player skin carries: the geometry its resource patch names out
 * of the skin's own geometry data, or null when the skin has none.
 */
std::shared_ptr<const EntityRig> buildSkinRig(const std::string& geometryData, const std::string& resourcePatch);

inline constexpr uint32_t NoEntityChoice = 0xFFFFFFFFu;

/**
 * A worn armor piece as the humanoid armor models draw it: the model for its
 * slot (helmet, chestplate, leggings, boots) and its material's texture layer.
 */
struct ArmorLook {
    const EntityRig* rig = nullptr;
    uint32_t layer = 0;
};

/**
 * A bone name pattern ('*' matches any run of characters, lowercase) and the
 * Molang expression telling whether the matching bones are drawn.
 */
struct EntityPartRule {
    std::string pattern;
    molang::Script visible;
};

/**
 * A render controller bound to one entity: its condition from the entity's
 * list, the geometry and texture selectors, each an expression yielding an
 * index into the rig or texture layer choices (NoEntityChoice when missing),
 * and the part visibility rules in order, later rules winning.
 */
enum class EntityBlend : uint8_t {
    Opaque,
    Blend,
    Additive,
};

struct EntityRenderController {
    molang::Script condition;
    molang::Script geometry;
    std::vector<uint32_t> geometryChoices;
    molang::Script texture;
    std::vector<uint32_t> textureChoices;
    std::vector<EntityPartRule> parts;
    EntityBlend blend = EntityBlend::Opaque;
    bool oneSided = false;
};

/**
 * Where a quad of a combined rig came from: the render controller that draws
 * it and the rig that controller has to pick for it to show.
 */
struct CombinedQuadSource {
    uint16_t controller = 0;
    uint16_t rig = 0;
};

/**
 * An entity's model from its client definition: every geometry it references
 * as a rig (the default one first), its render controllers, its scripts and
 * the default entity texture layer it samples. An entity drawn by several
 * render controllers at once also gets one combined rig holding every
 * geometry those controllers can pick, bones shared by name, so a single
 * animation pass poses all of them.
 */
struct EntityModel {
    std::vector<EntityRig> rigs;
    std::vector<EntityRenderController> controllers;
    std::shared_ptr<const EntityScripts> scripts;
    uint32_t layer = 0;
    EntityRig combined;
    std::vector<CombinedQuadSource> combinedSources;
};

class BlockAssets {
public:
    static std::shared_ptr<const BlockAssets> shared(std::string& error);
    static std::shared_ptr<const BlockAssets> create(const std::vector<std::shared_ptr<const PackFiles>>& packs, const std::vector<CustomBlock>& customBlocks, std::string& error);

    const BlockVisual& visual(uint32_t networkValue, bool hashed, const SequentialMap* sequential = nullptr) const;
    std::shared_ptr<const SequentialMap> sequentialMap() const;
    std::string describe(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const;
    std::string blockName(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const;
    const Tag* blockStates(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const;

    /**
     * The block state hash of a network block value, or 0 for a value that
     * names no vanilla state.
     */
    uint32_t stateHash(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const;
    std::optional<uint32_t> networkValueForState(uint32_t hash, bool hashed, const SequentialMap* sequential) const;
    uint32_t blockEntityTemplate(const BlockVisual& visual, const Tag* data, const std::array<int32_t, 3>& position) const;
    ChestLid chestLid(const BlockVisual& visual, const Tag* data, const std::array<int32_t, 3>& position) const;

    size_t customBlockCount() const
    {
        return customs.size();
    }

    size_t customStateCount() const
    {
        return customStates.size();
    }

    const std::vector<Material>& materials() const
    {
        return materialTable;
    }

    const TextureArray& textures() const
    {
        return textureArray;
    }

    const std::vector<ModelTemplate>& modelTemplates() const
    {
        return templates;
    }

    const std::vector<ModelQuad>& modelQuads() const
    {
        return quads;
    }

    const BiomeTints& biomeTints() const
    {
        return biomes;
    }

    const EntityModel* entityModel(const std::string& identifier) const
    {
        auto found = entityModels.find(identifier);
        return found == entityModels.end() ? nullptr : &found->second;
    }

    /**
     * How armor in the given slot looks, or no rig for items without an
     * armor texture (elytra, heads, pumpkins).
     */
    ArmorLook armorLook(size_t slot, const std::string& identifier) const;

    const AnimationLibrary& animationLibrary() const
    {
        return animations;
    }

    const std::vector<uint8_t>& entityTexturePixels() const
    {
        return entityPixels;
    }

    uint32_t entityTextureLayers() const
    {
        return static_cast<uint32_t>(entityPixels.size() / (size_t(EntityTextureSize) * EntityTextureSize * 4));
    }

    uint32_t skinLayerBase() const
    {
        return entityTextureLayers();
    }

    /**
     * How many layers across and down an entity texture starting at layer is
     * cut into; one by one for all but the textures bigger than a layer.
     */
    std::pair<uint32_t, uint32_t> entityTileGrid(uint32_t layer) const
    {
        auto found = entityTiles.find(layer);
        return found == entityTiles.end() ? std::make_pair(1u, 1u) : found->second;
    }

    std::vector<uint8_t> itemIcon(const std::string& identifier, int32_t aux, const std::string& iconHint) const;

    /**
     * The look of the default state of the block an item places, when it is
     * drawn as a full cube; null for other items.
     */
    const BlockVisual* itemCube(const std::string& identifier) const;
    std::vector<ModelQuad> itemGeometry(const std::string& identifier) const;

    size_t itemTextureCount() const
    {
        return itemTextures.size() + itemFiles.size();
    }

    uint32_t sunLayer() const
    {
        return sun;
    }

    uint32_t moonLayer(uint32_t phase) const
    {
        return moonPhases[phase % 8];
    }

    /**
     * The crack texture drawn over a block being broken, stage 0 to 9, or 0
     * when the pack has none.
     */
    uint32_t destroyStageLayer(uint32_t stage) const
    {
        return destroyStages[std::min(stage, DestroyStages - 1)];
    }

    uint32_t templateFlags(const BlockVisual& visual) const
    {
        return visual.hasModel() && visual.modelTemplate < templates.size() ? templates[visual.modelTemplate].flags : 0;
    }

    size_t diagnosticVisuals() const
    {
        return diagnosticCount;
    }

    uint64_t unresolvedLookups() const
    {
        return unresolved.load();
    }

    uint32_t lastUnresolvedValue() const
    {
        return lastUnresolved.load();
    }

    uint32_t airSequentialId() const
    {
        return airSequential;
    }

    uint32_t airNetworkHash() const
    {
        return airHash;
    }

private:
    struct CustomState {
        size_t block = 0;
        Tag states;
    };

    bool build(const std::vector<std::shared_ptr<const PackFiles>>& packs, std::string& error);
    void buildEntityModels(PackSource& pack, const std::vector<std::shared_ptr<const PackFiles>>& packs);
    void buildInterfaceAssets(PackSource& pack, const std::vector<std::shared_ptr<const PackFiles>>& packs);
    void buildBlockEntityTemplates(PackSource& pack, std::vector<std::vector<uint8_t>>& layers, std::vector<bool>& overlayLayers, std::map<std::string, uint32_t>& materialByKey,
        const std::function<uint32_t(const std::vector<ModelQuad>&, uint32_t)>& pushTemplate);
    const std::string& nameAt(size_t index) const;
    int32_t indexOf(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const;

    /**
     * The default state of the block an item places, with its carried
     * textures when the block has them; null for items that place no block.
     */
    const BlockVisual* itemVisual(const std::string& identifier) const;

    BlockRegistry registry;
    std::vector<CustomBlock> customs;
    std::vector<CustomState> customStates;
    std::unordered_map<uint32_t, uint32_t> customByHash;
    std::vector<BlockVisual> visuals;
    std::vector<Material> materialTable;
    std::vector<ModelTemplate> templates;
    std::vector<ModelQuad> quads;
    BlockEntityTemplates entityTemplates;
    std::unordered_map<std::string, EntityModel> entityModels;
    std::array<EntityRig, 4> armorRigs;
    std::unordered_map<std::string, uint32_t> armorLayers;
    AnimationLibrary animations;
    std::vector<uint8_t> entityPixels;
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> entityTiles;
    TextureArray textureArray;
    BiomeTints biomes;
    std::unordered_map<std::string, std::vector<std::vector<uint8_t>>> itemTextures;
    std::unordered_map<std::string, uint32_t> blockByName;
    std::unordered_map<std::string, BlockVisual> carriedVisuals;
    std::unordered_map<std::string, std::vector<uint8_t>> itemFiles;
    std::unordered_map<std::string, std::string> itemIconNames;
    uint32_t sun = 0;
    std::array<uint32_t, 8> moonPhases {};
    std::array<uint32_t, DestroyStages> destroyStages {};
    size_t diagnosticCount = 0;
    uint32_t airSequential = 0;
    uint32_t airHash = 0;
    mutable std::atomic<uint64_t> unresolved { 0 };
    mutable std::atomic<uint32_t> lastUnresolved { 0 };
};

}
