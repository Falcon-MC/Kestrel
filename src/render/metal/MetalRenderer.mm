#include "render/Renderer.h"

#include "platform/Window.h"
#include "ui/DrawList.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
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
    uint2 quad [[attribute(0)]];
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
};

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
    return out;
}

struct ModelIn {
    uint4 a [[attribute(0)]];
    uint4 b [[attribute(1)]];
    uint4 c [[attribute(2)]];
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
    out.material = words[10];
    out.shade = faceShade[min(words[11], 6u)];
    out.relative = position;
    return out;
}

float4 sampleMaterial(texture2d_array<float> blocks, sampler blockSampler, constant DrawData& draw, uint material, float2 uv)
{
    uint layer = material & 0xfff;
    uint count = ((material >> 14) & 0x7f) + 1;
    uint ticksPerFrame = ((material >> 21) & 0x7ff) + 1;
    float timeline = draw.origin.w / float(ticksPerFrame);
    uint frame = uint(timeline) % count;
    float4 texel = blocks.sample(blockSampler, uv, layer + frame);
    if (count > 1 && ((material >> 13) & 1) != 0) {
        float4 next = blocks.sample(blockSampler, uv, layer + (frame + 1) % count);
        texel = mix(texel, next, fract(timeline));
    }
    return texel;
}

float3 shadeWorld(constant DrawData& draw, float3 rgb, float shade, float3 relative)
{
    float3 color = rgb * shade * mix(0.25, 1.0, draw.params.y);
    float amount = smoothstep(draw.fog.w, draw.params.x, length(relative));
    return mix(color, draw.fog.rgb, amount);
}

fragment float4 blend_fragment(WorldOut in [[stage_in]], texture2d_array<float> blocks [[texture(0)]], sampler blockSampler [[sampler(0)]], constant DrawData& draw [[buffer(1)]])
{
    float4 texel = sampleMaterial(blocks, blockSampler, draw, in.material, in.uv);
    if (texel.a < 0.004) {
        discard_fragment();
    }
    return float4(shadeWorld(draw, texel.rgb, in.shade, in.relative) * texel.a, texel.a);
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
    const float3 normals[7] = { float3(0.0), float3(0, -1, 0), float3(0, 1, 0), float3(-1, 0, 0), float3(1, 0, 0), float3(0, 0, -1), float3(0, 0, 1) };
    float4 color = in.color;
    if ((in.flags & 1) != 0) {
        float4 texel = blocks.sample(blockSampler, in.uv, in.layer, level(0.0));
        color.rgb *= texel.rgb;
        if ((in.flags & 2) == 0) {
            color.a *= texel.a;
        }
    }
    uint normal = (in.flags >> 3) & 7;
    if (normal != 0) {
        float directional = max(dot(normals[min(normal, 6u)], draw.sun.xyz), 0.0);
        color.rgb *= max(draw.params.y, 0.2) * mix(0.55, 1.0, directional);
    }
    if ((in.flags & 4) != 0) {
        float start = clamp(draw.fog.w, 0.0, 255.0);
        float end = clamp(draw.params.x, 0.0, 255.0);
        float range = length(in.relative);
        float amount = end <= start ? (range >= end ? 1.0 : 0.0) : smoothstep(start, end, range);
        color.a *= 1.0 - amount;
    }
    if ((in.flags & 2) != 0) {
        return float4(color.rgb * color.a, 0.0);
    }
    return float4(color.rgb * color.a, color.a);
}

fragment float4 world_fragment(WorldOut in [[stage_in]], texture2d_array<float> blocks [[texture(0)]], sampler blockSampler [[sampler(0)]], constant DrawData& draw [[buffer(1)]])
{
    float4 texel = sampleMaterial(blocks, blockSampler, draw, in.material, in.uv);
    if (texel.a < 0.5) {
        discard_fragment();
    }
    return float4(shadeWorld(draw, texel.rgb, in.shade, in.relative), 1.0);
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

    void uploadBlockTextures(const BlockTextureUpload& textures) override
    {
        if (textures.layers == 0) {
            return;
        }
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
        descriptor.textureType = MTLTextureType2DArray;
        descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
        descriptor.width = textures.size;
        descriptor.height = textures.size;
        descriptor.arrayLength = textures.layers;
        descriptor.mipmapLevelCount = textures.mipLevels;
        descriptor.usage = MTLTextureUsageShaderRead;
        blockTextures = [device newTextureWithDescriptor:descriptor];
        for (uint32_t layerIndex = 0; layerIndex < textures.layers; ++layerIndex) {
            for (uint32_t mip = 0; mip < textures.mipLevels; ++mip) {
                uint32_t side = std::max<uint32_t>(textures.size >> mip, 1);
                const uint8_t* source = textures.mips[mip] + static_cast<size_t>(layerIndex) * side * side * 4;
                [blockTextures replaceRegion:MTLRegionMake2D(0, 0, side, side) mipmapLevel:mip slice:layerIndex withBytes:source bytesPerRow:side * 4 bytesPerImage:side * side * 4];
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

    void setCloudMesh(const SkyVertex* vertices, uint32_t count) override
    {
        clouds = count ? [device newBufferWithBytes:vertices length:static_cast<NSUInteger>(count) * sizeof(SkyVertex) options:MTLResourceStorageModeShared] : nil;
        cloudCount = count;
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
        if (!encoder || !blockTextures) {
            return;
        }
        [encoder setFragmentTexture:blockTextures atIndex:0];
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
        for (size_t stream : { size_t(0), size_t(1) }) {
            [encoder setRenderPipelineState:stream == 0 ? worldPipeline : modelPipeline];
            for (const auto& [id, chunk] : chunks) {
                drawStream(chunk, stream);
            }
        }

        [encoder setDepthStencilState:overlayDepth];
        if (clouds && cloudCount) {
            [encoder setRenderPipelineState:skyPipeline];
            for (uint32_t i = 0; i < view.cloudOriginCount; ++i) {
                const std::array<float, 3>& origin = view.cloudOrigins[i];
                pushOrigin(origin[0], origin[1], origin[2]);
                [encoder setVertexBuffer:clouds offset:0 atIndex:0];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:cloudCount];
            }
        }

        std::vector<std::pair<double, const ChunkBuffer*>> ordered;
        for (const auto& [id, chunk] : chunks) {
            if (chunk.counts[2] || chunk.counts[3]) {
                double dx = chunk.origin[0] + 8.0 - view.cameraX;
                double dy = chunk.origin[1] + 8.0 - view.cameraY;
                double dz = chunk.origin[2] + 8.0 - view.cameraZ;
                ordered.emplace_back(dx * dx + dy * dy + dz * dz, &chunk);
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
        [commandBuffer commit];

        encoder = nil;
        commandBuffer = nil;
        drawable = nil;
    }

private:
    static constexpr uint32_t StreamStride[4] = { CubeQuadBytes, ModelQuadBytes, CubeQuadBytes, ModelQuadBytes };

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
        vertexDescriptor.attributes[0].format = MTLVertexFormatUInt2;
        vertexDescriptor.attributes[0].offset = 0;
        vertexDescriptor.attributes[0].bufferIndex = 0;
        vertexDescriptor.layouts[0].stride = 8;
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
        for (NSUInteger attribute = 0; attribute < 3; ++attribute) {
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
        samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
        samplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
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
    id<MTLBuffer> clouds;
    uint32_t cloudCount = 0;
    id<MTLDepthStencilState> worldDepth;
    id<MTLDepthStencilState> uiDepth;
    id<MTLSamplerState> blockSampler;
    id<MTLTexture> blockTextures;
    id<MTLTexture> depthTexture;
    std::unordered_map<uint64_t, ChunkBuffer> chunks;
};

}

std::unique_ptr<Renderer> Renderer::create(Window& window)
{
    return std::make_unique<MetalRenderer>(window);
}

}
