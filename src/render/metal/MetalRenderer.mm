#include "render/Renderer.h"

#include "platform/Window.h"
#include "ui/DrawList.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace kestrel {

namespace {

constexpr char UiShader[] = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
    float2 local [[attribute(3)]];
    float2 halfSize [[attribute(4)]];
    float2 shape [[attribute(5)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
    float2 local;
    float2 halfSize;
    float2 shape;
};

vertex VertexOut ui_vertex(VertexIn in [[stage_in]], constant float2& viewport [[buffer(1)]])
{
    VertexOut out;
    out.position = float4(in.position.x / viewport.x * 2.0 - 1.0, 1.0 - in.position.y / viewport.y * 2.0, 0.0, 1.0);
    out.uv = in.uv;
    out.color = in.color;
    out.local = in.local;
    out.halfSize = in.halfSize;
    out.shape = in.shape;
    return out;
}

fragment float4 ui_fragment(VertexOut in [[stage_in]], texture2d<float> atlas [[texture(0)]], sampler atlasSampler [[sampler(0)]])
{
    float4 texel = atlas.sample(atlasSampler, in.uv);
    float coverage = texel.a;
    if (in.shape.x >= 0.0) {
        float radius = in.shape.x;
        float2 q = abs(in.local) - in.halfSize + radius;
        float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
        coverage *= saturate(0.5 - distance / max(in.shape.y, 1.0));
    }
    return float4(in.color.rgb * texel.rgb, in.color.a * coverage);
}
)";

constexpr char WorldShader[] = R"(
#include <metal_stdlib>
using namespace metal;

struct WorldIn {
    uint4 quad [[attribute(0)]];
    uint ao [[attribute(1)]];
};

struct DrawData {
    float4x4 viewProjection;
    float4 origin;
    float4 fog;
    float4 params;
    float4 sun;
};

struct WorldOut {
    float4 position [[position]];
    float2 uv;
    uint material [[flat]];
    float shade;
    float3 relative;
    uint tint [[flat]];
    float3 light;
    uint entity [[flat]];
};

constant float lightCurve[16] = {
    0.0, 0.01754386, 0.037037037, 0.05882353,
    0.083333336, 0.11111111, 0.14285715, 0.17948718,
    0.22222222, 0.27272728, 0.33333334, 0.4074074,
    0.5, 0.61904764, 0.7777778, 1.0 };

float3 cornerLight(uint light, uint ao, uint corner)
{
    uint levels = (light >> (corner * 8)) & 0xff;
    float occlusion = 1.0 - float((ao >> (corner * 2)) & 3) * 0.12;
    return float3(lightCurve[levels & 0xf], lightCurve[levels >> 4], occlusion);
}

float4 applyTint(float4 texel, uint tint)
{
    if ((tint & 0x80000000u) == 0) {
        return texel;
    }
    float3 color = float3(float((tint >> 16) & 0xff), float((tint >> 8) & 0xff), float(tint & 0xff)) / 255.0;
    if ((tint & 0x40000000u) != 0) {
        return float4(mix(texel.rgb, texel.rgb * color, texel.a), 1.0);
    }
    return float4(texel.rgb * color, texel.a);
}

float3 quadCorner(uint face, uint corner, float3 o, float w, float h)
{
    float3 c[4];
    if (face == 0) {
        c[0] = o; c[1] = o + float3(0, 0, w); c[2] = o + float3(0, h, w); c[3] = o + float3(0, h, 0);
    } else if (face == 1) {
        float3 b = o + float3(1, 0, 0);
        c[0] = b; c[1] = b + float3(0, h, 0); c[2] = b + float3(0, h, w); c[3] = b + float3(0, 0, w);
    } else if (face == 2) {
        c[0] = o; c[1] = o + float3(w, 0, 0); c[2] = o + float3(w, 0, h); c[3] = o + float3(0, 0, h);
    } else if (face == 3) {
        float3 b = o + float3(0, 1, 0);
        c[0] = b; c[1] = b + float3(0, 0, h); c[2] = b + float3(w, 0, h); c[3] = b + float3(w, 0, 0);
    } else if (face == 4) {
        c[0] = o; c[1] = o + float3(0, h, 0); c[2] = o + float3(w, h, 0); c[3] = o + float3(w, 0, 0);
    } else {
        float3 b = o + float3(0, 0, 1);
        c[0] = b; c[1] = b + float3(w, 0, 0); c[2] = b + float3(w, h, 0); c[3] = b + float3(0, h, 0);
    }
    return c[corner];
}

float2 greedyUv(uint face, uint corner, float w, float h, uint flags)
{
    float2 horizontalStandard[4] = { float2(0, 0), float2(w, 0), float2(w, h), float2(0, h) };
    float2 horizontalTransposed[4] = { float2(0, 0), float2(0, h), float2(w, h), float2(w, 0) };
    float2 verticalStandard[4] = { float2(0, h), float2(w, h), float2(w, 0), float2(0, 0) };
    float2 verticalTransposed[4] = { float2(0, h), float2(0, 0), float2(w, 0), float2(w, h) };
    float2 uv = horizontalStandard[corner];
    if (face == 0 || face == 5) {
        uv = verticalStandard[corner];
    } else if (face == 1 || face == 4) {
        uv = verticalTransposed[corner];
    } else if (face == 3) {
        uv = horizontalTransposed[corner];
    }
    if (flags != 0) {
        uv = float2(uv.y, w - uv.x);
    }
    return uv;
}

vertex WorldOut world_vertex(WorldIn in [[stage_in]], uint vertexId [[vertex_id]], constant DrawData& draw [[buffer(1)]])
{
    const uint cornerOrder[6] = { 0, 1, 2, 0, 2, 3 };
    const float faceShade[6] = { 0.6, 0.6, 0.5, 1.0, 0.8, 0.8 };
    uint geometry = in.quad.x;
    float3 localOrigin = float3(geometry & 0x1f, (geometry >> 5) & 0x1f, (geometry >> 10) & 0x1f);
    uint face = (geometry >> 15) & 0x7;
    float width = float(((geometry >> 18) & 0xf) + 1);
    float height = float(((geometry >> 22) & 0xf) + 1);
    uint corner = cornerOrder[vertexId];

    WorldOut out;
    float3 position = draw.origin.xyz + quadCorner(face, corner, localOrigin, width, height);
    out.position = draw.viewProjection * float4(position, 1.0);
    out.uv = greedyUv(face, corner, width, height, (in.quad.y >> 12) & 1);
    out.material = in.quad.y;
    out.shade = faceShade[face];
    out.relative = position;
    out.tint = in.quad.z;
    out.light = cornerLight(in.quad.w, in.ao, corner);
    out.entity = 0;
    return out;
}

struct ModelIn {
    uint4 a [[attribute(0)]];
    uint4 b [[attribute(1)]];
    uint4 c [[attribute(2)]];
    uint4 d [[attribute(3)]];
};

vertex WorldOut model_vertex(ModelIn in [[stage_in]], uint vertexId [[vertex_id]], constant DrawData& draw [[buffer(1)]])
{
    const uint cornerOrder[6] = { 0, 1, 2, 0, 2, 3 };
    const float faceShade[7] = { 0.9, 0.6, 0.6, 0.5, 1.0, 0.8, 0.8 };
    uint words[12] = { in.a.x, in.a.y, in.a.z, in.a.w, in.b.x, in.b.y, in.b.z, in.b.w, in.c.x, in.c.y, in.c.z, in.c.w };
    uint corner = cornerOrder[vertexId];
    float3 local;
    for (uint i = 0; i < 3; ++i) {
        uint component = corner * 3 + i;
        uint word = words[component / 2];
        int value = (component & 1) != 0 ? (int(word) >> 16) : (int(word << 16) >> 16);
        local[i] = float(value) / 256.0;
    }
    uint uvWord = words[6 + corner];

    WorldOut out;
    float3 position = draw.origin.xyz + local;
    out.position = draw.viewProjection * float4(position, 1.0);
    out.uv = float2(float(uvWord & 0xffff), float(uvWord >> 16)) / 4096.0;
    if ((words[11] & 0x10u) != 0) {
        out.uv.y -= fract(draw.origin.w / 32.0);
    }
    out.material = words[10];
    out.shade = faceShade[min(words[11] & 0xfu, 6u)];
    out.relative = position;
    uint rgb = words[11] >> 8;
    out.tint = rgb != 0 ? (0x80000000u | rgb) : 0u;
    out.light = cornerLight(in.d.x, in.d.y, corner);
    out.entity = (words[11] >> 5) & 1;
    return out;
}

float4 sampleLayer(texture2d_array<float> blocks, texture2d_array<float> blocksHigh, sampler blockSampler, float2 uv, uint layer)
{
    float4 low = blocks.sample(blockSampler, uv, min(layer, 2047u));
    float4 high = blocksHigh.sample(blockSampler, uv, layer >= 2048u ? layer - 2048u : 0u);
    return layer >= 2048u ? high : low;
}

float4 sampleMaterial(texture2d_array<float> blocks, texture2d_array<float> blocksHigh, sampler blockSampler, constant DrawData& draw, uint material, float2 uv)
{
    uint layer = material & 0xfff;
    uint count = ((material >> 14) & 0x7f) + 1;
    uint ticksPerFrame = ((material >> 21) & 0x7ff) + 1;
    float timeline = draw.origin.w / float(ticksPerFrame);
    uint frame = uint(timeline) % count;
    float4 texel = sampleLayer(blocks, blocksHigh, blockSampler, uv, layer + frame);
    if (count > 1 && ((material >> 13) & 1) != 0) {
        float4 next = sampleLayer(blocks, blocksHigh, blockSampler, uv, layer + (frame + 1) % count);
        texel = mix(texel, next, fract(timeline));
    }
    return texel;
}

float3 shadeWorld(constant DrawData& draw, float3 rgb, float shade, float3 relative, float3 cornerLevels)
{
    float daylight = max(saturate(draw.params.y), 0.2);
    float channel = max(saturate(cornerLevels.x), saturate(cornerLevels.y) * daylight);
    float light = mix(0.04, 1.0, channel) * saturate(cornerLevels.z);
    float3 color = rgb * shade * pow(light, 1.0 / 2.2);
    float amount = smoothstep(draw.fog.w, draw.params.x, length(relative));
    return mix(color, draw.fog.rgb, amount);
}

fragment float4 blend_fragment(WorldOut in [[stage_in]], texture2d_array<float> blocks [[texture(0)]], texture2d_array<float> blocksHigh [[texture(1)]], texture2d_array<float> entities [[texture(2)]], sampler blockSampler [[sampler(0)]], constant DrawData& draw [[buffer(1)]])
{
    float4 texel = in.entity != 0 ? entities.sample(blockSampler, in.uv, in.material & 0xfff) : applyTint(sampleMaterial(blocks, blocksHigh, blockSampler, draw, in.material, in.uv), in.tint);
    if (texel.a < 0.004) {
        discard_fragment();
    }
    return float4(shadeWorld(draw, texel.rgb, in.shade, in.relative, in.light) * texel.a, texel.a);
}

struct SkyIn {
    float3 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
    uint layer [[attribute(2)]];
    uint color [[attribute(3)]];
    uint flags [[attribute(4)]];
};

struct SkyOut {
    float4 position [[position]];
    float2 uv;
    uint layer [[flat]];
    float4 color;
    uint flags [[flat]];
    float3 relative;
};

vertex SkyOut sky_vertex(SkyIn in [[stage_in]], constant DrawData& draw [[buffer(1)]])
{
    SkyOut out;
    float3 position = draw.origin.xyz + in.position;
    out.position = draw.viewProjection * float4(position, 1.0);
    out.uv = in.uv;
    out.layer = in.layer;
    out.color = float4(float(in.color & 0xff), float((in.color >> 8) & 0xff), float((in.color >> 16) & 0xff), float(in.color >> 24)) / 255.0;
    out.flags = in.flags;
    out.relative = position;
    return out;
}

fragment float4 sky_fragment(SkyOut in [[stage_in]], texture2d_array<float> blocks [[texture(0)]], sampler blockSampler [[sampler(0)]], constant DrawData& draw [[buffer(1)]])
{
    float4 color = in.color;
    if ((in.flags & 1) != 0) {
        float4 texel = blocks.sample(blockSampler, in.uv, in.layer, level(0.0));
        color.rgb *= texel.rgb;
        if ((in.flags & 2) == 0) {
            color.a *= texel.a;
        }
    }
    if ((in.flags & 2) != 0) {
        return float4(color.rgb * color.a, 0.0);
    }
    return float4(color.rgb * color.a, color.a);
}

fragment float4 world_fragment(WorldOut in [[stage_in]], texture2d_array<float> blocks [[texture(0)]], texture2d_array<float> blocksHigh [[texture(1)]], texture2d_array<float> entities [[texture(2)]], sampler blockSampler [[sampler(0)]], constant DrawData& draw [[buffer(1)]])
{
    float4 texel = in.entity != 0 ? entities.sample(blockSampler, in.uv, in.material & 0xfff) : applyTint(sampleMaterial(blocks, blocksHigh, blockSampler, draw, in.material, in.uv), in.tint);
    if (in.entity != 0) {
        if (texel.a < 0.1) {
            discard_fragment();
        }
        return texel;
    }
    if (texel.a < 0.5) {
        discard_fragment();
    }
    return float4(shadeWorld(draw, texel.rgb, in.shade, in.relative, in.light), 1.0);
}
)";

class MetalRenderer final : public Renderer {
public:
    explicit MetalRenderer(Window& window)
        : width(window.width())
        , height(window.height())
    {
        device = MTLCreateSystemDefaultDevice();
        if (!device) {
            throw std::runtime_error("No Metal device");
        }
        queue = [device newCommandQueue];

        layer = (__bridge CAMetalLayer*)window.nativeHandle();
        layer.device = device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.displaySyncEnabled = NO;
        layer.drawableSize = CGSizeMake(width, height);

        createUiPipeline();
        createWorldPipeline();
    }

    void resize(uint32_t newWidth, uint32_t newHeight) override
    {
        width = newWidth;
        height = newHeight;
        layer.drawableSize = CGSizeMake(width, height);
    }

    void uploadUiAtlas(const uint8_t* pixels, uint32_t atlasWidth, uint32_t atlasHeight) override
    {
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:atlasWidth height:atlasHeight mipmapped:NO];
        descriptor.usage = MTLTextureUsageShaderRead;
        atlas = [device newTextureWithDescriptor:descriptor];
        [atlas replaceRegion:MTLRegionMake2D(0, 0, atlasWidth, atlasHeight) mipmapLevel:0 withBytes:pixels bytesPerRow:atlasWidth * 4];
    }

    void uploadEntityTextures(const uint8_t* pixels, uint32_t size, uint32_t layers) override
    {
        std::vector<uint8_t> blank;
        if (layers == 0) {
            size = 1;
            layers = 1;
            blank.assign(4, 0);
            pixels = blank.data();
        }
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
        descriptor.textureType = MTLTextureType2DArray;
        descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
        descriptor.width = size;
        descriptor.height = size;
        descriptor.arrayLength = layers;
        descriptor.usage = MTLTextureUsageShaderRead;
        entityTextures = [device newTextureWithDescriptor:descriptor];
        entitySize = size;
        entityLayers = layers;
        for (uint32_t layer = 0; layer < layers; ++layer) {
            updateEntityTexture(layer, pixels + static_cast<size_t>(layer) * size * size * 4);
        }
    }

    void updateEntityTexture(uint32_t layer, const uint8_t* pixels) override
    {
        if (!entityTextures || layer >= entityLayers) {
            return;
        }
        [entityTextures replaceRegion:MTLRegionMake2D(0, 0, entitySize, entitySize) mipmapLevel:0 slice:layer withBytes:pixels bytesPerRow:entitySize * 4 bytesPerImage:entitySize * entitySize * 4];
    }

    void uploadBlockTextures(const BlockTextureUpload& textures) override
    {
        if (textures.layers == 0) {
            return;
        }
        for (uint32_t page = 0; page < BlockTexturePages; ++page) {
            uint32_t first = page * BlockTexturePageLayers;
            uint32_t count = textures.layers > first ? std::min(textures.layers - first, BlockTexturePageLayers) : 0;
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
            descriptor.textureType = MTLTextureType2DArray;
            descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
            descriptor.width = textures.size;
            descriptor.height = textures.size;
            descriptor.arrayLength = std::max(count, 1u);
            descriptor.mipmapLevelCount = textures.mipLevels;
            descriptor.usage = MTLTextureUsageShaderRead;
            blockTextures[page] = [device newTextureWithDescriptor:descriptor];
            for (uint32_t layerIndex = 0; layerIndex < count; ++layerIndex) {
                for (uint32_t mip = 0; mip < textures.mipLevels; ++mip) {
                    uint32_t side = std::max<uint32_t>(textures.size >> mip, 1);
                    const uint8_t* source = textures.mips[mip] + static_cast<size_t>(first + layerIndex) * side * side * 4;
                    [blockTextures[page] replaceRegion:MTLRegionMake2D(0, 0, side, side) mipmapLevel:mip slice:layerIndex withBytes:source bytesPerRow:side * 4 bytesPerImage:side * side * 4];
                }
            }
        }
    }

    void setChunkMesh(uint64_t id, int32_t originX, int32_t originY, int32_t originZ, const ChunkMeshUpload& mesh) override
    {
        if (mesh.cubeCount == 0 && mesh.modelCount == 0 && mesh.translucentCubeCount == 0 && mesh.translucentModelCount == 0) {
            chunks.erase(id);
            return;
        }
        ChunkBuffer chunk;
        const void* sources[4] = { mesh.cubes, mesh.models, mesh.translucentCubes, mesh.translucentModels };
        uint32_t counts[4] = { mesh.cubeCount, mesh.modelCount, mesh.translucentCubeCount, mesh.translucentModelCount };
        for (size_t stream = 0; stream < 4; ++stream) {
            if (counts[stream]) {
                chunk.buffers[stream] = [device newBufferWithBytes:sources[stream] length:static_cast<NSUInteger>(counts[stream]) * StreamStride[stream] options:MTLResourceStorageModeShared];
            }
            chunk.counts[stream] = counts[stream];
        }
        chunk.origin = { originX, originY, originZ };
        chunks[id] = chunk;
    }

    void removeChunkMesh(uint64_t id) override
    {
        chunks.erase(id);
    }

    void clearChunkMeshes() override
    {
        chunks.clear();
    }

    void drawWorld(const WorldView& view) override
    {
        if (!encoder || !blockTextures[0]) {
            return;
        }
        [encoder setFragmentTexture:blockTextures[0] atIndex:0];
        [encoder setFragmentTexture:blockTextures[1] atIndex:1];
        [encoder setFragmentTexture:entityTextures atIndex:2];
        [encoder setFragmentSamplerState:blockSampler atIndex:0];

        WorldConstants constants(view);
        auto pushOrigin = [&](float x, float y, float z) {
            constants.setOrigin(x, y, z);
            [encoder setVertexBytes:constants.values.data() length:sizeof(float) * 32 atIndex:1];
            [encoder setFragmentBytes:constants.values.data() length:sizeof(float) * 32 atIndex:1];
        };
        auto drawStream = [&](const ChunkBuffer& chunk, size_t stream) {
            uint32_t count = chunk.counts[stream];
            if (count == 0) {
                return;
            }
            pushOrigin(static_cast<float>(chunk.origin[0] - view.cameraX), static_cast<float>(chunk.origin[1] - view.cameraY), static_cast<float>(chunk.origin[2] - view.cameraZ));
            [encoder setVertexBuffer:chunk.buffers[stream] offset:0 atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6 instanceCount:count];
        };

        [encoder setDepthStencilState:overlayDepth];
        if (view.backgroundCount) {
            id<MTLBuffer> background = [device newBufferWithBytes:view.background length:static_cast<NSUInteger>(view.backgroundCount) * sizeof(SkyVertex) options:MTLResourceStorageModeShared];
            [encoder setRenderPipelineState:skyPipeline];
            pushOrigin(0.0f, 0.0f, 0.0f);
            [encoder setVertexBuffer:background offset:0 atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:view.backgroundCount];
        }

        [encoder setDepthStencilState:worldDepth];
        ChunkFrustum frustum(view);
        std::vector<const ChunkBuffer*> visible;
        visible.reserve(chunks.size());
        for (const auto& [id, chunk] : chunks) {
            if (frustum.contains(view, chunk.origin[0], chunk.origin[1], chunk.origin[2])) {
                visible.push_back(&chunk);
            }
        }
        for (size_t stream : { size_t(0), size_t(1) }) {
            [encoder setRenderPipelineState:stream == 0 ? worldPipeline : modelPipeline];
            for (const ChunkBuffer* chunk : visible) {
                drawStream(*chunk, stream);
            }
        }
        recordedOpaque = static_cast<uint32_t>(std::count_if(visible.begin(), visible.end(), [](const ChunkBuffer* chunk) {
            return chunk->counts[0] || chunk->counts[1];
        }));

        if (view.entityQuadCount) {
            id<MTLBuffer> entityBuffer = [device newBufferWithBytes:view.entityQuads length:static_cast<NSUInteger>(view.entityQuadCount) * ModelQuadBytes options:MTLResourceStorageModeShared];
            [encoder setRenderPipelineState:modelPipeline];
            pushOrigin(view.entityOrigin[0], view.entityOrigin[1], view.entityOrigin[2]);
            [encoder setVertexBuffer:entityBuffer offset:0 atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6 instanceCount:view.entityQuadCount];
        }

        [encoder setDepthStencilState:overlayDepth];

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
            [encoder setRenderPipelineState:blendPipeline];
            drawStream(*chunk, 2);
            [encoder setRenderPipelineState:modelBlendPipeline];
            drawStream(*chunk, 3);
        }
    }

    void beginFrame(float r, float g, float b) override
    {
        drawable = [layer nextDrawable];
        if (!drawable) {
            return;
        }

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(r, g, b, 1.0);

        if (!depthTexture || depthTexture.width != drawable.texture.width || depthTexture.height != drawable.texture.height) {
            MTLTextureDescriptor* depthDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:drawable.texture.width height:drawable.texture.height mipmapped:NO];
            depthDescriptor.usage = MTLTextureUsageRenderTarget;
            depthDescriptor.storageMode = MTLStorageModePrivate;
            depthTexture = [device newTextureWithDescriptor:depthDescriptor];
        }
        pass.depthAttachment.texture = depthTexture;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth = 1.0;

        commandBuffer = [queue commandBuffer];
        encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
    }

    void drawUi(const ui::DrawList& list) override
    {
        if (!encoder || !atlas || list.indices().empty()) {
            return;
        }

        id<MTLBuffer> vertices = [device newBufferWithBytes:list.vertices().data() length:list.vertices().size() * sizeof(ui::UiVertex) options:MTLResourceStorageModeShared];
        id<MTLBuffer> indices = [device newBufferWithBytes:list.indices().data() length:list.indices().size() * sizeof(uint32_t) options:MTLResourceStorageModeShared];
        const float viewport[2] = { static_cast<float>(width), static_cast<float>(height) };

        [encoder setRenderPipelineState:uiPipeline];
        [encoder setDepthStencilState:uiDepth];
        [encoder setVertexBuffer:vertices offset:0 atIndex:0];
        [encoder setVertexBytes:viewport length:sizeof(viewport) atIndex:1];
        [encoder setFragmentTexture:atlas atIndex:0];
        [encoder setFragmentSamplerState:sampler atIndex:0];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:list.indices().size() indexType:MTLIndexTypeUInt32 indexBuffer:indices indexBufferOffset:0];
    }

    void endFrame() override
    {
        if (!drawable) {
            return;
        }
        [encoder endEncoding];
        [commandBuffer presentDrawable:drawable];
        CompletedFrame report { ++submissions, recordedOpaque };
        recordedOpaque = 0;
        std::shared_ptr<CompletedState> state = completedState;
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
            std::lock_guard<std::mutex> guard(state->mutex);
            if (report.submission > state->frame.submission) {
                state->frame = report;
            }
        }];
        [commandBuffer commit];

        encoder = nil;
        commandBuffer = nil;
        drawable = nil;
    }

    uint64_t submittedFrames() const override
    {
        return submissions;
    }

    CompletedFrame completedFrame() const override
    {
        std::lock_guard<std::mutex> guard(completedState->mutex);
        return completedState->frame;
    }

private:
    static constexpr uint32_t StreamStride[4] = { CubeQuadBytes, ModelQuadBytes, CubeQuadBytes, ModelQuadBytes };

    struct CompletedState {
        std::mutex mutex;
        CompletedFrame frame;
    };

    uint64_t submissions = 0;
    uint32_t recordedOpaque = 0;
    std::shared_ptr<CompletedState> completedState = std::make_shared<CompletedState>();

    struct ChunkBuffer {
        std::array<id<MTLBuffer>, 4> buffers;
        std::array<uint32_t, 4> counts {};
        std::array<int32_t, 3> origin {};
    };

    void createWorldPipeline()
    {
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:WorldShader] options:nil error:&error];
        if (!library) {
            throw std::runtime_error(std::string("World shader compilation failed: ") + error.localizedDescription.UTF8String);
        }

        MTLVertexDescriptor* vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];
        vertexDescriptor.attributes[0].format = MTLVertexFormatUInt4;
        vertexDescriptor.attributes[0].offset = 0;
        vertexDescriptor.attributes[0].bufferIndex = 0;
        vertexDescriptor.attributes[1].format = MTLVertexFormatUInt;
        vertexDescriptor.attributes[1].offset = 16;
        vertexDescriptor.attributes[1].bufferIndex = 0;
        vertexDescriptor.layouts[0].stride = CubeQuadBytes;
        vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerInstance;
        vertexDescriptor.layouts[0].stepRate = 1;

        MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:@"world_vertex"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"world_fragment"];
        descriptor.vertexDescriptor = vertexDescriptor;
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        worldPipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!worldPipeline) {
            throw std::runtime_error(std::string("World pipeline creation failed: ") + error.localizedDescription.UTF8String);
        }

        MTLVertexDescriptor* modelDescriptor = [MTLVertexDescriptor vertexDescriptor];
        for (NSUInteger attribute = 0; attribute < 4; ++attribute) {
            modelDescriptor.attributes[attribute].format = MTLVertexFormatUInt4;
            modelDescriptor.attributes[attribute].offset = attribute * 16;
            modelDescriptor.attributes[attribute].bufferIndex = 0;
        }
        modelDescriptor.layouts[0].stride = ModelQuadBytes;
        modelDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerInstance;
        modelDescriptor.layouts[0].stepRate = 1;
        descriptor.vertexFunction = [library newFunctionWithName:@"model_vertex"];
        descriptor.vertexDescriptor = modelDescriptor;
        modelPipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!modelPipeline) {
            throw std::runtime_error(std::string("Model pipeline creation failed: ") + error.localizedDescription.UTF8String);
        }

        MTLRenderPipelineColorAttachmentDescriptor* attachment = descriptor.colorAttachments[0];
        attachment.blendingEnabled = YES;
        attachment.sourceRGBBlendFactor = MTLBlendFactorOne;
        attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
        attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        descriptor.fragmentFunction = [library newFunctionWithName:@"blend_fragment"];
        modelBlendPipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        descriptor.vertexFunction = [library newFunctionWithName:@"world_vertex"];
        descriptor.vertexDescriptor = vertexDescriptor;
        blendPipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];

        MTLVertexDescriptor* skyDescriptor = [MTLVertexDescriptor vertexDescriptor];
        static const MTLVertexFormat skyFormats[5] = { MTLVertexFormatFloat3, MTLVertexFormatFloat2, MTLVertexFormatUInt, MTLVertexFormatUInt, MTLVertexFormatUInt };
        static const NSUInteger skyOffsets[5] = { 0, 12, 20, 24, 28 };
        for (NSUInteger attribute = 0; attribute < 5; ++attribute) {
            skyDescriptor.attributes[attribute].format = skyFormats[attribute];
            skyDescriptor.attributes[attribute].offset = skyOffsets[attribute];
            skyDescriptor.attributes[attribute].bufferIndex = 0;
        }
        skyDescriptor.layouts[0].stride = sizeof(SkyVertex);
        skyDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        descriptor.vertexFunction = [library newFunctionWithName:@"sky_vertex"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"sky_fragment"];
        descriptor.vertexDescriptor = skyDescriptor;
        skyPipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!blendPipeline || !modelBlendPipeline || !skyPipeline) {
            throw std::runtime_error(std::string("Blend pipeline creation failed: ") + (error ? error.localizedDescription.UTF8String : ""));
        }

        MTLDepthStencilDescriptor* worldDepthDescriptor = [MTLDepthStencilDescriptor new];
        worldDepthDescriptor.depthCompareFunction = MTLCompareFunctionLess;
        worldDepthDescriptor.depthWriteEnabled = YES;
        worldDepth = [device newDepthStencilStateWithDescriptor:worldDepthDescriptor];

        MTLDepthStencilDescriptor* overlayDepthDescriptor = [MTLDepthStencilDescriptor new];
        overlayDepthDescriptor.depthCompareFunction = MTLCompareFunctionLess;
        overlayDepthDescriptor.depthWriteEnabled = NO;
        overlayDepth = [device newDepthStencilStateWithDescriptor:overlayDepthDescriptor];

        MTLDepthStencilDescriptor* uiDepthDescriptor = [MTLDepthStencilDescriptor new];
        uiDepthDescriptor.depthCompareFunction = MTLCompareFunctionAlways;
        uiDepthDescriptor.depthWriteEnabled = NO;
        uiDepth = [device newDepthStencilStateWithDescriptor:uiDepthDescriptor];

        MTLSamplerDescriptor* samplerDescriptor = [MTLSamplerDescriptor new];
        samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
        samplerDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
        samplerDescriptor.mipFilter = MTLSamplerMipFilterLinear;
        samplerDescriptor.sAddressMode = MTLSamplerAddressModeRepeat;
        samplerDescriptor.tAddressMode = MTLSamplerAddressModeRepeat;
        blockSampler = [device newSamplerStateWithDescriptor:samplerDescriptor];
    }

    void createUiPipeline()
    {
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:UiShader] options:nil error:&error];
        if (!library) {
            throw std::runtime_error(std::string("UI shader compilation failed: ") + error.localizedDescription.UTF8String);
        }

        MTLVertexDescriptor* vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];
        vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
        vertexDescriptor.attributes[0].offset = 0;
        vertexDescriptor.attributes[0].bufferIndex = 0;
        vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
        vertexDescriptor.attributes[1].offset = 8;
        vertexDescriptor.attributes[1].bufferIndex = 0;
        vertexDescriptor.attributes[2].format = MTLVertexFormatUChar4Normalized;
        vertexDescriptor.attributes[2].offset = 16;
        vertexDescriptor.attributes[2].bufferIndex = 0;
        for (NSUInteger attribute = 3; attribute <= 5; ++attribute) {
            vertexDescriptor.attributes[attribute].format = MTLVertexFormatFloat2;
            vertexDescriptor.attributes[attribute].offset = 20 + (attribute - 3) * 8;
            vertexDescriptor.attributes[attribute].bufferIndex = 0;
        }
        vertexDescriptor.layouts[0].stride = sizeof(ui::UiVertex);

        MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:@"ui_vertex"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"ui_fragment"];
        descriptor.vertexDescriptor = vertexDescriptor;
        MTLRenderPipelineColorAttachmentDescriptor* color = descriptor.colorAttachments[0];
        color.pixelFormat = MTLPixelFormatBGRA8Unorm;
        color.blendingEnabled = YES;
        color.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        color.sourceAlphaBlendFactor = MTLBlendFactorOne;
        color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

        descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        uiPipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!uiPipeline) {
            throw std::runtime_error(std::string("UI pipeline creation failed: ") + error.localizedDescription.UTF8String);
        }

        MTLSamplerDescriptor* samplerDescriptor = [MTLSamplerDescriptor new];
        samplerDescriptor.minFilter = MTLSamplerMinMagFilterNearest;
        samplerDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
        samplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
        samplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler = [device newSamplerStateWithDescriptor:samplerDescriptor];
    }

    uint32_t width;
    uint32_t height;
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLRenderPipelineState> uiPipeline;
    id<MTLSamplerState> sampler;
    id<MTLTexture> atlas;
    CAMetalLayer* layer;
    id<CAMetalDrawable> drawable;
    id<MTLCommandBuffer> commandBuffer;
    id<MTLRenderCommandEncoder> encoder;
    id<MTLRenderPipelineState> worldPipeline;
    id<MTLRenderPipelineState> modelPipeline;
    id<MTLRenderPipelineState> blendPipeline;
    id<MTLRenderPipelineState> modelBlendPipeline;
    id<MTLRenderPipelineState> skyPipeline;
    id<MTLDepthStencilState> overlayDepth;
    id<MTLDepthStencilState> worldDepth;
    id<MTLDepthStencilState> uiDepth;
    id<MTLSamplerState> blockSampler;
    std::array<id<MTLTexture>, BlockTexturePages> blockTextures;
    id<MTLTexture> entityTextures;
    uint32_t entitySize = 0;
    uint32_t entityLayers = 0;
    id<MTLTexture> depthTexture;
    std::unordered_map<uint64_t, ChunkBuffer> chunks;
};

}

std::unique_ptr<Renderer> Renderer::create(Window& window)
{
    return std::make_unique<MetalRenderer>(window);
}

}
