#include "render/rhi/Device.h"
#include "render/Renderer.h"

#include "client/DebugLog.h"
#include "ui/DrawList.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kestrel::rhi {

namespace {

constexpr uint32_t StreamStride[4] = { CubeQuadBytes, ModelQuadBytes, CubeQuadBytes, ModelQuadBytes };
constexpr uint32_t WorldTextureCount = BlockTexturePages + EntityTexturePages;
constexpr uint32_t EntityMipLevels = 5;

/**
 * The smaller levels of one square entity layer, each the average of the four
 * texels above it with colour weighted by coverage, so cut-out edges keep
 * their colour and a far face shows the texture's tone instead of moiré.
 */
std::vector<std::vector<uint8_t>> entityMips(const uint8_t* base, uint32_t size, uint32_t levels)
{
    std::vector<std::vector<uint8_t>> mips;
    const uint8_t* source = base;
    uint32_t side = size;
    for (uint32_t level = 1; level < levels && side > 1; ++level) {
        uint32_t half = side / 2;
        std::vector<uint8_t> next(size_t(half) * half * 4);
        for (uint32_t y = 0; y < half; ++y) {
            for (uint32_t x = 0; x < half; ++x) {
                uint32_t color[3] {};
                uint32_t alpha = 0;
                for (uint32_t dy = 0; dy < 2; ++dy) {
                    for (uint32_t dx = 0; dx < 2; ++dx) {
                        const uint8_t* texel = source + (size_t(y * 2 + dy) * side + x * 2 + dx) * 4;
                        for (size_t channel = 0; channel < 3; ++channel) {
                            color[channel] += uint32_t(texel[channel]) * texel[3];
                        }
                        alpha += texel[3];
                    }
                }
                uint8_t* out = next.data() + (size_t(y) * half + x) * 4;
                for (size_t channel = 0; channel < 3; ++channel) {
                    out[channel] = alpha > 0 ? static_cast<uint8_t>(color[channel] / alpha) : 0;
                }
                out[3] = static_cast<uint8_t>(alpha / 4);
            }
        }
        mips.push_back(std::move(next));
        source = mips.back().data();
        side = half;
    }
    return mips;
}

VertexLayout uiLayout()
{
    return {
        {
            { "POSITION", 0, VertexFormat::Float2, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 8 },
            { "COLOR", 0, VertexFormat::UByte4Norm, 16 },
            { "TEXCOORD", 1, VertexFormat::Float2, 20 },
            { "TEXCOORD", 2, VertexFormat::Float2, 28 },
            { "TEXCOORD", 3, VertexFormat::Float2, 36 },
            { "TEXCOORD", 4, VertexFormat::Float, 44 },
        },
        sizeof(ui::UiVertex),
        false,
    };
}

VertexLayout cubeLayout()
{
    return {
        {
            { "QUAD", 0, VertexFormat::UInt4, 0 },
            { "QUAD", 1, VertexFormat::UInt, 16 },
        },
        CubeQuadBytes,
        true,
    };
}

VertexLayout modelLayout()
{
    return {
        {
            { "MODEL", 0, VertexFormat::UInt4, 0 },
            { "MODEL", 1, VertexFormat::UInt4, 16 },
            { "MODEL", 2, VertexFormat::UInt4, 32 },
            { "MODEL", 3, VertexFormat::UInt4, 48 },
        },
        ModelQuadBytes,
        true,
    };
}

VertexLayout skyLayout()
{
    return {
        {
            { "POSITION", 0, VertexFormat::Float3, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 12 },
            { "LAYER", 0, VertexFormat::UInt, 20 },
            { "COLOR", 0, VertexFormat::UInt, 24 },
            { "FLAGS", 0, VertexFormat::UInt, 28 },
        },
        sizeof(SkyVertex),
        false,
    };
}

VertexLayout customLayout()
{
    return {
        {
            { "POSITION", 0, VertexFormat::Float3, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 12 },
            { "COLOR", 0, VertexFormat::UByte4Norm, 20 },
        },
        sizeof(CustomVertex),
        false,
    };
}

BlendMode customBlend(CustomBlend blend)
{
    switch (blend) {
    case CustomBlend::Opaque:
        return BlendMode::None;
    case CustomBlend::Alpha:
        return BlendMode::Alpha;
    case CustomBlend::Premultiplied:
        return BlendMode::Premultiplied;
    case CustomBlend::Multiply:
        return BlendMode::Multiply;
    }
    return BlendMode::Alpha;
}

PipelineDesc worldPipeline(const char* vertexEntry, const char* pixelEntry, VertexLayout vertices, BlendMode blend, bool depthWrite, DepthCompare compare)
{
    PipelineDesc desc;
    desc.library = ShaderLibrary::World;
    desc.vertexEntry = vertexEntry;
    desc.pixelEntry = pixelEntry;
    desc.vertices = std::move(vertices);
    desc.bindings = { 32, false, WorldTextureCount, SamplerMode::TerrainWrap };
    desc.blend = blend;
    desc.depthWrite = depthWrite;
    desc.depthCompare = compare;
    return desc;
}

/**
 * Draws the world and the interface with nothing but device commands, so
 * every backend shares the same culling, ordering and passes.
 */
class RhiRenderer final : public Renderer {
public:
    explicit RhiRenderer(std::unique_ptr<Device> created)
        : device(std::move(created))
        , frames(device->framesInFlight())
        , slotFrames(device->framesInFlight())
    {
        PipelineDesc ui;
        ui.library = ShaderLibrary::Ui;
        ui.vertexEntry = "vs_main";
        ui.pixelEntry = "ps_main";
        ui.vertices = uiLayout();
        ui.bindings = { 2, true, 1, SamplerMode::PixelClamp };
        ui.blend = BlendMode::Alpha;
        ui.depthWrite = false;
        ui.depthCompare = DepthCompare::LessEqual;
        uiPipeline = device->createPipeline(ui);

        cubePipeline = device->createPipeline(worldPipeline("vs_world", "ps_world", cubeLayout(), BlendMode::None, true, DepthCompare::Less));
        modelPipeline = device->createPipeline(worldPipeline("vs_model", "ps_world", modelLayout(), BlendMode::None, true, DepthCompare::Less));
        modelBlendPipeline = device->createPipeline(worldPipeline("vs_model", "ps_blend", modelLayout(), BlendMode::Premultiplied, false, DepthCompare::Less));
        cubeBlendPipeline = device->createPipeline(worldPipeline("vs_world", "ps_blend", cubeLayout(), BlendMode::Premultiplied, false, DepthCompare::Less));
        skyPipeline = device->createPipeline(worldPipeline("vs_sky", "ps_sky", skyLayout(), BlendMode::Premultiplied, false, DepthCompare::Less));
        overlayPipeline = device->createPipeline(worldPipeline("vs_overlay", "ps_overlay", modelLayout(), BlendMode::Multiply, false, DepthCompare::LessEqual));

        uiTextures = device->createTextureSet(1, false, SamplerMode::PixelClamp);
        worldTextures = device->createTextureSet(WorldTextureCount, true, SamplerMode::TerrainWrap);
    }

    ~RhiRenderer() override
    {
        device->waitIdle();
    }

    std::string_view backendName() const override
    {
        return device->backendName();
    }

    const std::string& deviceName() const override
    {
        return device->deviceName();
    }

    uint64_t submittedFrames() const override
    {
        return device->submittedFrames();
    }

    CompletedFrame completedFrame() const override
    {
        return completedReport;
    }

    void resize(uint32_t width, uint32_t height) override
    {
        device->resize(width, height);
    }

    void uploadUiAtlas(const uint8_t* pixels, uint32_t width, uint32_t height) override
    {
        device->waitIdle();
        atlas.reset();
        atlas = device->createTexture({ width, height, 1, 1, false });
        device->uploadTexture(*atlas, { { 0, 0, pixels } });
        uiTextures->bind(0, atlas.get());
    }

    void uploadBlockTextures(const BlockTextureUpload& textures) override
    {
        if (textures.layers == 0) {
            return;
        }
        device->waitIdle();
        for (uint32_t page = 0; page < BlockTexturePages; ++page) {
            uint32_t first = page * BlockTexturePageLayers;
            uint32_t count = textures.layers > first ? std::min(textures.layers - first, BlockTexturePageLayers) : 0;
            debugLog("RHI block texture page=" + std::to_string(page) + " size=" + std::to_string(textures.size)
                     + " layers=" + std::to_string(count) + " mips=" + std::to_string(textures.mipLevels));
            blockTextures[page].reset();
            if (count == 0) {
                worldTextures->bind(page, nullptr);
                continue;
            }
            blockTextures[page] = device->createTexture({ textures.size, textures.size, count, textures.mipLevels, true });
            std::vector<TextureData> data;
            data.reserve(static_cast<size_t>(count) * textures.mipLevels);
            for (uint32_t layer = 0; layer < count; ++layer) {
                for (uint32_t mip = 0; mip < textures.mipLevels; ++mip) {
                    uint32_t side = std::max<uint32_t>(textures.size >> mip, 1);
                    data.push_back({ layer, mip, textures.mips[mip] + static_cast<size_t>(first + layer) * side * side * 4 });
                }
            }
            device->uploadTexture(*blockTextures[page], data);
            worldTextures->bind(page, blockTextures[page].get());
        }
    }

    void uploadEntityTextures(const uint8_t* pixels, uint32_t size, uint32_t layers) override
    {
        debugLog("RHI entity textures size=" + std::to_string(size) + " layers=" + std::to_string(layers)
                 + " bytes=" + std::to_string(uint64_t(size) * size * layers * 4));
        if (layers > EntityTexturePageLayers * EntityTexturePages) {
            throw std::runtime_error("Entity textures exceed the 8192-layer material index capacity");
        }
        device->waitIdle();
        entitySize = size;
        entityLayers = layers;
        for (uint32_t page = 0; page < EntityTexturePages; ++page) {
            entityTextures[page].reset();
            uint32_t first = page * EntityTexturePageLayers;
            uint32_t count = layers > first ? std::min(layers - first, EntityTexturePageLayers) : 0;
            if (count == 0) {
                worldTextures->bind(BlockTexturePages + page, nullptr);
                continue;
            }
            entityTextures[page] = device->createTexture({ size, size, count, EntityMipLevels, true });
            std::vector<TextureData> data;
            std::vector<std::vector<std::vector<uint8_t>>> mips(count);
            data.reserve(static_cast<size_t>(count) * EntityMipLevels);
            for (uint32_t layer = 0; layer < count; ++layer) {
                const uint8_t* base = pixels + (static_cast<size_t>(first) + layer) * size * size * 4;
                data.push_back({ layer, 0, base });
                mips[layer] = entityMips(base, size, EntityMipLevels);
                for (size_t level = 0; level < mips[layer].size(); ++level) {
                    data.push_back({ layer, static_cast<uint32_t>(level + 1), mips[layer][level].data() });
                }
            }
            device->uploadTexture(*entityTextures[page], data);
            worldTextures->bind(BlockTexturePages + page, entityTextures[page].get());
        }
    }

    void updateEntityTexture(uint32_t layer, const uint8_t* pixels) override
    {
        uint32_t page = layer / EntityTexturePageLayers;
        if (layer >= entityLayers || !entityTextures[page]) {
            return;
        }
        device->waitIdle();
        uint32_t slot = layer % EntityTexturePageLayers;
        std::vector<std::vector<uint8_t>> mips = entityMips(pixels, entitySize, EntityMipLevels);
        std::vector<TextureData> data { { slot, 0, pixels } };
        for (size_t level = 0; level < mips.size(); ++level) {
            data.push_back({ slot, static_cast<uint32_t>(level + 1), mips[level].data() });
        }
        device->uploadTexture(*entityTextures[page], data);
    }

    void setChunkMesh(uint64_t id, int32_t originX, int32_t originY, int32_t originZ, const ChunkMeshUpload& mesh) override
    {
        removeChunkMesh(id);
        if (mesh.cubeCount == 0 && mesh.modelCount == 0 && mesh.translucentCubeCount == 0 && mesh.translucentModelCount == 0) {
            return;
        }
        ChunkBuffer chunk;
        const void* sources[4] = { mesh.cubes, mesh.models, mesh.translucentCubes, mesh.translucentModels };
        uint32_t counts[4] = { mesh.cubeCount, mesh.modelCount, mesh.translucentCubeCount, mesh.translucentModelCount };
        for (size_t stream = 0; stream < 4; ++stream) {
            size_t bytes = static_cast<size_t>(counts[stream]) * StreamStride[stream];
            if (bytes != 0) {
                chunk.buffers[stream] = device->createBuffer(bytes);
                std::memcpy(chunk.buffers[stream]->mapped(), sources[stream], bytes);
            }
            chunk.counts[stream] = counts[stream];
        }
        chunk.origin = { originX, originY, originZ };
        chunks.emplace(id, std::move(chunk));
    }

    void removeChunkMesh(uint64_t id) override
    {
        auto found = chunks.find(id);
        if (found == chunks.end()) {
            return;
        }
        retire(found->second);
        chunks.erase(found);
    }

    void clearChunkMeshes() override
    {
        for (auto& [id, chunk] : chunks) {
            retire(chunk);
        }
        chunks.clear();
    }

    void beginFrame(float r, float g, float b) override
    {
        uint64_t submission = device->beginFrame(r, g, b);
        uint64_t completed = device->completedSubmission();
        for (const CompletedFrame& frame : slotFrames) {
            if (frame.submission <= completed && frame.submission > completedReport.submission) {
                completedReport = frame;
            }
        }
        recording = { submission, 0 };
    }

    void drawWorld(const WorldView& view) override
    {
        if (!blockTextures[0] || !device->recording()) {
            return;
        }
        FrameBuffers& buffers = frames[device->frameSlot()];
        WorldConstants constants(view);
        auto bind = [&](const Pipeline& pipeline, float x, float y, float z) {
            device->setPipeline(pipeline);
            device->setTextures(*worldTextures);
            constants.setOrigin(x, y, z);
            device->setConstants(constants.values.data(), static_cast<uint32_t>(constants.values.size()));
        };
        auto drawStream = [&](const Pipeline& pipeline, const ChunkBuffer& chunk, size_t stream) {
            uint32_t count = chunk.counts[stream];
            if (count == 0) {
                return;
            }
            bind(pipeline, static_cast<float>(chunk.origin[0] - view.cameraX), static_cast<float>(chunk.origin[1] - view.cameraY), static_cast<float>(chunk.origin[2] - view.cameraZ));
            device->setVertexBuffer(*chunk.buffers[stream], StreamStride[stream], static_cast<size_t>(count) * StreamStride[stream]);
            device->draw(6, count, 0, 0);
        };

        if (view.backgroundCount) {
            size_t bytes = static_cast<size_t>(view.backgroundCount) * sizeof(SkyVertex);
            ensure(buffers.sky, bytes);
            std::memcpy(buffers.sky->mapped(), view.background, bytes);
            bind(*skyPipeline, 0.0f, 0.0f, 0.0f);
            device->setVertexBuffer(*buffers.sky, sizeof(SkyVertex), bytes);
            device->draw(view.backgroundCount, 1, 0, 0);
        }

        ChunkFrustum frustum(view);
        std::vector<const ChunkBuffer*> visible;
        visible.reserve(chunks.size());
        for (const auto& [id, chunk] : chunks) {
            if (frustum.contains(view, chunk.origin[0], chunk.origin[1], chunk.origin[2])) {
                visible.push_back(&chunk);
            }
        }
        for (const ChunkBuffer* chunk : visible) {
            drawStream(*cubePipeline, *chunk, 0);
        }
        for (const ChunkBuffer* chunk : visible) {
            drawStream(*modelPipeline, *chunk, 1);
        }
        recording.opaqueChunks = static_cast<uint32_t>(std::count_if(visible.begin(), visible.end(), [](const ChunkBuffer* chunk) {
            return chunk->counts[0] || chunk->counts[1];
        }));

        bool entities = view.entityTotal() && entityLayers > 0 && entityTextures[0];
        size_t entityBytes = static_cast<size_t>(view.entityTotal()) * ModelQuadBytes;
        if (entities) {
            ensure(buffers.entities, entityBytes);
            std::memcpy(buffers.entities->mapped(), view.entityQuads, entityBytes);
        }
        auto drawEntities = [&](const Pipeline& pipeline, uint32_t first, uint32_t count, float maxDepth) {
            if (!entities || count == 0) {
                return;
            }
            device->setDepthRange(maxDepth);
            bind(pipeline, view.entityOrigin[0], view.entityOrigin[1], view.entityOrigin[2]);
            device->setVertexBuffer(*buffers.entities, ModelQuadBytes, entityBytes);
            device->draw(6, count, 0, first);
            device->setDepthRange(1.0f);
        };
        drawEntities(*modelPipeline, 0, view.entityQuadCount, 1.0f);

        std::vector<std::pair<double, const ChunkBuffer*>> ordered;
        for (const ChunkBuffer* chunk : visible) {
            if (chunk->counts[2] || chunk->counts[3]) {
                double dx = chunk->origin[0] + 8.0 - view.cameraX;
                double dy = chunk->origin[1] + 8.0 - view.cameraY;
                double dz = chunk->origin[2] + 8.0 - view.cameraZ;
                ordered.emplace_back(dx * dx + dy * dy + dz * dz, chunk);
            }
        }
        std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
            return left.first > right.first;
        });
        for (const auto& [distance, chunk] : ordered) {
            drawStream(*modelBlendPipeline, *chunk, 3);
            drawStream(*cubeBlendPipeline, *chunk, 2);
        }
        drawEntities(*modelBlendPipeline, view.entityQuadCount, view.entityBlendCount, 1.0f);
        drawEntities(*overlayPipeline, view.overlayStart(), view.overlayQuadCount, 1.0f);
        drawEntities(*modelPipeline, view.entityQuadCount + view.entityBlendCount, view.handQuadCount, HandDepthRange);
    }

    void drawUi(const ui::DrawList& list) override
    {
        if (list.indices().empty() || !atlas || !device->recording()) {
            return;
        }
        FrameBuffers& buffers = frames[device->frameSlot()];
        size_t vertexBytes = list.vertices().size() * sizeof(ui::UiVertex);
        size_t indexBytes = list.indices().size() * sizeof(uint32_t);
        ensure(buffers.vertices, vertexBytes);
        ensure(buffers.indices, indexBytes);
        std::memcpy(buffers.vertices->mapped(), list.vertices().data(), vertexBytes);
        std::memcpy(buffers.indices->mapped(), list.indices().data(), indexBytes);

        const float viewport[2] = { static_cast<float>(device->width()), static_cast<float>(device->height()) };
        device->setPipeline(*uiPipeline);
        device->setTextures(*uiTextures);
        device->setConstants(viewport, 2);
        device->setVertexBuffer(*buffers.vertices, sizeof(ui::UiVertex), vertexBytes);
        device->setIndexBuffer(*buffers.indices, indexBytes);
        device->drawIndexed(static_cast<uint32_t>(list.indices().size()));
    }

    uint32_t createShader(const ShaderSource& source, CustomBlend blend, bool post, std::string& error) override
    {
        PipelineDesc desc;
        desc.source = &source;
        desc.vertexEntry = "vs_main";
        desc.pixelEntry = "ps_main";
        desc.vertices = customLayout();
        desc.bindings = { 32, false, post ? PostTextureCount : 1u, SamplerMode::PixelClamp };
        desc.blend = post ? BlendMode::None : customBlend(blend);
        desc.depthWrite = false;
        desc.depthCompare = DepthCompare::LessEqual;
        try {
            uint32_t id = nextShader++;
            customPipelines.emplace(id, CustomPipeline { device->createPipeline(desc), post });
            return id;
        } catch (const std::exception& failure) {
            error = failure.what();
            return 0;
        }
    }

    void destroyShader(uint32_t shader) override
    {
        auto found = customPipelines.find(shader);
        if (found == customPipelines.end()) {
            return;
        }
        device->waitIdle();
        customPipelines.erase(found);
    }

    void drawCustom(CustomLayer layer, const std::vector<CustomVertex>& vertices, const std::vector<CustomDraw>& draws, const std::array<float, 16>& transform, float seconds) override
    {
        if (draws.empty() || vertices.empty() || !device->recording()) {
            return;
        }
        std::unique_ptr<Buffer>& buffer = frames[device->frameSlot()].custom[static_cast<size_t>(layer)];
        size_t bytes = vertices.size() * sizeof(CustomVertex);
        ensure(buffer, bytes);
        std::memcpy(buffer->mapped(), vertices.data(), bytes);

        std::array<float, 32> constants {};
        std::copy(transform.begin(), transform.end(), constants.begin());
        constants[16] = seconds;
        constants[17] = static_cast<float>(device->width());
        constants[18] = static_cast<float>(device->height());
        for (const CustomDraw& draw : draws) {
            auto found = customPipelines.find(draw.shader);
            if (found == customPipelines.end() || found->second.post || draw.vertexCount == 0 || size_t(draw.firstVertex) + draw.vertexCount > vertices.size()) {
                continue;
            }
            std::copy(draw.params.begin(), draw.params.end(), constants.begin() + 20);
            device->setPipeline(*found->second.pipeline);
            device->setTextures(*uiTextures);
            device->setConstants(constants.data(), static_cast<uint32_t>(constants.size()));
            device->setVertexBuffer(*buffer, sizeof(CustomVertex), bytes);
            device->draw(draw.vertexCount, 1, draw.firstVertex, 0);
        }
    }

    bool supportsPostProcess() const override
    {
        return device->supportsSceneCopy();
    }

    void drawPost(const std::vector<CustomDraw>& passes, const std::array<float, 16>& inverseViewProjection, float seconds) override
    {
        if (passes.empty() || !device->recording() || !device->supportsSceneCopy()) {
            return;
        }
        // A full screen quad, clip space with y up and uvs from the top left.
        static constexpr CustomVertex Quad[6] = {
            { -1.0f, 1.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f, 1.0f, 0.0f }, { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f },
            { -1.0f, 1.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f }, { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
        };
        std::unique_ptr<Buffer>& buffer = frames[device->frameSlot()].postQuad;
        ensure(buffer, sizeof(Quad));
        std::memcpy(buffer->mapped(), Quad, sizeof(Quad));

        std::array<float, 32> constants {};
        std::copy(inverseViewProjection.begin(), inverseViewProjection.end(), constants.begin());
        constants[16] = seconds;
        constants[17] = static_cast<float>(device->width());
        constants[18] = static_cast<float>(device->height());
        bool first = true;
        for (const CustomDraw& pass : passes) {
            auto found = customPipelines.find(pass.shader);
            if (found == customPipelines.end() || !found->second.post) {
                continue;
            }
            device->copyScene(first, pass.keepInput);
            first = false;
            std::copy(pass.params.begin(), pass.params.end(), constants.begin() + 20);
            device->setPipeline(*found->second.pipeline);
            device->setTextures(*device->sceneTextures());
            device->setConstants(constants.data(), static_cast<uint32_t>(constants.size()));
            device->setVertexBuffer(*buffer, sizeof(CustomVertex), sizeof(Quad));
            device->draw(6, 1, 0, 0);
        }
    }

    void endFrame() override
    {
        if (device->recording()) {
            slotFrames[device->frameSlot()] = recording;
        }
        device->endFrame();
    }

    bool requestCapture() override
    {
        return device->requestCapture();
    }

    bool takeCapture(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height) override
    {
        return device->takeCapture(rgba, width, height);
    }

private:
    struct ChunkBuffer {
        std::array<std::unique_ptr<Buffer>, 4> buffers;
        std::array<uint32_t, 4> counts {};
        std::array<int32_t, 3> origin {};
    };

    struct FrameBuffers {
        std::unique_ptr<Buffer> vertices;
        std::unique_ptr<Buffer> indices;
        std::unique_ptr<Buffer> sky;
        std::unique_ptr<Buffer> entities;
        std::array<std::unique_ptr<Buffer>, CustomLayerCount> custom;
        std::unique_ptr<Buffer> postQuad;
    };

    struct CustomPipeline {
        std::unique_ptr<Pipeline> pipeline;
        bool post = false;
    };

    static constexpr uint32_t PostTextureCount = 4;

    void retire(ChunkBuffer& chunk)
    {
        for (std::unique_ptr<Buffer>& buffer : chunk.buffers) {
            if (buffer) {
                device->retire(std::move(buffer));
            }
        }
    }

    void ensure(std::unique_ptr<Buffer>& buffer, size_t needed)
    {
        size_t capacity = buffer ? buffer->size() : 0;
        if (capacity >= needed) {
            return;
        }
        capacity = std::max<size_t>({ needed, capacity * 2, 64 * 1024 });
        if (buffer) {
            device->retire(std::move(buffer));
        }
        buffer = device->createBuffer(capacity);
    }

    std::unique_ptr<Device> device;
    std::unique_ptr<Pipeline> uiPipeline;
    std::unique_ptr<Pipeline> cubePipeline;
    std::unique_ptr<Pipeline> modelPipeline;
    std::unique_ptr<Pipeline> cubeBlendPipeline;
    std::unique_ptr<Pipeline> modelBlendPipeline;
    std::unique_ptr<Pipeline> overlayPipeline;
    std::unique_ptr<Pipeline> skyPipeline;
    std::unique_ptr<TextureSet> uiTextures;
    std::unique_ptr<TextureSet> worldTextures;
    std::unique_ptr<Texture> atlas;
    std::array<std::unique_ptr<Texture>, BlockTexturePages> blockTextures;
    std::array<std::unique_ptr<Texture>, EntityTexturePages> entityTextures;
    uint32_t entitySize = 0;
    uint32_t entityLayers = 0;
    std::unordered_map<uint64_t, ChunkBuffer> chunks;
    std::unordered_map<uint32_t, CustomPipeline> customPipelines;
    uint32_t nextShader = 1;
    std::vector<FrameBuffers> frames;
    std::vector<CompletedFrame> slotFrames;
    CompletedFrame recording;
    CompletedFrame completedReport;
};

}

std::unique_ptr<Renderer> createRenderer(std::unique_ptr<Device> device)
{
    return std::make_unique<RhiRenderer>(std::move(device));
}

}
