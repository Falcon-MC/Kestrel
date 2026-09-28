#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace kestrel {

class Window;

namespace ui {
class DrawList;
}

struct BlockTextureUpload {
    const uint8_t* const* mips = nullptr;
    uint32_t layers = 0;
    uint32_t size = 0;
    uint32_t mipLevels = 0;
};

/**
 * Cube quads are 12 bytes each; model quads are 48 bytes (twelve words) each.
 */
struct ChunkMeshUpload {
    const void* cubes = nullptr;
    uint32_t cubeCount = 0;
    const void* models = nullptr;
    uint32_t modelCount = 0;
    const void* translucentCubes = nullptr;
    uint32_t translucentCubeCount = 0;
    const void* translucentModels = nullptr;
    uint32_t translucentModelCount = 0;
};

inline constexpr uint32_t BlockTexturePageLayers = 2048;
inline constexpr uint32_t BlockTexturePages = 2;
inline constexpr uint32_t CubeQuadBytes = 20;
inline constexpr uint32_t ModelQuadBytes = 64;

enum SkyVertexFlag : uint32_t {
    SkyTextured = 1 << 0,
    SkyAdditive = 1 << 1,
};

/**
 * One vertex of the sky or a celestial body. Positions are relative to the
 * draw origin and color is RGBA8.
 */
struct SkyVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    uint32_t layer = 0;
    uint32_t color = 0xFFFFFFFFu;
    uint32_t flags = 0;
};

static_assert(sizeof(SkyVertex) == 32);

struct WorldView {
    std::array<float, 16> viewProjection {};
    double cameraX = 0.0;
    double cameraY = 0.0;
    double cameraZ = 0.0;
    float animationTicks = 0.0f;
    std::array<float, 3> fogColor { 0.6f, 0.75f, 1.0f };
    float fogStart = 192.0f;
    float fogEnd = 256.0f;
    float daylight = 1.0f;
    float nightVision = 0.0f;
    std::array<float, 3> sunDirection { 0.0f, 1.0f, 0.0f };
    const SkyVertex* background = nullptr;
    uint32_t backgroundCount = 0;
    /**
     * Entity quads in four runs: entityQuadCount opaque ones, then
     * entityBlendCount alpha blended ones drawn after translucent terrain,
     * then handQuadCount first person ones drawn last in a sliver of the depth
     * range so walls never cut into the hand, then overlayQuadCount block
     * cracks and outline edges. Overlays are placed in 1/1024 block and
     * multiply the color under them: entity quads shade it like 40% black, the rest
     * double what their texture covers, like the game's Cracks material.
     */
    const void* entityQuads = nullptr;
    uint32_t entityQuadCount = 0;
    uint32_t entityBlendCount = 0;
    uint32_t handQuadCount = 0;
    uint32_t overlayQuadCount = 0;
    std::array<float, 3> entityOrigin {};

    uint32_t entityTotal() const
    {
        return entityQuadCount + entityBlendCount + handQuadCount + overlayQuadCount;
    }

    uint32_t overlayStart() const
    {
        return entityQuadCount + entityBlendCount + handQuadCount;
    }
};

/**
 * The depth range first person quads are squeezed into.
 */
inline constexpr float HandDepthRange = 0.05f;

/**
 * Push constants shared by every world pipeline: view projection, draw origin
 * and animation ticks, fog color and start, fog end, daylight, sun direction.
 */
struct WorldConstants {
    std::array<float, 32> values {};

    WorldConstants(const WorldView& view)
    {
        std::copy(view.viewProjection.begin(), view.viewProjection.end(), values.begin());
        values[19] = view.animationTicks;
        values[20] = view.fogColor[0];
        values[21] = view.fogColor[1];
        values[22] = view.fogColor[2];
        values[23] = view.fogStart;
        values[24] = view.fogEnd;
        values[25] = view.daylight;
        values[26] = view.nightVision;
        values[28] = view.sunDirection[0];
        values[29] = view.sunDirection[1];
        values[30] = view.sunDirection[2];
    }

    void setOrigin(float x, float y, float z)
    {
        values[16] = x;
        values[17] = y;
        values[18] = z;
    }
};

/**
 * The side and near planes of a camera relative view projection; a sub-chunk
 * is drawn only when its bounding box touches all of them.
 */
struct ChunkFrustum {
    std::array<std::array<float, 4>, 5> planes {};

    explicit ChunkFrustum(const WorldView& view)
    {
        const std::array<float, 16>& matrix = view.viewProjection;
        for (size_t axis = 0; axis < 2; ++axis) {
            for (size_t side = 0; side < 2; ++side) {
                float sign = side == 0 ? 1.0f : -1.0f;
                for (size_t column = 0; column < 4; ++column) {
                    planes[axis * 2 + side][column] = matrix[column * 4 + 3] + sign * matrix[column * 4 + axis];
                }
            }
        }
        for (size_t column = 0; column < 4; ++column) {
            planes[4][column] = matrix[column * 4 + 2];
        }
    }

    bool contains(const WorldView& view, int32_t x, int32_t y, int32_t z) const
    {
        float cx = static_cast<float>(x + 8 - view.cameraX);
        float cy = static_cast<float>(y + 8 - view.cameraY);
        float cz = static_cast<float>(z + 8 - view.cameraZ);
        for (const std::array<float, 4>& plane : planes) {
            float radius = 8.0f * (std::abs(plane[0]) + std::abs(plane[1]) + std::abs(plane[2]));
            if (plane[0] * cx + plane[1] * cy + plane[2] * cz + plane[3] < -radius) {
                return false;
            }
        }
        return true;
    }
};

/**
 * The latest frame the GPU has finished: its submission number (counting
 * from one) and how many sub-chunks with opaque terrain it drew.
 */
struct CompletedFrame {
    uint64_t submission = 0;
    uint32_t opaqueChunks = 0;
};

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual std::string_view backendName() const = 0;
    virtual const std::string& deviceName() const = 0;
    virtual uint64_t submittedFrames() const = 0;
    virtual CompletedFrame completedFrame() const = 0;

    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void uploadUiAtlas(const uint8_t* pixels, uint32_t width, uint32_t height) = 0;
    virtual void uploadBlockTextures(const BlockTextureUpload& textures) = 0;
    virtual void setChunkMesh(uint64_t id, int32_t originX, int32_t originY, int32_t originZ, const ChunkMeshUpload& mesh) = 0;
    virtual void removeChunkMesh(uint64_t id) = 0;
    virtual void clearChunkMeshes() = 0;

    /**
     * Entity textures: square RGBA layers sampled by entity quads, which set
     * bit 5 of their shade word. Single layers are replaced for player skins.
     */
    virtual void uploadEntityTextures(const uint8_t* pixels, uint32_t size, uint32_t layers) = 0;
    virtual void updateEntityTexture(uint32_t layer, const uint8_t* pixels) = 0;

    virtual void beginFrame(float r, float g, float b) = 0;
    virtual void drawWorld(const WorldView& view) = 0;
    virtual void drawUi(const ui::DrawList& list) = 0;
    virtual void endFrame() = 0;

    static std::unique_ptr<Renderer> create(Window& window);
};

}
