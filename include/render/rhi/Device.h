#pragma once

#include "render/Renderer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel {

class Window;
class Renderer;

}

namespace kestrel::rhi {

enum class VertexFormat {
    Float,
    Float2,
    Float3,
    UByte4Norm,
    UInt,
    UInt4,
};

struct VertexAttribute {
    const char* semantic = "";
    uint32_t index = 0;
    VertexFormat format = VertexFormat::Float;
    uint32_t offset = 0;
};

/**
 * How one vertex buffer is read: its attributes, its stride and whether it
 * advances per instance instead of per vertex.
 */
struct VertexLayout {
    std::vector<VertexAttribute> attributes;
    uint32_t stride = 0;
    bool perInstance = false;
};

/**
 * Alpha blends straight alpha over the target, Premultiplied adds a color
 * already multiplied by its alpha, Multiply scales the target by the source
 * color and keeps the target alpha.
 */
enum class BlendMode {
    None,
    Alpha,
    Premultiplied,
    Multiply,
};

/**
 * Always passes every fragment, for draws meant to show through the world.
 */
enum class DepthCompare {
    Less,
    LessEqual,
    Equal,
    Always,
};

/**
 * PixelClamp samples nearest texels and clamps, for the interface.
 * TerrainWrap magnifies nearest, minifies linearly with linear mips and wraps
 * horizontally, for blocks and entities.
 */
enum class SamplerMode {
    PixelClamp,
    TerrainWrap,
};

/**
 * The shader source a pipeline takes its entry points from; each backend
 * keeps its own translation of every library. Primitive draws custom vertices
 * in the vertex color times the interface atlas, with the custom draw
 * constants.
 */
enum class ShaderLibrary {
    Ui,
    World,
    Primitive,
};

/**
 * What a pipeline reads besides its vertices: constantCount 32-bit push
 * constants, visible to the vertex stage only or to every stage, and a set of
 * textureCount textures seen by the pixel stage through one sampler.
 */
struct BindingLayout {
    uint32_t constantCount = 0;
    bool constantsVertexOnly = false;
    uint32_t textureCount = 0;
    SamplerMode sampler = SamplerMode::PixelClamp;
    bool actorConstants = false;
};

/**
 * With source set the pipeline is built from that code, and library and the
 * entry names are ignored.
 */
struct PipelineDesc {
    const ShaderSource* source = nullptr;
    ShaderLibrary library = ShaderLibrary::Ui;
    const char* vertexEntry = "";
    const char* pixelEntry = "";
    VertexLayout vertices;
    BindingLayout bindings;
    BlendMode blend = BlendMode::None;
    bool depthWrite = true;
    bool colorWrite = true;
    bool cullBackFaces = false;
    DepthCompare depthCompare = DepthCompare::Less;
};

/**
 * A 2D RGBA8 texture, or an array of layers when array is set, with the given
 * number of mip levels.
 */
struct TextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t layers = 1;
    uint32_t mipLevels = 1;
    bool array = false;
};

/**
 * RGBA8 pixels for a region of one mip/layer. Zero dimensions select the
 * whole mip; zero rowBytes selects tightly packed rows.
 */
struct TextureData {
    uint32_t layer = 0;
    uint32_t mip = 0;
    const uint8_t* pixels = nullptr;
    uint32_t x = 0, y = 0;
    uint32_t width = 0, height = 0;
    size_t rowBytes = 0;
};

/**
 * A buffer the CPU writes and the GPU reads, mapped for its whole life.
 */
class Buffer {
public:
    virtual ~Buffer() = default;
    virtual void* mapped() = 0;
    virtual size_t size() const = 0;
};

class Texture {
public:
    virtual ~Texture() = default;
    virtual const TextureDesc& desc() const = 0;
};

class Pipeline {
public:
    virtual ~Pipeline() = default;
};

/**
 * A fixed number of texture slots bound together to a pipeline's textures.
 * An empty slot reads as a texture of the kind the set was made for.
 */
class TextureSet {
public:
    virtual ~TextureSet() = default;
    virtual void bind(uint32_t slot, const Texture* texture) = 0;
};

/**
 * One GPU and its window: resources, pipelines and the commands of the
 * frame being recorded. Draw state set during a frame lasts until the frame
 * ends.
 */
class Device {
public:
    virtual ~Device() = default;

    virtual std::string_view backendName() const = 0;
    virtual const std::string& deviceName() const = 0;
    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;
    virtual uint32_t framesInFlight() const = 0;

    /**
     * The slot of the frame being recorded, below framesInFlight; resources
     * written each frame are kept once per slot.
     */
    virtual uint32_t frameSlot() const = 0;

    /**
     * The submission number of the latest frame the GPU has finished,
     * counting from one, or zero before any.
     */
    virtual uint64_t completedSubmission() const = 0;
    virtual uint64_t submittedFrames() const = 0;

    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void waitIdle() = 0;

    virtual std::unique_ptr<Buffer> createBuffer(size_t size) = 0;
    virtual std::unique_ptr<Buffer> createPersistentBuffer(size_t size) = 0;
    virtual void uploadBuffer(Buffer& target, const void* data, size_t bytes) = 0;
    virtual std::unique_ptr<Texture> createTexture(const TextureDesc& desc) = 0;

    /**
     * Copies the given subresources into a texture and waits until they are
     * in place, leaving it ready for sampling.
     */
    virtual void uploadTexture(Texture& texture, const std::vector<TextureData>& data) = 0;
    // Copies input before returning; staging survives until the queued copy completes.
    virtual void uploadTextureAsync(Texture& texture, const std::vector<TextureData>& data) = 0;
    virtual std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) = 0;
    /**
     * Texture slots for pipelines that read count textures, 2D arrays when
     * arrays is set, sampled the given way.
     */
    virtual std::unique_ptr<TextureSet> createTextureSet(uint32_t count, bool arrays, SamplerMode sampler) = 0;

    /**
     * Keeps a buffer alive until every frame recorded so far has finished.
     */
    virtual void retire(std::unique_ptr<Buffer> buffer) = 0;

    /**
     * Starts recording a frame that clears color and depth, and returns the
     * submission number it will have. A frame the window cannot show right now
     * records nothing and is not submitted.
     */
    virtual uint64_t beginFrame(float r, float g, float b) = 0;

    /**
     * Whether the current frame records commands; resources written each
     * frame must be left alone otherwise, since the GPU may still read them.
     */
    virtual bool recording() const = 0;
    virtual void setPipeline(const Pipeline& pipeline) = 0;
    virtual void setConstants(const void* values, uint32_t count) = 0;
    // Copies 36 words of actor pose data into frame-owned GPU storage.
    virtual void setActorConstants(const void* values, uint32_t count) = 0;
    virtual void setTextures(const TextureSet& textures) = 0;
    virtual void setVertexBuffer(const Buffer& buffer, uint32_t stride, size_t bytes) = 0;
    virtual void setIndexBuffer(const Buffer& buffer, size_t bytes) = 0;

    /**
     * Squeezes the depth the following draws write into [0, maxDepth].
     */
    virtual void setDepthRange(float maxDepth) = 0;
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstInstance = 0) = 0;
    virtual void endFrame() = 0;

    virtual bool supportsSceneCopy() const
    {
        return false;
    }

    /**
     * Copies what the frame has drawn so far into the scene textures and
     * carries on drawing on top of it: slot 0 always, slot 3 with keep, and
     * with first the depth into slot 1 and the color into slots 2 and 3 too.
     */
    virtual void copyScene(bool first, bool keep)
    {
        (void)first;
        (void)keep;
    }

    /**
     * The four scene textures for post pipelines, sampled nearest; null
     * without scene copies.
     */
    virtual const TextureSet* sceneTextures() const
    {
        return nullptr;
    }

    /**
     * Asks for the next frame to be read back once it is drawn. False when
     * the backend cannot read its frames back.
     */
    virtual bool requestCapture()
    {
        return false;
    }

    /**
     * Whether presenting waits for the display's refresh.
     */
    virtual void setVsync(bool enabled) = 0;

    /**
     * The captured frame as RGBA rows from the top, once one is ready.
     */
    virtual bool takeCapture(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height)
    {
        (void)rgba;
        (void)width;
        (void)height;
        return false;
    }

    static std::unique_ptr<Device> create(Window& window);
};

/**
 * The renderer that draws the world and the interface through a device.
 */
std::unique_ptr<Renderer> createRenderer(std::unique_ptr<Device> device);

}
