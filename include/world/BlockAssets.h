#pragma once

#include "world/BiomeTints.h"
#include "world/BlockRegistry.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kestrel::world {

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
inline constexpr uint32_t NoModelTemplate = 0xFFFFFFFFu;

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

struct BlockVisual {
    uint8_t flags = 0;
    std::array<uint32_t, 6> faces {};
    uint32_t modelTemplate = NoModelTemplate;
    uint32_t variant = 0;

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

struct CustomBlock {
    std::string name;
    uint32_t permutations = 1;
};

using SequentialMap = std::vector<int32_t>;

class BlockAssets {
public:
    static std::shared_ptr<const BlockAssets> shared(std::string& error);

    const BlockVisual& visual(uint32_t networkValue, bool hashed, const SequentialMap* sequential = nullptr) const;
    std::shared_ptr<const SequentialMap> sequentialMap(const std::vector<CustomBlock>& customBlocks) const;
    std::string describe(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const;

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

    uint32_t sunLayer() const
    {
        return sun;
    }

    uint32_t moonLayer(uint32_t phase) const
    {
        return moonPhases[phase % 8];
    }

    const std::vector<uint8_t>& cloudMask() const
    {
        return clouds;
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
    bool build(std::string& error);

    BlockRegistry registry;
    std::vector<BlockVisual> visuals;
    std::vector<Material> materialTable;
    std::vector<ModelTemplate> templates;
    std::vector<ModelQuad> quads;
    TextureArray textureArray;
    BiomeTints biomes;
    uint32_t sun = 0;
    std::array<uint32_t, 8> moonPhases {};
    std::vector<uint8_t> clouds;
    size_t diagnosticCount = 0;
    uint32_t airSequential = 0;
    uint32_t airHash = 0;
    mutable std::atomic<uint64_t> unresolved { 0 };
    mutable std::atomic<uint32_t> lastUnresolved { 0 };
};

}
