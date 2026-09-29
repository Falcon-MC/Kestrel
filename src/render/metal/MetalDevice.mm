#include "render/rhi/Device.h"
#include "render/Renderer.h"

#include "MetalShaders.h"
#include "platform/Window.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel::rhi {

namespace {

constexpr uint32_t FramesInFlight = 3;

NSString* functionName(std::string_view entry)
{
    static const std::map<std::string_view, const char*> names {
        { "vs_main", "ui_vertex" },
        { "ps_main", "ui_fragment" },
        { "vs_world", "world_vertex" },
        { "vs_model", "model_vertex" },
        { "vs_overlay", "overlay_vertex" },
        { "vs_sky", "sky_vertex" },
        { "ps_world", "world_fragment" },
        { "ps_blend", "blend_fragment" },
        { "ps_overlay", "overlay_fragment" },
        { "ps_sky", "sky_fragment" },
    };
    auto found = names.find(entry);
    if (found == names.end()) {
        throw std::runtime_error("No Metal function for shader entry " + std::string(entry));
    }
    return [NSString stringWithUTF8String:found->second];
}

MTLVertexFormat vertexFormat(VertexFormat format)
{
    switch (format) {
    case VertexFormat::Float:
        return MTLVertexFormatFloat;
    case VertexFormat::Float2:
        return MTLVertexFormatFloat2;
    case VertexFormat::Float3:
        return MTLVertexFormatFloat3;
    case VertexFormat::UByte4Norm:
        return MTLVertexFormatUChar4Normalized;
    case VertexFormat::UInt:
        return MTLVertexFormatUInt;
    case VertexFormat::UInt4:
        return MTLVertexFormatUInt4;
    }
    return MTLVertexFormatInvalid;
}

void applyBlend(MTLRenderPipelineColorAttachmentDescriptor* attachment, BlendMode mode)
{
    switch (mode) {
    case BlendMode::None:
        attachment.blendingEnabled = NO;
        break;
    case BlendMode::Alpha:
        attachment.blendingEnabled = YES;
        attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
        attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        break;
    case BlendMode::Premultiplied:
        attachment.blendingEnabled = YES;
        attachment.sourceRGBBlendFactor = MTLBlendFactorOne;
        attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
        attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        break;
    case BlendMode::Multiply:
        attachment.blendingEnabled = YES;
        attachment.sourceRGBBlendFactor = MTLBlendFactorDestinationColor;
        attachment.destinationRGBBlendFactor = MTLBlendFactorSourceColor;
        attachment.sourceAlphaBlendFactor = MTLBlendFactorZero;
        attachment.destinationAlphaBlendFactor = MTLBlendFactorOne;
        break;
    }
}

class MetalBuffer final : public Buffer {
public:
    MetalBuffer(id<MTLBuffer> buffer, size_t bytes)
        : buffer(buffer)
        , bytes(bytes)
    {
    }

    void* mapped() override
    {
        return buffer.contents;
    }

    size_t size() const override
    {
        return bytes;
    }

    id<MTLBuffer> buffer;
    size_t bytes;
};

class MetalTexture final : public Texture {
public:
    const TextureDesc& desc() const override
    {
        return description;
    }

    id<MTLTexture> texture;
    TextureDesc description;
};

class MetalPipeline final : public Pipeline {
public:
    id<MTLRenderPipelineState> state;
    id<MTLDepthStencilState> depth;
    bool constantsVertexOnly = false;
};

class MetalTextureSet final : public TextureSet {
public:
    void bind(uint32_t slot, const Texture* texture) override
    {
        if (slot >= textures.size()) {
            return;
        }
        textures[slot] = texture ? static_cast<const MetalTexture*>(texture)->texture : blank;
    }

    std::vector<id<MTLTexture>> textures;
    id<MTLTexture> blank;
    id<MTLSamplerState> sampler;
};

/**
 * The frames the GPU has finished, shared with the completion handlers that
 * outlive a frame's recording.
 */
struct CompletionState {
    std::atomic<uint64_t> completed { 0 };
    dispatch_semaphore_t slots;
};

class MetalDevice final : public Device {
public:
    explicit MetalDevice(Window& window)
        : surfaceWidth(window.width())
        , surfaceHeight(window.height())
    {
        device = MTLCreateSystemDefaultDevice();
        if (!device) {
            throw std::runtime_error("No Metal device");
        }
        adapterName = device.name.UTF8String;
        queue = [device newCommandQueue];

        layer = (__bridge CAMetalLayer*)window.nativeHandle();
        layer.device = device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.displaySyncEnabled = NO;
        layer.drawableSize = CGSizeMake(surfaceWidth, surfaceHeight);

        completion = std::make_shared<CompletionState>();
        completion->slots = dispatch_semaphore_create(FramesInFlight);

        uiLibrary = compileLibrary(metal::UiShader);
        worldLibrary = compileLibrary(metal::WorldShader);

        MTLSamplerDescriptor* pixel = [MTLSamplerDescriptor new];
        pixel.minFilter = MTLSamplerMinMagFilterNearest;
        pixel.magFilter = MTLSamplerMinMagFilterNearest;
        pixel.sAddressMode = MTLSamplerAddressModeClampToEdge;
        pixel.tAddressMode = MTLSamplerAddressModeClampToEdge;
        pixelSampler = [device newSamplerStateWithDescriptor:pixel];

        MTLSamplerDescriptor* terrain = [MTLSamplerDescriptor new];
        terrain.minFilter = MTLSamplerMinMagFilterLinear;
        terrain.magFilter = MTLSamplerMinMagFilterNearest;
        terrain.mipFilter = MTLSamplerMipFilterLinear;
        terrain.sAddressMode = MTLSamplerAddressModeRepeat;
        terrain.tAddressMode = MTLSamplerAddressModeRepeat;
        terrainSampler = [device newSamplerStateWithDescriptor:terrain];

        blankImage = blankTexture(MTLTextureType2D);
        blankArray = blankTexture(MTLTextureType2DArray);
    }

    ~MetalDevice() override
    {
        waitIdle();
    }

    std::string_view backendName() const override
    {
        return "Metal";
    }

    const std::string& deviceName() const override
    {
        return adapterName;
    }

    uint32_t width() const override
    {
        return surfaceWidth;
    }

    uint32_t height() const override
    {
        return surfaceHeight;
    }

    uint32_t framesInFlight() const override
    {
        return FramesInFlight;
    }

    uint32_t frameSlot() const override
    {
        return frame;
    }

    uint64_t completedSubmission() const override
    {
        return completion->completed.load();
    }

    uint64_t submittedFrames() const override
    {
        return submissions;
    }

    void resize(uint32_t newWidth, uint32_t newHeight) override
    {
        surfaceWidth = newWidth;
        surfaceHeight = newHeight;
        layer.drawableSize = CGSizeMake(surfaceWidth, surfaceHeight);
    }

    void waitIdle() override
    {
        id<MTLCommandBuffer> fence = [queue commandBuffer];
        [fence commit];
        [fence waitUntilCompleted];
    }

    std::unique_ptr<Buffer> createBuffer(size_t size) override
    {
        id<MTLBuffer> buffer = [device newBufferWithLength:std::max<size_t>(size, 1) options:MTLResourceStorageModeShared];
        return std::make_unique<MetalBuffer>(buffer, size);
    }

    std::unique_ptr<Texture> createTexture(const TextureDesc& desc) override
    {
        auto texture = std::make_unique<MetalTexture>();
        texture->description = desc;
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
        descriptor.textureType = desc.array ? MTLTextureType2DArray : MTLTextureType2D;
        descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
        descriptor.width = desc.width;
        descriptor.height = desc.height;
        descriptor.arrayLength = desc.array ? desc.layers : 1;
        descriptor.mipmapLevelCount = desc.mipLevels;
        descriptor.usage = MTLTextureUsageShaderRead;
        texture->texture = [device newTextureWithDescriptor:descriptor];
        return texture;
    }

    void uploadTexture(Texture& target, const std::vector<TextureData>& data) override
    {
        auto& texture = static_cast<MetalTexture&>(target);
        for (const TextureData& entry : data) {
            NSUInteger w = std::max<uint32_t>(texture.description.width >> entry.mip, 1);
            NSUInteger h = std::max<uint32_t>(texture.description.height >> entry.mip, 1);
            [texture.texture replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:entry.mip slice:entry.layer withBytes:entry.pixels bytesPerRow:w * 4 bytesPerImage:w * h * 4];
        }
    }

    std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override
    {
        id<MTLLibrary> library = desc.library == ShaderLibrary::Ui ? uiLibrary : worldLibrary;
        NSString* vertexName = functionName(desc.vertexEntry);
        NSString* fragmentName = functionName(desc.pixelEntry);
        if (desc.source) {
            if (desc.source->metal.empty()) {
                throw std::runtime_error("The shader has no Metal source, which Metal needs");
            }
            library = compileLibrary(desc.source->metal.c_str());
            vertexName = @"vs_main";
            fragmentName = @"ps_main";
        }
        MTLVertexDescriptor* vertices = [MTLVertexDescriptor vertexDescriptor];
        for (NSUInteger index = 0; index < desc.vertices.attributes.size(); ++index) {
            const VertexAttribute& attribute = desc.vertices.attributes[index];
            vertices.attributes[index].format = vertexFormat(attribute.format);
            vertices.attributes[index].offset = attribute.offset;
            vertices.attributes[index].bufferIndex = 0;
        }
        vertices.layouts[0].stride = desc.vertices.stride;
        vertices.layouts[0].stepFunction = desc.vertices.perInstance ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
        vertices.layouts[0].stepRate = 1;

        MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:vertexName];
        descriptor.fragmentFunction = [library newFunctionWithName:fragmentName];
        descriptor.vertexDescriptor = vertices;
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        applyBlend(descriptor.colorAttachments[0], desc.blend);
        descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        NSError* error = nil;
        auto pipeline = std::make_unique<MetalPipeline>();
        pipeline->state = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!pipeline->state) {
            throw std::runtime_error(std::string("Metal pipeline creation failed: ") + (error ? error.localizedDescription.UTF8String : desc.vertexEntry));
        }
        MTLDepthStencilDescriptor* depth = [MTLDepthStencilDescriptor new];
        depth.depthCompareFunction = desc.depthCompare == DepthCompare::Less ? MTLCompareFunctionLess : MTLCompareFunctionLessEqual;
        depth.depthWriteEnabled = desc.depthWrite ? YES : NO;
        pipeline->depth = [device newDepthStencilStateWithDescriptor:depth];
        pipeline->constantsVertexOnly = desc.bindings.constantsVertexOnly;
        return pipeline;
    }

    std::unique_ptr<TextureSet> createTextureSet(uint32_t count, bool arrays, SamplerMode mode) override
    {
        auto set = std::make_unique<MetalTextureSet>();
        set->blank = arrays ? blankArray : blankImage;
        set->textures.assign(count, set->blank);
        set->sampler = mode == SamplerMode::PixelClamp ? pixelSampler : terrainSampler;
        return set;
    }

    void retire(std::unique_ptr<Buffer>) override
    {
    }

    uint64_t beginFrame(float r, float g, float b) override
    {
        encoder = nil;
        drawable = [layer nextDrawable];
        if (!drawable) {
            return submissions + 1;
        }
        dispatch_semaphore_wait(completion->slots, DISPATCH_TIME_FOREVER);

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
        viewport = { 0.0, 0.0, double(surfaceWidth), double(surfaceHeight), 0.0, 1.0 };
        [encoder setViewport:viewport];
        bound = nullptr;
        boundTextures = nullptr;
        return submissions + 1;
    }

    bool recording() const override
    {
        return encoder != nil;
    }

    void setPipeline(const Pipeline& pipeline) override
    {
        if (!encoder) {
            return;
        }
        const auto* chosen = static_cast<const MetalPipeline*>(&pipeline);
        if (chosen == bound) {
            return;
        }
        [encoder setRenderPipelineState:chosen->state];
        [encoder setDepthStencilState:chosen->depth];
        bound = chosen;
    }

    void setConstants(const void* values, uint32_t count) override
    {
        if (!encoder || !bound) {
            return;
        }
        NSUInteger bytes = count * sizeof(uint32_t);
        [encoder setVertexBytes:values length:bytes atIndex:1];
        if (!bound->constantsVertexOnly) {
            [encoder setFragmentBytes:values length:bytes atIndex:1];
        }
    }

    void setTextures(const TextureSet& textures) override
    {
        if (!encoder) {
            return;
        }
        const auto* set = static_cast<const MetalTextureSet*>(&textures);
        for (NSUInteger slot = 0; slot < set->textures.size(); ++slot) {
            [encoder setFragmentTexture:set->textures[slot] atIndex:slot];
        }
        [encoder setFragmentSamplerState:set->sampler atIndex:0];
        boundTextures = set;
    }

    void setVertexBuffer(const Buffer& buffer, uint32_t, size_t) override
    {
        if (encoder) {
            [encoder setVertexBuffer:static_cast<const MetalBuffer&>(buffer).buffer offset:0 atIndex:0];
        }
    }

    void setIndexBuffer(const Buffer& buffer, size_t) override
    {
        indexBuffer = static_cast<const MetalBuffer&>(buffer).buffer;
    }

    void setDepthRange(float maxDepth) override
    {
        if (!encoder || viewport.zfar == maxDepth) {
            return;
        }
        viewport.zfar = maxDepth;
        [encoder setViewport:viewport];
    }

    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override
    {
        if (encoder) {
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:firstVertex vertexCount:vertexCount instanceCount:instanceCount baseInstance:firstInstance];
        }
    }

    void drawIndexed(uint32_t indexCount) override
    {
        if (encoder && indexBuffer) {
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:indexCount indexType:MTLIndexTypeUInt32 indexBuffer:indexBuffer indexBufferOffset:0];
        }
    }

    void endFrame() override
    {
        if (!encoder) {
            return;
        }
        [encoder endEncoding];
        [commandBuffer presentDrawable:drawable];
        uint64_t submission = ++submissions;
        std::shared_ptr<CompletionState> state = completion;
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
            uint64_t previous = state->completed.load();
            while (submission > previous && !state->completed.compare_exchange_weak(previous, submission)) {
            }
            dispatch_semaphore_signal(state->slots);
        }];
        [commandBuffer commit];
        encoder = nil;
        commandBuffer = nil;
        drawable = nil;
        indexBuffer = nil;
        frame = (frame + 1) % FramesInFlight;
    }

private:
    id<MTLLibrary> compileLibrary(const char* source)
    {
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:source] options:nil error:&error];
        if (!library) {
            throw std::runtime_error(std::string("Metal shader compilation failed: ") + error.localizedDescription.UTF8String);
        }
        return library;
    }

    id<MTLTexture> blankTexture(MTLTextureType type)
    {
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
        descriptor.textureType = type;
        descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
        descriptor.width = 1;
        descriptor.height = 1;
        descriptor.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
        const uint8_t blank[4] = { 0, 0, 0, 0 };
        [texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 slice:0 withBytes:blank bytesPerRow:4 bytesPerImage:4];
        return texture;
    }

    uint32_t surfaceWidth;
    uint32_t surfaceHeight;
    id<MTLDevice> device;
    std::string adapterName;
    id<MTLCommandQueue> queue;
    CAMetalLayer* layer;
    id<MTLLibrary> uiLibrary;
    id<MTLLibrary> worldLibrary;
    id<MTLSamplerState> pixelSampler;
    id<MTLSamplerState> terrainSampler;
    id<MTLTexture> blankImage;
    id<MTLTexture> blankArray;
    id<MTLTexture> depthTexture;
    id<CAMetalDrawable> drawable;
    id<MTLCommandBuffer> commandBuffer;
    id<MTLRenderCommandEncoder> encoder;
    id<MTLBuffer> indexBuffer;
    MTLViewport viewport {};
    std::shared_ptr<CompletionState> completion;
    const MetalPipeline* bound = nullptr;
    const MetalTextureSet* boundTextures = nullptr;
    uint64_t submissions = 0;
    uint32_t frame = 0;
};

}

std::unique_ptr<Device> Device::create(Window& window)
{
    return std::make_unique<MetalDevice>(window);
}

}

namespace kestrel {

std::unique_ptr<Renderer> Renderer::create(Window& window)
{
    return rhi::createRenderer(rhi::Device::create(window));
}

}
