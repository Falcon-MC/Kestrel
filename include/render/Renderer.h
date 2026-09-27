#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

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

inline constexpr uint32_t CubeQuadBytes = 20;
inline constexpr uint32_t ModelQuadBytes = 64;

enum SkyVertexFlag : uint32_t {
    SkyTextured = 1 << 0,
    SkyAdditive = 1 << 1,
    SkyFogged = 1 << 2,
    SkyNormalShift = 3,
};

/**
 * One vertex of the sky, celestial bodies or clouds. Positions are relative to
 * the draw origin, color is RGBA8 and the normal index (flags >> 3, 1..6) lights
 * cloud faces from the sun.
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
    std::array<float, 3> sunDirection { 0.0f, 1.0f, 0.0f };
    const SkyVertex* background = nullptr;
    uint32_t backgroundCount = 0;
    const std::array<float, 3>* cloudOrigins = nullptr;
    uint32_t cloudOriginCount = 0;
};

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

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void uploadUiAtlas(const uint8_t* pixels, uint32_t width, uint32_t height) = 0;
    virtual void uploadBlockTextures(const BlockTextureUpload& textures) = 0;
    virtual void setChunkMesh(uint64_t id, int32_t originX, int32_t originY, int32_t originZ, const ChunkMeshUpload& mesh) = 0;
    virtual void removeChunkMesh(uint64_t id) = 0;
    virtual void clearChunkMeshes() = 0;
    virtual void setCloudMesh(const SkyVertex* vertices, uint32_t count) = 0;

    virtual void beginFrame(float r, float g, float b) = 0;
    virtual void drawWorld(const WorldView& view) = 0;
    virtual void drawUi(const ui::DrawList& list) = 0;
    virtual void endFrame() = 0;

    static std::unique_ptr<Renderer> create(Window& window);
};

}
