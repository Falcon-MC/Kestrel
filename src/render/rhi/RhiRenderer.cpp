#include "render/rhi/Device.h"
#include "render/Renderer.h"
#include "render/OpaqueTerrain.h"
#include "world/ChunkVisibility.h"

#include "client/DebugLog.h"
#include "ui/DrawList.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kestrel::rhi {

namespace {

constexpr uint32_t StreamStride[4] = { CubeQuadBytes, ModelQuadBytes, CubeQuadBytes, ModelQuadBytes };
constexpr uint32_t WorldTextureCount = BlockTexturePages + EntityTexturePages;
constexpr uint32_t EntityMipLevels = 5;
// Blocks the camera may drift before translucent terrain is sorted again.
constexpr double TransparentResortDistance = 1.0;

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
        auto solidDesc = worldPipeline("vs_world", "ps_solid", cubeLayout(), BlendMode::None, true, DepthCompare::Less);
        solidDesc.cullBackFaces = true;
        solidCubePipeline = device->createPipeline(solidDesc);
        modelPipeline = device->createPipeline(worldPipeline("vs_model", "ps_world", modelLayout(), BlendMode::None, true, DepthCompare::Less));
        solidModelPipeline = device->createPipeline(worldPipeline("vs_model", "ps_solid", modelLayout(), BlendMode::None, true, DepthCompare::Less));
        auto actorDesc = worldPipeline("vs_actor", "ps_world", modelLayout(), BlendMode::None, true, DepthCompare::Less);
        actorDesc.bindings.actorConstants = true;
        actorPipeline = device->createPipeline(actorDesc);
        actorDesc.pixelEntry = "ps_blend";
        actorDesc.blend = BlendMode::Premultiplied;
        actorDesc.depthWrite = false;
        actorBlendPipeline = device->createPipeline(actorDesc);
        actorDesc.pixelEntry = "ps_world";
        actorDesc.blend = BlendMode::None;
        actorDesc.depthWrite = true;
        actorDesc.colorWrite = false;
        actorDepthPipeline = device->createPipeline(actorDesc);
        actorDesc.colorWrite = true;
        actorDesc.depthWrite = false;
        actorDesc.depthCompare = DepthCompare::Equal;
        actorDissolvePipeline = device->createPipeline(actorDesc);
        modelBlendPipeline = device->createPipeline(worldPipeline("vs_model", "ps_blend", modelLayout(), BlendMode::Premultiplied, false, DepthCompare::Less));
        cubeBlendPipeline = device->createPipeline(worldPipeline("vs_world", "ps_blend", cubeLayout(), BlendMode::Premultiplied, false, DepthCompare::Less));
        skyPipeline = device->createPipeline(worldPipeline("vs_sky", "ps_sky", skyLayout(), BlendMode::Premultiplied, false, DepthCompare::Less));
        overlayPipeline = device->createPipeline(worldPipeline("vs_overlay", "ps_overlay", modelLayout(), BlendMode::Multiply, false, DepthCompare::LessEqual));

        uiTextures = device->createTextureSet(1, false, SamplerMode::PixelClamp);
        worldTextures = device->createTextureSet(WorldTextureCount, true, SamplerMode::TerrainWrap);
        // Index 5 selects corner 3 in the existing shaders while shared indices reuse the other corners.
        static constexpr uint32_t QuadIndices[6] = { 0, 1, 2, 0, 2, 5 };
        quadIndices = device->createPersistentBuffer(sizeof(QuadIndices));
        device->uploadBuffer(*quadIndices, QuadIndices, sizeof(QuadIndices));

        PipelineDesc primitive;
        primitive.library = ShaderLibrary::Primitive;
        primitive.vertexEntry = "vs_primitive";
        primitive.pixelEntry = "ps_primitive";
        primitive.vertices = customLayout();
        primitive.bindings = { 32, false, 1, SamplerMode::PixelClamp };
        primitive.blend = BlendMode::Alpha;
        primitive.depthWrite = false;
        primitive.depthCompare = DepthCompare::LessEqual;
        builtinShaders[static_cast<size_t>(CustomBuiltin::Primitive)] = nextShader;
        customPipelines.emplace(nextShader++, CustomPipeline { device->createPipeline(primitive), false });
        primitive.depthCompare = DepthCompare::Always;
        builtinShaders[static_cast<size_t>(CustomBuiltin::PrimitiveOverlay)] = nextShader;
        customPipelines.emplace(nextShader++, CustomPipeline { device->createPipeline(primitive), false });
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
        if (!atlas || atlasWidth != width || atlasHeight != height) {
            device->waitIdle();
            atlas = device->createTexture({ width, height, 1, 1, false });
            device->uploadTexture(*atlas, { { 0, 0, pixels } });
            uiTextures->bind(0, atlas.get());
            atlasWidth = width;
            atlasHeight = height;
        } else {
            device->uploadTextureAsync(*atlas, { { 0, 0, pixels } });
        }
    }

    void updateUiAtlas(const uint8_t* pixels, uint32_t width, uint32_t height, const std::vector<ui::ImageRegion>& regions) override
    {
        if (!atlas || atlasWidth != width || atlasHeight != height) {
            uploadUiAtlas(pixels, width, height);
            return;
        }
        std::vector<TextureData> updates;
        updates.reserve(regions.size());
        for (const auto& region : regions) {
            if (!region.width || !region.height || region.x >= width || region.y >= height
                || region.width > width - region.x || region.height > height - region.y) continue;
            updates.push_back({ 0, 0, pixels + (size_t(region.y) * width + region.x) * 4,
                region.x, region.y, region.width, region.height, size_t(width) * 4 });
        }
        device->uploadTextureAsync(*atlas, updates);
    }

    void uploadBlockTextures(const BlockTextureUpload& textures) override
    {
        if (textures.layers == 0) {
            return;
        }
        device->waitIdle();
        opaqueLayers = opaqueTextureLayers(textures);
        ++textureRevision;
        visibilityGraph.clear();
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
        uint32_t slot = layer % EntityTexturePageLayers;
        std::vector<std::vector<uint8_t>> mips = entityMips(pixels, entitySize, EntityMipLevels);
        std::vector<TextureData> data { { slot, 0, pixels } };
        for (size_t level = 0; level < mips.size(); ++level) {
            data.push_back({ slot, static_cast<uint32_t>(level + 1), mips[level].data() });
        }
        device->uploadTextureAsync(*entityTextures[page], data);
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
        OpaqueCubeRuns cubeRuns = partitionCubeQuads(mesh.cubes, mesh.cubeCount, opaqueLayers);
        sources[0] = cubeRuns.words.data();
        OpaqueModelRuns modelRuns = partitionModelQuads(mesh.models, mesh.modelCount, opaqueLayers);
        sources[1] = modelRuns.words.data();
        chunk.solidModels = modelRuns.solidCount;
        chunk.cubeOffsets = cubeRuns.offsets;
        chunk.textureRevision = textureRevision;
        for (size_t stream = 0; stream < 4; ++stream) {
            size_t bytes = static_cast<size_t>(counts[stream]) * StreamStride[stream];
            if (bytes != 0) {
                if (stream < 2) {
                    chunk.buffers[stream] = acquirePersistent(bytes);
                    device->uploadBuffer(*chunk.buffers[stream], sources[stream], bytes);
                } else {
                    auto& centers = chunk.centers[stream - 2];
                    centers.resize(counts[stream]);
                    const auto* data = static_cast<const uint8_t*>(sources[stream]);
                    chunk.translucent[stream - 2].assign(data, data + bytes);
                    for (size_t quad = 0; quad < counts[stream]; ++quad) centers[quad] = quadCenter(data + quad * StreamStride[stream], stream == 2);
                }
            }
            chunk.counts[stream] = counts[stream];
        }
        chunk.origin = { originX, originY, originZ };
        chunks.emplace(id, std::move(chunk));
        ++meshRevision;
    }

    void removeChunkMesh(uint64_t id) override
    {
        auto found = chunks.find(id);
        if (found == chunks.end()) {
            return;
        }
        retire(found->second);
        chunks.erase(found);
        ++meshRevision;
    }

    void clearChunkMeshes() override
    {
        for (auto& [id, chunk] : chunks) {
            retire(chunk);
        }
        chunks.clear();
        visibilityGraph.clear();
        ++meshRevision;
    }

    void setChunkVisibility(int32_t x, int32_t y, int32_t z, std::shared_ptr<const world::ChunkVisibility> visibility) override
    {
        visibilityGraph.set({ x, y, z }, std::move(visibility));
    }

    void beginFrame(float r, float g, float b) override
    {
        uint64_t submission = device->beginFrame(r, g, b);
        uint64_t completed = device->completedSubmission();
        collectPool();
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
        device->setIndexBuffer(*quadIndices, 6 * sizeof(uint32_t));
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
            device->drawIndexed(6, count, 0);
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
        visibilityGraph.update({ view.cameraX, view.cameraY, view.cameraZ });
        visible.clear();
        visible.reserve(chunks.size());
        for (const auto& [id, chunk] : chunks) {
            if (frustum.contains(view, chunk.origin[0], chunk.origin[1], chunk.origin[2])
                && visibilityGraph.contains({ chunk.origin[0] / 16, chunk.origin[1] / 16, chunk.origin[2] / 16 })) {
                visible.push_back(&chunk);
            }
        }
        for (const ChunkBuffer* chunk : visible) {
            if (chunk->textureRevision != textureRevision) {
                drawStream(*cubePipeline, *chunk, 0);
                continue;
            }
            if (!chunk->counts[0]) continue;
            device->setVertexBuffer(*chunk->buffers[0], CubeQuadBytes, size_t(chunk->counts[0]) * CubeQuadBytes);
            if (chunk->cubeOffsets[6]) {
                bind(*solidCubePipeline, float(chunk->origin[0] - view.cameraX), float(chunk->origin[1] - view.cameraY), float(chunk->origin[2] - view.cameraZ));
                uint8_t facing = facingCubeDirections(chunk->origin, { view.cameraX, view.cameraY, view.cameraZ });
                drawSolidCubeRuns(chunk->cubeOffsets, facing, [&](uint32_t first, uint32_t count) { device->drawIndexed(6, count, first); });
            }
            uint32_t first = chunk->cubeOffsets[6];
            if (first < chunk->counts[0]) {
                bind(*cubePipeline, float(chunk->origin[0] - view.cameraX), float(chunk->origin[1] - view.cameraY), float(chunk->origin[2] - view.cameraZ));
                device->drawIndexed(6, chunk->counts[0] - first, first);
            }
        }
        for (const ChunkBuffer* chunk : visible) {
            if (chunk->textureRevision != textureRevision) {
                drawStream(*modelPipeline, *chunk, 1);
                continue;
            }
            if (!chunk->counts[1]) continue;
            device->setVertexBuffer(*chunk->buffers[1], ModelQuadBytes, size_t(chunk->counts[1]) * ModelQuadBytes);
            if (chunk->solidModels) {
                bind(*solidModelPipeline, float(chunk->origin[0] - view.cameraX), float(chunk->origin[1] - view.cameraY), float(chunk->origin[2] - view.cameraZ));
                device->drawIndexed(6, chunk->solidModels);
            }
            if (chunk->solidModels < chunk->counts[1]) {
                bind(*modelPipeline, float(chunk->origin[0] - view.cameraX), float(chunk->origin[1] - view.cameraY), float(chunk->origin[2] - view.cameraZ));
                device->drawIndexed(6, chunk->counts[1] - chunk->solidModels, chunk->solidModels);
            }
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
            device->drawIndexed(6, count, first);
            device->setDepthRange(1.0f);
        };
        drawEntities(*modelPipeline, 0, view.entityQuadCount, 1.0f);
        auto drawActor = [&](uint32_t index) {
            const ActorDraw& draw = view.actorDraws[index];
            if (!draw.count || !draw.total || !entityLayers) return;
            auto& mesh = actorMeshes[draw.geometryKey];
            if (!mesh.buffer) {
                mesh.buffer = acquirePersistent(size_t(draw.total) * ModelQuadBytes);
                device->uploadBuffer(*mesh.buffer, draw.quads, size_t(draw.total) * ModelQuadBytes);
            }
            mesh.used = recording.submission;
            const Pipeline& pipeline = draw.depthOnly ? *actorDepthPipeline : draw.equalDepth ? *actorDissolvePipeline : draw.blended ? *actorBlendPipeline : *actorPipeline;
            bind(pipeline, view.entityOrigin[0], view.entityOrigin[1], view.entityOrigin[2]);
            auto constants = draw.constants;
            device->setActorConstants(constants.data(), static_cast<uint32_t>(constants.size()));
            device->setVertexBuffer(*mesh.buffer, ModelQuadBytes, size_t(draw.total) * ModelQuadBytes);
            device->drawIndexed(6, draw.count, draw.first);
        };
        for (uint32_t index = 0; index < view.actorDrawCount; ++index) {
            if (!view.actorDraws[index].blended) drawActor(index);
        }


        // Distance rather than view depth, so turning the camera never asks for a new sort.
        auto depth = [](const std::array<float, 3>& point) {
            return point[0] * point[0] + point[1] * point[1] + point[2] * point[2];
        };
        const std::array<double, 3> camera { view.cameraX, view.cameraY, view.cameraZ };
        double movedX = camera[0] - cachedCamera[0];
        double movedY = camera[1] - cachedCamera[1];
        double movedZ = camera[2] - cachedCamera[2];
        // a flying camera never sits perfectly still, so resorting on every tiny drift redid the whole sort each frame
        bool moved = movedX * movedX + movedY * movedY + movedZ * movedZ > TransparentResortDistance * TransparentResortDistance;
        if (!terrainCacheValid || cachedMeshRevision != meshRevision || moved || cachedVisible != visible) {
            terrainTransparent.clear();
            for (const ChunkBuffer* chunk : visible) {
                std::array<float, 3> relative {
                    float(chunk->origin[0] - view.cameraX), float(chunk->origin[1] - view.cameraY), float(chunk->origin[2] - view.cameraZ)
                };
                for (size_t stream = 2; stream < 4; ++stream) {
                    const auto& centers = chunk->centers[stream - 2];
                    for (uint32_t quad = 0; quad < centers.size(); ++quad) {
                        auto point = centers[quad];
                        for (size_t axis = 0; axis < 3; ++axis) point[axis] += relative[axis];
                        terrainTransparent.push_back({ depth(point), chunk, uint32_t(stream), quad });
                    }
                }
            }
            std::sort(terrainTransparent.begin(), terrainTransparent.end(), transparentBefore);
            terrainCounts = {};
            for (const auto& quad : terrainTransparent) ++terrainCounts[quad.stream - 2];
            cachedCamera = camera;
            cachedVisible = visible;
            cachedMeshRevision = meshRevision;
            terrainCacheValid = true;
            ++terrainGeneration;
        }
        actorTransparent.clear();
        const auto* entityData = static_cast<const uint8_t*>(view.entityQuads);
        for (uint32_t quad = 0; entities && quad < view.entityBlendCount; ++quad) {
            uint32_t index = view.entityQuadCount + quad;
            auto point = quadCenter(entityData + size_t(index) * ModelQuadBytes, false);
            for (size_t axis = 0; axis < 3; ++axis) point[axis] += view.entityOrigin[axis];
            actorTransparent.push_back({ depth(point), nullptr, 3, index });
        }
        for (uint32_t index = 0; entityLayers && index < view.actorDrawCount; ++index) {
            const ActorDraw& draw = view.actorDraws[index];
            if (!draw.blended) continue;
            auto point = draw.center;
            for (size_t axis = 0; axis < 3; ++axis) point[axis] += view.entityOrigin[axis];
            actorTransparent.push_back({ depth(point), nullptr, 4, index });
        }
        std::sort(actorTransparent.begin(), actorTransparent.end(), transparentBefore);
        std::array<size_t, 2> sortedCounts = terrainCounts;
        if (actorTransparent.empty()) {
            if (terrainDataGeneration != terrainGeneration) {
                for (size_t stream = 0; stream < 2; ++stream) terrainData[stream].resize(terrainCounts[stream] * StreamStride[stream + 2]);
                terrainDraws = terrainTransparent;
                std::array<uint32_t, 2> offsets {};
                for (auto& quad : terrainDraws) {
                    size_t stream = quad.stream - 2;
                    size_t stride = StreamStride[quad.stream];
                    std::memcpy(terrainData[stream].data() + size_t(offsets[stream]) * stride,
                        quad.chunk->translucent[stream].data() + size_t(quad.quad) * stride, stride);
                    quad.quad = offsets[stream]++;
                }
                terrainDataGeneration = terrainGeneration;
            }
            if (buffers.terrainGeneration != terrainGeneration) {
                for (size_t stream = 0; stream < 2; ++stream) {
                    if (terrainData[stream].empty()) continue;
                    ensure(buffers.translucent[stream], terrainData[stream].size());
                    std::memcpy(buffers.translucent[stream]->mapped(), terrainData[stream].data(), terrainData[stream].size());
                }
                buffers.terrainGeneration = terrainGeneration;
            }
        } else {
            transparent.resize(terrainTransparent.size() + actorTransparent.size());
            std::merge(terrainTransparent.begin(), terrainTransparent.end(), actorTransparent.begin(), actorTransparent.end(), transparent.begin(), transparentBefore);
            sortedCounts[1] += std::count_if(actorTransparent.begin(), actorTransparent.end(), [](const auto& quad) { return quad.stream == 3; });
            std::array<uint8_t*, 2> sortedData {};
            for (size_t stream = 0; stream < sortedCounts.size(); ++stream) {
                if (sortedCounts[stream] == 0) continue;
                ensure(buffers.translucent[stream], sortedCounts[stream] * StreamStride[stream + 2]);
                sortedData[stream] = static_cast<uint8_t*>(buffers.translucent[stream]->mapped());
            }
            std::array<uint32_t, 2> offsets {};
            for (auto& quad : transparent) {
                if (quad.stream == 4) continue;
                size_t stream = quad.stream - 2;
                size_t stride = StreamStride[quad.stream];
                const uint8_t* source = quad.chunk ? quad.chunk->translucent[stream].data() : entityData;
                std::memcpy(sortedData[stream] + size_t(offsets[stream]) * stride, source + size_t(quad.quad) * stride, stride);
                quad.quad = offsets[stream]++;
            }
            buffers.terrainGeneration = 0;
        }
        const auto& sorted = actorTransparent.empty() ? terrainDraws : transparent;
        for (size_t index = 0; index < sorted.size();) {
            const auto& first = sorted[index];
            if (first.stream == 4) {
                drawActor(first.quad);
                ++index;
                continue;
            }
            uint32_t count = 1;
            while (index + count < sorted.size()) {
                const auto& next = sorted[index + count];
                if (next.chunk != first.chunk || next.stream != first.stream || next.quad != first.quad + count) break;
                ++count;
            }
            if (first.chunk) {
                const auto& chunk = *first.chunk;
                bind(first.stream == 2 ? *cubeBlendPipeline : *modelBlendPipeline, float(chunk.origin[0] - view.cameraX), float(chunk.origin[1] - view.cameraY), float(chunk.origin[2] - view.cameraZ));
            } else {
                bind(*modelBlendPipeline, view.entityOrigin[0], view.entityOrigin[1], view.entityOrigin[2]);
            }
            size_t stream = first.stream - 2;
            device->setVertexBuffer(*buffers.translucent[stream], StreamStride[first.stream], sortedCounts[stream] * StreamStride[first.stream]);
            device->drawIndexed(6, count, first.quad);
            index += count;
        }
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
        if (found == customPipelines.end() || std::find(builtinShaders.begin(), builtinShaders.end(), shader) != builtinShaders.end()) {
            return;
        }
        device->waitIdle();
        customPipelines.erase(found);
    }

    uint32_t builtinShader(CustomBuiltin builtin) const override
    {
        return builtinShaders[static_cast<size_t>(builtin)];
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

    void setVsync(bool enabled) override
    {
        device->setVsync(enabled);
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
        std::array<uint32_t, 8> cubeOffsets {};
        uint64_t textureRevision = 0;
        uint32_t solidModels = 0;
        std::array<std::vector<std::array<float, 3>>, 2> centers;
        std::array<std::vector<uint8_t>, 2> translucent;
    };

    struct FrameBuffers {
        std::unique_ptr<Buffer> vertices;
        std::unique_ptr<Buffer> indices;
        std::unique_ptr<Buffer> sky;
        std::unique_ptr<Buffer> entities;
        std::array<std::unique_ptr<Buffer>, 2> translucent;
        uint64_t terrainGeneration = 0;
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
                pool.push_back({ std::move(buffer), device->submittedFrames() + 1 });
            }
        }
    }

    struct PooledBuffer {
        std::unique_ptr<Buffer> buffer;
        uint64_t safeAfter;
    };
    struct ActorMesh {
        std::unique_ptr<Buffer> buffer;
        uint64_t used = 0;
    };
    struct TransparentQuad {
        float depth;
        const ChunkBuffer* chunk;
        uint32_t stream;
        uint32_t quad;
    };

    static bool transparentBefore(const TransparentQuad& a, const TransparentQuad& b)
    {
        if (a.depth != b.depth) return a.depth > b.depth;
        if (a.chunk != b.chunk) return std::less<const ChunkBuffer*> {}(a.chunk, b.chunk);
        if (a.stream != b.stream) return a.stream < b.stream;
        return a.quad < b.quad;
    }

    static std::array<float, 3> quadCenter(const void* data, bool cube)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint32_t words[16] {};
        std::memcpy(words, bytes, cube ? CubeQuadBytes : ModelQuadBytes);
        std::array<float, 3> center {};
        if (cube) {
            uint32_t geometry = words[0];
            center = { float(geometry & 31), float((geometry >> 5) & 31), float((geometry >> 10) & 31) };
            uint32_t face = (geometry >> 15) & 7;
            float width = float(((geometry >> 18) & 15) + 1);
            float height = float(((geometry >> 22) & 15) + 1);
            if (face < 2) { center[0] += face == 1 ? 1.0f : 0.0f; center[1] += height * .5f; center[2] += width * .5f; }
            else if (face < 4) { center[0] += width * .5f; center[1] += face == 3 ? 1.0f : 0.0f; center[2] += height * .5f; }
            else { center[0] += width * .5f; center[1] += height * .5f; center[2] += face == 5 ? 1.0f : 0.0f; }
        } else {
            for (size_t component = 0; component < 12; ++component) {
                uint16_t packed = uint16_t(words[component / 2] >> ((component & 1) * 16));
                center[component % 3] += float(static_cast<int16_t>(packed)) / 1024.0f;
            }
            if (words[13] & 0x80000000u) {
                for (size_t axis = 0; axis < 3; ++axis) {
                    int32_t packed = int32_t((words[13] >> (axis * 10)) & 1023u);
                    if (packed & 512) packed -= 1024;
                    center[axis] += float(packed) * 64.0f;
                }
            }
        }
        return center;
    }

    std::unique_ptr<Buffer> acquirePersistent(size_t bytes)
    {
        auto best = pool.end();
        uint64_t completed = device->completedSubmission();
        for (auto it = pool.begin(); it != pool.end(); ++it) {
            if (it->safeAfter > completed || it->buffer->size() < bytes) continue;
            if (best == pool.end() || it->buffer->size() < best->buffer->size()) best = it;
        }
        if (best != pool.end()) {
            auto buffer = std::move(best->buffer);
            pool.erase(best);
            return buffer;
        }
        return device->createPersistentBuffer((bytes + 255) & ~size_t(255));
    }

    void collectPool()
    {
        uint64_t completed = device->completedSubmission();
        size_t bytes = 0;
        for (const auto& entry : pool) bytes += entry.buffer->size();
        size_t destroyed = 0;
        for (size_t index = 0; index < pool.size() && destroyed < 32;) {
            if (bytes <= 64 * 1024 * 1024) break;
            if (pool[index].safeAfter > completed) {
                ++index;
            } else {
                bytes -= pool[index].buffer->size();
                if (index != pool.size() - 1) std::swap(pool[index], pool.back());
                pool.pop_back();
                ++destroyed;
            }
        }
        size_t actorBytes = 0;
        for (const auto& [key, mesh] : actorMeshes) actorBytes += mesh.buffer->size();
        uint64_t previousSubmission = device->submittedFrames();
        for (auto it = actorMeshes.begin(); it != actorMeshes.end();) {
            bool expired = it->second.used + 120 < completed;
            bool overBudget = actorBytes > 128 * 1024 * 1024 && it->second.used < previousSubmission;
            if (expired || overBudget) {
                actorBytes -= it->second.buffer->size();
                pool.push_back({ std::move(it->second.buffer), device->submittedFrames() + 1 });
                it = actorMeshes.erase(it);
            } else {
                ++it;
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
    std::unique_ptr<Pipeline> actorPipeline;
    std::unique_ptr<Pipeline> actorBlendPipeline;
    std::unique_ptr<Pipeline> actorDepthPipeline;
    std::unique_ptr<Pipeline> actorDissolvePipeline;
    std::unique_ptr<Pipeline> cubeBlendPipeline;
    std::unique_ptr<Pipeline> modelBlendPipeline;
    std::unique_ptr<Pipeline> overlayPipeline;
    std::unique_ptr<Pipeline> skyPipeline;
    std::unique_ptr<TextureSet> uiTextures;
    std::unique_ptr<TextureSet> worldTextures;
    std::unique_ptr<Buffer> quadIndices;
    std::unique_ptr<Texture> atlas;
    uint32_t atlasWidth = 0;
    uint32_t atlasHeight = 0;
    std::array<std::unique_ptr<Texture>, BlockTexturePages> blockTextures;
    std::unique_ptr<Pipeline> solidCubePipeline;
    std::unique_ptr<Pipeline> solidModelPipeline;
    std::vector<uint8_t> opaqueLayers;
    uint64_t textureRevision = 0;
    std::array<std::unique_ptr<Texture>, EntityTexturePages> entityTextures;
    uint32_t entitySize = 0;
    uint32_t entityLayers = 0;
    std::unordered_map<uint64_t, ChunkBuffer> chunks;
    world::ChunkVisibilityGraph visibilityGraph;
    std::vector<PooledBuffer> pool;
    std::vector<const ChunkBuffer*> visible;
    std::vector<TransparentQuad> transparent;
    std::vector<TransparentQuad> terrainTransparent;
    std::vector<TransparentQuad> actorTransparent;
    std::vector<TransparentQuad> terrainDraws;
    std::array<std::vector<uint8_t>, 2> terrainData;
    std::array<size_t, 2> terrainCounts {};
    std::array<double, 3> cachedCamera {};
    std::vector<const ChunkBuffer*> cachedVisible;
    uint64_t meshRevision = 0;
    uint64_t cachedMeshRevision = 0;
    uint64_t terrainGeneration = 0;
    uint64_t terrainDataGeneration = 0;
    bool terrainCacheValid = false;
    std::unordered_map<uint64_t, ActorMesh> actorMeshes;
    std::unordered_map<uint32_t, CustomPipeline> customPipelines;
    uint32_t nextShader = 1;
    std::array<uint32_t, 2> builtinShaders {};
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
