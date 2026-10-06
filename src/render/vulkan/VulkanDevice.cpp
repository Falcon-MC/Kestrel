#include "render/rhi/Device.h"
#include "render/Renderer.h"

#include "platform/Window.h"
#include "render/vulkan/UiShaders.h"

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kestrel::rhi {

namespace {

constexpr uint32_t FramesInFlight = 2;
constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;

void check(VkResult result, const char* what)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed (" + std::to_string(result) + ")");
    }
}

struct SpirV {
    const uint32_t* code = nullptr;
    size_t bytes = 0;
};

template <size_t Size>
SpirV spirv(const uint32_t (&code)[Size])
{
    return { code, sizeof(code) };
}

SpirV shaderCode(ShaderLibrary library, std::string_view entry)
{
    if (library == ShaderLibrary::Ui) {
        if (entry == "vs_main") {
            return spirv(shaders::UiVertex);
        }
        if (entry == "ps_main") {
            return spirv(shaders::UiFragment);
        }
    } else {
        if (entry == "vs_world") {
            return spirv(shaders::WorldVertex);
        }
        if (entry == "vs_actor") {
            return spirv(shaders::ActorVertex);
        }
        if (entry == "vs_model") {
            return spirv(shaders::ModelVertex);
        }
        if (entry == "vs_overlay") {
            return spirv(shaders::OverlayVertex);
        }
        if (entry == "vs_sky") {
            return spirv(shaders::SkyVertex);
        }
        if (entry == "ps_world") {
            return spirv(shaders::WorldFragment);
        }
        if (entry == "ps_blend") {
            return spirv(shaders::BlendFragment);
        }
        if (entry == "ps_overlay") {
            return spirv(shaders::OverlayFragment);
        }
        if (entry == "ps_sky") {
            return spirv(shaders::SkyFragment);
        }
    }
    throw std::runtime_error("No SPIR-V for shader entry " + std::string(entry));
}

SpirV sourceCode(const std::vector<uint32_t>& code)
{
    if (code.empty()) {
        throw std::runtime_error("The shader has no SPIR-V, which Vulkan needs");
    }
    return { code.data(), code.size() * sizeof(uint32_t) };
}

VkFormat vertexFormat(VertexFormat format)
{
    switch (format) {
    case VertexFormat::Float:
        return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::UByte4Norm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case VertexFormat::UInt:
        return VK_FORMAT_R32_UINT;
    case VertexFormat::UInt4:
        return VK_FORMAT_R32G32B32A32_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

VkPipelineColorBlendAttachmentState blendState(BlendMode mode)
{
    VkPipelineColorBlendAttachmentState blend {};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    switch (mode) {
    case BlendMode::None:
        break;
    case BlendMode::Alpha:
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case BlendMode::Premultiplied:
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case BlendMode::Multiply:
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    }
    return blend;
}

class VulkanDevice;

class VulkanBuffer final : public Buffer {
public:
    VulkanBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, size_t bytes, void* memoryMapped)
        : device(device)
        , buffer(buffer)
        , memory(memory)
        , bytes(bytes)
        , memoryMapped(memoryMapped)
    {
    }

    ~VulkanBuffer() override
    {
        if (memoryMapped) vkUnmapMemory(device, memory);
        vkDestroyBuffer(device, buffer, nullptr);
        vkFreeMemory(device, memory, nullptr);
    }

    void* mapped() override
    {
        return memoryMapped;
    }

    size_t size() const override
    {
        return bytes;
    }

    VkDevice device;
    VkBuffer buffer;
    VkDeviceMemory memory;
    size_t bytes;
    void* memoryMapped;
};

class VulkanTexture final : public Texture {
public:
    explicit VulkanTexture(VkDevice device)
        : device(device)
    {
    }

    ~VulkanTexture() override
    {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
        if (image != VK_NULL_HANDLE) {
            vkDestroyImage(device, image, nullptr);
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory, nullptr);
        }
    }

    const TextureDesc& desc() const override
    {
        return description;
    }

    VkDevice device;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    TextureDesc description;
    bool sampled = false;
};

class VulkanPipeline final : public Pipeline {
public:
    VulkanPipeline(VkDevice device, VkPipeline pipeline, VkPipelineLayout layout, VkShaderStageFlags constantStages, bool actorConstants)
        : device(device)
        , pipeline(pipeline)
        , layout(layout)
        , constantStages(constantStages)
        , actorConstants(actorConstants)
    {
    }

    ~VulkanPipeline() override
    {
        vkDestroyPipeline(device, pipeline, nullptr);
    }

    VkDevice device;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkShaderStageFlags constantStages;
    bool actorConstants;
};

class VulkanTextureSet final : public TextureSet {
public:
    VulkanTextureSet(VulkanDevice& owner, VkDescriptorSet set, uint32_t count, bool arrays, VkSampler sampler)
        : owner(owner)
        , set(set)
        , count(count)
        , arrays(arrays)
        , sampler(sampler)
    {
    }

    void bind(uint32_t slot, const Texture* texture) override;

    VulkanDevice& owner;
    VkDescriptorSet set;
    uint32_t count;
    bool arrays;
    VkSampler sampler;
};

class VulkanDevice final : public Device {
public:
    explicit VulkanDevice(Window& window)
        : sdlWindow(static_cast<SDL_Window*>(window.nativeHandle()))
        , requestedWidth(window.width())
        , requestedHeight(window.height())
        , offscreen(!window.visible())
    {
        createInstance();
        if (!SDL_Vulkan_CreateSurface(sdlWindow, instance, nullptr, &surface)) {
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
        }
        pickDevice();
        createDevice();
        createCommands();
        createSamplers();
        createSwapchain();
        createDescriptorPool();
        createBlankTextures();
    }

    ~VulkanDevice() override
    {
        vkDeviceWaitIdle(device);
        collectTransfers();
        retired.clear();
        sceneSet.reset();
        blankImage.reset();
        blankArray.reset();
        for (auto& [count, layout] : pipelineLayouts) {
            vkDestroyPipelineLayout(device, layout.second, nullptr);
        }
        for (auto& [count, layout] : setLayouts) {
            vkDestroyDescriptorSetLayout(device, layout, nullptr);
        }
        for (auto& pages : actorPages) pages.clear();
        vkDestroyDescriptorPool(device, actorDescriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(device, actorLayout, nullptr);
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        vkDestroySampler(device, pixelSampler, nullptr);
        vkDestroySampler(device, terrainSampler, nullptr);
        destroySwapchain();
        vkDestroyRenderPass(device, renderPass, nullptr);
        vkDestroyRenderPass(device, resumePass, nullptr);
        for (uint32_t i = 0; i < FramesInFlight; ++i) {
            vkDestroySemaphore(device, imageAvailable[i], nullptr);
            vkDestroyFence(device, inFlight[i], nullptr);
        }
        vkDestroyCommandPool(device, commandPool, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
    }

    std::string_view backendName() const override
    {
        return "Vulkan";
    }

    const std::string& deviceName() const override
    {
        return adapterName;
    }

    uint32_t width() const override
    {
        return extent.width;
    }

    uint32_t height() const override
    {
        return extent.height;
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
        return completed;
    }

    uint64_t submittedFrames() const override
    {
        return submissions;
    }

    void resize(uint32_t newWidth, uint32_t newHeight) override
    {
        requestedWidth = newWidth;
        requestedHeight = newHeight;
        swapchainDirty = true;
    }

    void setVsync(bool enabled) override
    {
        if (vsync == enabled) {
            return;
        }
        vsync = enabled;
        swapchainDirty = true;
    }

    void waitIdle() override
    {
        vkDeviceWaitIdle(device);
        collectTransfers();
    }

    std::unique_ptr<Buffer> createBuffer(size_t size) override
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        allocateBuffer(static_cast<VkDeviceSize>(size), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, buffer, memory, mapped);
        return std::make_unique<VulkanBuffer>(device, buffer, memory, size, mapped);
    }

    std::unique_ptr<Buffer> createPersistentBuffer(size_t size) override
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkBufferCreateInfo info { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        info.size = std::max<size_t>(size, 1);
        info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device, &info, nullptr, &buffer), "vkCreateBuffer");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, buffer, &requirements);
        VkMemoryAllocateInfo allocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &memory), "vkAllocateMemory");
        check(vkBindBufferMemory(device, buffer, memory, 0), "vkBindBufferMemory");
        return std::make_unique<VulkanBuffer>(device, buffer, memory, size, nullptr);
    }

    void uploadBuffer(Buffer& target, const void* data, size_t bytes) override
    {
        if (!bytes) return;
        auto staging = createBuffer(bytes);
        std::memcpy(staging->mapped(), data, bytes);
        VkCommandBuffer copy = beginOneTime();
        VkBufferCopy region { 0, 0, static_cast<VkDeviceSize>(bytes) };
        vkCmdCopyBuffer(copy, static_cast<VulkanBuffer&>(*staging).buffer, static_cast<VulkanBuffer&>(target).buffer, 1, &region);
        VkBufferMemoryBarrier barrier { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = static_cast<VulkanBuffer&>(target).buffer;
        barrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(copy, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
        queueTransfer(copy, std::move(staging));
    }

    std::unique_ptr<Texture> createTexture(const TextureDesc& desc) override
    {
        auto texture = std::make_unique<VulkanTexture>(device);
        texture->description = desc;
        VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = { desc.width, desc.height, 1 };
        imageInfo.mipLevels = desc.mipLevels;
        imageInfo.arrayLayers = desc.layers;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device, &imageInfo, nullptr, &texture->image), "vkCreateImage");

        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, texture->image, &requirements);
        VkMemoryAllocateInfo allocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &texture->memory), "vkAllocateMemory");
        vkBindImageMemory(device, texture->image, texture->memory, 0);

        VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = texture->image;
        viewInfo.viewType = desc.array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, desc.mipLevels, 0, desc.layers };
        check(vkCreateImageView(device, &viewInfo, nullptr, &texture->view), "vkCreateImageView");
        return texture;
    }

    void uploadTexture(Texture& target, const std::vector<TextureData>& data) override
    {
        uploadTextureAsync(target, data);
        waitIdle();
        collectTransfers();
    }

    void uploadTextureAsync(Texture& target, const std::vector<TextureData>& data) override
    {
        if (data.empty()) {
            return;
        }
        auto& texture = static_cast<VulkanTexture&>(target);
        VkDeviceSize total = 0;
        for (const TextureData& entry : data) {
            uint32_t w = entry.width ? entry.width : std::max<uint32_t>(texture.description.width >> entry.mip, 1);
            uint32_t h = entry.height ? entry.height : std::max<uint32_t>(texture.description.height >> entry.mip, 1);
            total += VkDeviceSize(w) * h * 4;
        }
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        allocateBuffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, stagingMemory, mapped);

        std::vector<VkBufferImageCopy> regions;
        regions.reserve(data.size());
        VkDeviceSize offset = 0;
        for (const TextureData& entry : data) {
            uint32_t w = entry.width ? entry.width : std::max<uint32_t>(texture.description.width >> entry.mip, 1);
            uint32_t h = entry.height ? entry.height : std::max<uint32_t>(texture.description.height >> entry.mip, 1);
            size_t bytes = size_t(w) * h * 4;
            size_t sourceStride = entry.rowBytes ? entry.rowBytes : size_t(w) * 4;
            for (uint32_t row = 0; row < h; ++row) {
                std::memcpy(static_cast<uint8_t*>(mapped) + offset + size_t(row) * w * 4,
                    entry.pixels + size_t(row) * sourceStride, size_t(w) * 4);
            }
            VkBufferImageCopy region {};
            region.bufferOffset = offset;
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, entry.mip, entry.layer, 1 };
            region.imageExtent = { w, h, 1 };
            region.imageOffset = { static_cast<int32_t>(entry.x), static_cast<int32_t>(entry.y), 0 };
            regions.push_back(region);
            offset += bytes;
        }

        VkCommandBuffer command = beginOneTime();
        VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = texture.sampled ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = texture.sampled ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture.image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, texture.description.mipLevels, 0, texture.description.layers };
        vkCmdPipelineBarrier(command, texture.sampled ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        vkCmdCopyBufferToImage(command, staging, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), regions.data());
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        queueTransfer(command, std::make_unique<VulkanBuffer>(device, staging, stagingMemory, static_cast<size_t>(total), mapped));
        texture.sampled = true;
    }

    std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override
    {
        VkShaderStageFlags constantStages = desc.bindings.constantsVertexOnly ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        VkPipelineLayout layout = pipelineLayout(desc.bindings.textureCount, desc.bindings.constantCount, constantStages, desc.bindings.actorConstants);

        SpirV vertexCode = desc.source ? sourceCode(desc.source->spirvVertex) : shaderCode(desc.library, desc.vertexEntry);
        SpirV fragmentCode = desc.source ? sourceCode(desc.source->spirvPixel) : shaderCode(desc.library, desc.pixelEntry);
        VkShaderModule vertexModule = shaderModule(vertexCode);
        VkShaderModule fragmentModule = shaderModule(fragmentCode);

        VkPipelineShaderStageCreateInfo stages[2] {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertexModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragmentModule;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding { 0, desc.vertices.stride, desc.vertices.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX };
        std::vector<VkVertexInputAttributeDescription> attributes;
        for (size_t location = 0; location < desc.vertices.attributes.size(); ++location) {
            const VertexAttribute& attribute = desc.vertices.attributes[location];
            attributes.push_back({ static_cast<uint32_t>(location), 0, vertexFormat(attribute.format), attribute.offset });
        }
        VkPipelineVertexInputStateCreateInfo vertexInput { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo assembly { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blend = blendState(desc.blend);
        if (!desc.colorWrite) blend.colorWriteMask = 0;
        VkPipelineColorBlendStateCreateInfo blending { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blending.attachmentCount = 1;
        blending.pAttachments = &blend;

        VkPipelineDepthStencilStateCreateInfo depthState { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depthState.depthTestEnable = VK_TRUE;
        depthState.depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE;
        depthState.depthCompareOp = desc.depthCompare == DepthCompare::Equal ? VK_COMPARE_OP_EQUAL : desc.depthCompare == DepthCompare::Less ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_LESS_OR_EQUAL;

        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo info { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &rasterizer;
        info.pMultisampleState = &multisample;
        info.pColorBlendState = &blending;
        info.pDepthStencilState = &depthState;
        info.pDynamicState = &dynamic;
        info.layout = layout;
        info.renderPass = renderPass;
        VkPipeline pipeline = VK_NULL_HANDLE;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "vkCreateGraphicsPipelines");
        vkDestroyShaderModule(device, vertexModule, nullptr);
        vkDestroyShaderModule(device, fragmentModule, nullptr);
        return std::make_unique<VulkanPipeline>(device, pipeline, layout, constantStages, desc.bindings.actorConstants);
    }

    std::unique_ptr<TextureSet> createTextureSet(uint32_t count, bool arrays, SamplerMode mode) override
    {
        VkDescriptorSetLayout layout = setLayout(count);
        VkDescriptorSetAllocateInfo allocation { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocation.descriptorPool = descriptorPool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        check(vkAllocateDescriptorSets(device, &allocation, &set), "vkAllocateDescriptorSets");
        auto created = std::make_unique<VulkanTextureSet>(*this, set, count, arrays, mode == SamplerMode::PixelClamp ? pixelSampler : terrainSampler);
        for (uint32_t slot = 0; slot < count; ++slot) {
            writeDescriptor(*created, slot, nullptr);
        }
        return created;
    }

    void retire(std::unique_ptr<Buffer> buffer) override
    {
        retired.push_back({ std::move(buffer), frameCounter });
    }

    uint64_t beginFrame(float r, float g, float b) override
    {
        collectTransfers();
        active = false;
        if (requestedWidth == 0 || requestedHeight == 0) {
            return submissions + 1;
        }
        if (swapchainDirty) {
            recreateSwapchain();
        }

        vkWaitForFences(device, 1, &inFlight[frame], VK_TRUE, std::numeric_limits<uint64_t>::max());
        if (slotSubmissions[frame] > completed) {
            completed = slotSubmissions[frame];
        }
        if (offscreen) {
            imageIndex = 0;
        } else {
            VkResult acquired = vkAcquireNextImageKHR(device, swapchain, std::numeric_limits<uint64_t>::max(), imageAvailable[frame], VK_NULL_HANDLE, &imageIndex);
            if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
                swapchainDirty = true;
                return submissions + 1;
            }
            if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
                check(acquired, "vkAcquireNextImageKHR");
            }
        }
        actorCursor = 0;
        vkResetFences(device, 1, &inFlight[frame]);

        command = commandBuffers[frame];
        vkResetCommandBuffer(command, 0);
        VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(command, &begin);

        ++frameCounter;
        std::erase_if(retired, [this](const RetiredBuffer& entry) {
            return entry.frame + FramesInFlight < frameCounter;
        });

        VkClearValue clears[2] {};
        clears[0].color = { { r, g, b, 1.0f } };
        clears[1].depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo pass { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        pass.renderPass = renderPass;
        pass.framebuffer = framebuffers[imageIndex];
        pass.renderArea = { { 0, 0 }, extent };
        pass.clearValueCount = 2;
        pass.pClearValues = clears;
        vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);

        viewport = { 0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f };
        VkRect2D scissor { { 0, 0 }, extent };
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);
        bound = nullptr;
        boundSet = VK_NULL_HANDLE;
        active = true;
        return submissions + 1;
    }

    bool recording() const override
    {
        return active;
    }

    void setPipeline(const Pipeline& pipeline) override
    {
        if (!active) {
            return;
        }
        const auto* chosen = static_cast<const VulkanPipeline*>(&pipeline);
        if (chosen == bound) {
            return;
        }
        if (!bound || bound->layout != chosen->layout) {
            boundSet = VK_NULL_HANDLE;
        }
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, chosen->pipeline);
        bound = chosen;
    }

    void setConstants(const void* values, uint32_t count) override
    {
        if (!active || !bound) {
            return;
        }
        vkCmdPushConstants(command, bound->layout, bound->constantStages, 0, count * sizeof(uint32_t), values);
    }

    void setActorConstants(const void* values, uint32_t count) override
    {
        if (!active || !bound || !bound->actorConstants || !values || count != 60) return;
        size_t pageBytes = actorStride * 4096;
        size_t page = actorCursor / pageBytes;
        size_t offset = actorCursor % pageBytes;
        auto& pages = actorPages[frame];
        if (page == pages.size()) {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            void* mapped = nullptr;
            allocateBuffer(pageBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, buffer, memory, mapped);
            ActorPage created;
            created.buffer = std::make_unique<VulkanBuffer>(device, buffer, memory, pageBytes, mapped);
            VkDescriptorSetAllocateInfo allocation { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            allocation.descriptorPool = actorDescriptorPool;
            allocation.descriptorSetCount = 1;
            allocation.pSetLayouts = &actorLayout;
            check(vkAllocateDescriptorSets(device, &allocation, &created.set), "vkAllocateDescriptorSets: actor constants");
            VkDescriptorBufferInfo info { buffer, 0, 60 * sizeof(uint32_t) };
            VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = created.set;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            write.pBufferInfo = &info;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            pages.push_back(std::move(created));
        }
        std::memcpy(static_cast<uint8_t*>(pages[page].buffer->mapped()) + offset, values, count * sizeof(uint32_t));
        uint32_t dynamicOffset = static_cast<uint32_t>(offset);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, bound->layout, 1, 1, &pages[page].set, 1, &dynamicOffset);
        actorCursor += actorStride;
    }

    void setTextures(const TextureSet& textures) override
    {
        if (!active || !bound) {
            return;
        }
        const auto& set = static_cast<const VulkanTextureSet&>(textures);
        if (set.set == boundSet) {
            return;
        }
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, bound->layout, 0, 1, &set.set, 0, nullptr);
        boundSet = set.set;
    }

    void setVertexBuffer(const Buffer& buffer, uint32_t, size_t) override
    {
        if (!active) {
            return;
        }
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &static_cast<const VulkanBuffer&>(buffer).buffer, &offset);
    }

    void setIndexBuffer(const Buffer& buffer, size_t) override
    {
        if (!active) {
            return;
        }
        vkCmdBindIndexBuffer(command, static_cast<const VulkanBuffer&>(buffer).buffer, 0, VK_INDEX_TYPE_UINT32);
    }

    void setDepthRange(float maxDepth) override
    {
        if (!active || viewport.maxDepth == maxDepth) {
            return;
        }
        viewport.maxDepth = maxDepth;
        vkCmdSetViewport(command, 0, 1, &viewport);
    }

    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override
    {
        if (active) {
            vkCmdDraw(command, vertexCount, instanceCount, firstVertex, firstInstance);
        }
    }

    void drawIndexed(uint32_t indexCount) override
    {
        if (active) {
            vkCmdDrawIndexed(command, indexCount, 1, 0, 0, 0);
        }
    }

    void endFrame() override
    {
        if (!active) {
            return;
        }
        active = false;
        vkCmdEndRenderPass(command);
        bool capturing = captureWanted && recordCapture();
        vkEndCommandBuffer(command);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        if (!offscreen) {
            submit.waitSemaphoreCount = 1;
            submit.pWaitSemaphores = &imageAvailable[frame];
            submit.pWaitDstStageMask = &waitStage;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = &renderFinished[imageIndex];
        }
        check(vkQueueSubmit(queue, 1, &submit, inFlight[frame]), "vkQueueSubmit");
        slotSubmissions[frame] = ++submissions;
        if (offscreen) {
            if (capturing) {
                finishCapture();
            }
            frame = (frame + 1) % FramesInFlight;
            return;
        }

        VkPresentInfoKHR present { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &renderFinished[imageIndex];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &imageIndex;
        VkResult presented = vkQueuePresentKHR(queue, &present);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
            swapchainDirty = true;
        } else {
            check(presented, "vkQueuePresentKHR");
        }
        if (capturing) {
            finishCapture();
        }
        frame = (frame + 1) % FramesInFlight;
    }

    bool supportsSceneCopy() const override
    {
        return captureSupported;
    }

    const TextureSet* sceneTextures() const override
    {
        return sceneSet.get();
    }

    void copyScene(bool first, bool keep) override
    {
        if (!active || !captureSupported) {
            return;
        }
        ensureSceneImages();
        vkCmdEndRenderPass(command);

        enum { Color, Depth, Original, Kept };
        std::vector<VkImageMemoryBarrier> barriers;
        auto transition = [&](VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst) {
            VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.oldLayout = from;
            barrier.newLayout = to;
            barrier.srcAccessMask = src;
            barrier.dstAccessMask = dst;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange = { aspect, 0, 1, 0, 1 };
            barriers.push_back(barrier);
        };
        std::vector<VulkanTexture*> colorTargets { sceneImages[Color].get() };
        if (first || keep) {
            colorTargets.push_back(sceneImages[Kept].get());
        }
        if (first) {
            colorTargets.push_back(sceneImages[Original].get());
        }
        std::vector<VulkanTexture*> targets = colorTargets;
        if (first) {
            targets.push_back(sceneImages[Depth].get());
        }

        VkImageLayout presented = offscreen ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        transition(images[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT, presented, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        transition(depthImage, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        for (VulkanTexture* target : targets) {
            VkImageAspectFlags aspect = target == sceneImages[Depth].get() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            transition(target->image, aspect, target->sampled ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());

        VkImageCopy region {};
        region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.dstSubresource = region.srcSubresource;
        region.extent = { extent.width, extent.height, 1 };
        for (VulkanTexture* target : colorTargets) {
            vkCmdCopyImage(command, images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        }
        if (first) {
            region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            vkCmdCopyImage(command, depthImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sceneImages[Depth]->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        }

        barriers.clear();
        for (VulkanTexture* target : targets) {
            VkImageAspectFlags aspect = target == sceneImages[Depth].get() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            transition(target->image, aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            target->sampled = true;
        }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());

        VkRenderPassBeginInfo pass { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        pass.renderPass = resumePass;
        pass.framebuffer = framebuffers[imageIndex];
        pass.renderArea = { { 0, 0 }, extent };
        vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
        viewport.maxDepth = 1.0f;
        VkRect2D scissor { { 0, 0 }, extent };
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);
        bound = nullptr;
        boundSet = VK_NULL_HANDLE;
    }

    bool requestCapture() override
    {
        captureWanted = captureSupported;
        return captureSupported;
    }

    bool takeCapture(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height) override
    {
        if (capturedPixels.empty()) {
            return false;
        }
        rgba = std::move(capturedPixels);
        capturedPixels.clear();
        width = capturedWidth;
        height = capturedHeight;
        return true;
    }

    /**
     * Copies the swapchain image the frame drew into a host visible buffer,
     * then hands the image back to presentation.
     */
    bool recordCapture()
    {
        captureWanted = false;
        VkDeviceSize size = VkDeviceSize(extent.width) * extent.height * 4;
        void* mapped = nullptr;
        allocateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, captureBuffer, captureMemory, mapped);
        captureMapped = mapped;
        captureExtent = extent;

        VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = offscreen ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images[imageIndex];
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region {};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { extent.width, extent.height, 1 };
        vkCmdCopyImageToBuffer(command, images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, captureBuffer, 1, &region);
        if (offscreen) {
            return true;
        }

        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        return true;
    }

    void finishCapture()
    {
        vkWaitForFences(device, 1, &inFlight[frame], VK_TRUE, std::numeric_limits<uint64_t>::max());
        size_t pixels = size_t(captureExtent.width) * captureExtent.height;
        capturedPixels.resize(pixels * 4);
        const auto* source = static_cast<const uint8_t*>(captureMapped);
        bool bgra = surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM || surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB;
        for (size_t i = 0; i < pixels; ++i) {
            const uint8_t* in = source + i * 4;
            uint8_t* out = capturedPixels.data() + i * 4;
            out[0] = bgra ? in[2] : in[0];
            out[1] = in[1];
            out[2] = bgra ? in[0] : in[2];
            out[3] = 255;
        }
        capturedWidth = captureExtent.width;
        capturedHeight = captureExtent.height;
        vkUnmapMemory(device, captureMemory);
        vkDestroyBuffer(device, captureBuffer, nullptr);
        vkFreeMemory(device, captureMemory, nullptr);
        captureBuffer = VK_NULL_HANDLE;
        captureMemory = VK_NULL_HANDLE;
        captureMapped = nullptr;
    }

    /**
     * Points one slot of a set at a texture, or at the blank texture of the
     * set's kind, once the GPU no longer reads the set.
     */
    void writeDescriptor(const VulkanTextureSet& set, uint32_t slot, const Texture* texture)
    {
        const VulkanTexture* chosen = texture ? static_cast<const VulkanTexture*>(texture) : (set.arrays ? blankArray.get() : blankImage.get());
        VkDescriptorImageInfo imageDescriptor {};
        imageDescriptor.sampler = set.sampler;
        imageDescriptor.imageView = chosen->view;
        imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = set.set;
        write.dstBinding = slot;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

private:
    struct RetiredBuffer {
        std::unique_ptr<Buffer> buffer;
        uint64_t frame = 0;
    };

    /**
     * The scene copies live as long as the swapchain; the first copy after it
     * was made creates them and points the scene set at them.
     */
    void ensureSceneImages()
    {
        if (!sceneSet) {
            sceneSet = createTextureSet(static_cast<uint32_t>(sceneImages.size()), false, SamplerMode::PixelClamp);
        }
        if (sceneImages[0]) {
            return;
        }
        for (size_t slot = 0; slot < sceneImages.size(); ++slot) {
            bool depth = slot == 1;
            auto texture = std::make_unique<VulkanTexture>(device);
            texture->description = { extent.width, extent.height, 1, 1, false };
            VkFormat format = depth ? DepthFormat : surfaceFormat.format;
            VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = format;
            imageInfo.extent = { extent.width, extent.height, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            check(vkCreateImage(device, &imageInfo, nullptr, &texture->image), "vkCreateImage");
            VkMemoryRequirements requirements;
            vkGetImageMemoryRequirements(device, texture->image, &requirements);
            VkMemoryAllocateInfo allocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(device, &allocation, nullptr, &texture->memory), "vkAllocateMemory");
            vkBindImageMemory(device, texture->image, texture->memory, 0);
            VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            viewInfo.image = texture->image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format;
            viewInfo.subresourceRange = { static_cast<VkImageAspectFlags>(depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1 };
            check(vkCreateImageView(device, &viewInfo, nullptr, &texture->view), "vkCreateImageView");
            sceneImages[slot] = std::move(texture);
        }
        // Nothing reads the set until this frame's first pass, recorded after this.
        for (size_t slot = 0; slot < sceneImages.size(); ++slot) {
            writeDescriptor(static_cast<const VulkanTextureSet&>(*sceneSet), static_cast<uint32_t>(slot), sceneImages[slot].get());
        }
    }

    VkShaderModule shaderModule(const SpirV& code)
    {
        VkShaderModuleCreateInfo info { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        info.codeSize = code.bytes;
        info.pCode = code.code;
        VkShaderModule module = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
        return module;
    }

    VkDescriptorSetLayout setLayout(uint32_t count)
    {
        auto found = setLayouts.find(count);
        if (found != setLayouts.end()) {
            return found->second;
        }
        std::vector<VkDescriptorSetLayoutBinding> bindings(count);
        for (uint32_t binding = 0; binding < count; ++binding) {
            bindings[binding].binding = binding;
            bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[binding].descriptorCount = 1;
            bindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layoutInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.bindingCount = count;
        layoutInfo.pBindings = bindings.data();
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout), "vkCreateDescriptorSetLayout");
        setLayouts.emplace(count, layout);
        return layout;
    }

    VkDescriptorSetLayout actorSetLayout()
    {
        if (actorLayout) return actorLayout;
        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        actorStride = std::max<size_t>(256, properties.limits.minUniformBufferOffsetAlignment);
        VkDescriptorSetLayoutBinding binding {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = 1;
        info.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(device, &info, nullptr, &actorLayout), "vkCreateDescriptorSetLayout: actors");
        VkDescriptorPoolSize size { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4096 };
        VkDescriptorPoolCreateInfo pool { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool.maxSets = 4096;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &pool, nullptr, &actorDescriptorPool), "vkCreateDescriptorPool: actors");
        return actorLayout;
    }

    VkPipelineLayout pipelineLayout(uint32_t textureCount, uint32_t constantCount, VkShaderStageFlags stages, bool actorConstants)
    {
        auto key = std::make_pair(textureCount, (static_cast<uint64_t>(constantCount) << 32) | stages | (actorConstants ? (uint64_t(1) << 31) : 0));
        auto found = pipelineLayouts.find(key);
        if (found != pipelineLayouts.end()) {
            return found->second.second;
        }
        VkDescriptorSetLayout set = setLayout(textureCount);
        VkDescriptorSetLayout sets[] = { set, actorConstants ? actorSetLayout() : VK_NULL_HANDLE };
        VkPushConstantRange pushConstant { stages, 0, static_cast<uint32_t>(sizeof(uint32_t) * constantCount) };
        VkPipelineLayoutCreateInfo layoutInfo { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.setLayoutCount = actorConstants ? 2 : 1;
        layoutInfo.pSetLayouts = sets;
        layoutInfo.pushConstantRangeCount = constantCount ? 1 : 0;
        layoutInfo.pPushConstantRanges = &pushConstant;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout), "vkCreatePipelineLayout");
        pipelineLayouts.emplace(key, std::make_pair(set, layout));
        return layout;
    }

    void createDescriptorPool()
    {
        VkDescriptorPoolSize size { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 };
        VkDescriptorPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets = 16;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");
    }

    void createBlankTextures()
    {
        const uint8_t blank[4] = { 0, 0, 0, 0 };
        blankImage.reset(static_cast<VulkanTexture*>(createTexture({ 1, 1, 1, 1, false }).release()));
        uploadTexture(*blankImage, { { 0, 0, blank } });
        blankArray.reset(static_cast<VulkanTexture*>(createTexture({ 1, 1, 1, 1, true }).release()));
        uploadTexture(*blankArray, { { 0, 0, blank } });
    }

    void createSamplers()
    {
        VkSamplerCreateInfo pixel { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        pixel.magFilter = VK_FILTER_NEAREST;
        pixel.minFilter = VK_FILTER_NEAREST;
        pixel.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        pixel.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        pixel.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        pixel.maxLod = 1.0f;
        check(vkCreateSampler(device, &pixel, nullptr, &pixelSampler), "vkCreateSampler");

        VkSamplerCreateInfo terrain { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        terrain.magFilter = VK_FILTER_NEAREST;
        terrain.minFilter = VK_FILTER_LINEAR;
        terrain.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        terrain.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        terrain.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        terrain.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        terrain.maxLod = VK_LOD_CLAMP_NONE;
        check(vkCreateSampler(device, &terrain, nullptr, &terrainSampler), "vkCreateSampler");
    }

    struct Transfer {
        VkCommandBuffer command;
        VkFence fence;
        std::unique_ptr<Buffer> staging;
    };
    std::vector<Transfer> transfers;

    void collectTransfers()
    {
        std::erase_if(transfers, [this](Transfer& transfer) {
            if (vkGetFenceStatus(device, transfer.fence) != VK_SUCCESS) return false;
            vkFreeCommandBuffers(device, commandPool, 1, &transfer.command);
            vkDestroyFence(device, transfer.fence, nullptr);
            return true;
        });
    }

    void queueTransfer(VkCommandBuffer command, std::unique_ptr<Buffer> staging)
    {
        collectTransfers();
        check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
        VkFenceCreateInfo info { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkFence fence;
        check(vkCreateFence(device, &info, nullptr, &fence), "vkCreateFence");
        VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        check(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
        transfers.push_back({ command, fence, std::move(staging) });
    }

    VkCommandBuffer beginOneTime()
    {
        VkCommandBufferAllocateInfo commandInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandInfo.commandPool = commandPool;
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        VkCommandBuffer oneTime;
        check(vkAllocateCommandBuffers(device, &commandInfo, &oneTime), "vkAllocateCommandBuffers");
        VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(oneTime, &begin);
        return oneTime;
    }

    void endOneTime(VkCommandBuffer oneTime)
    {
        vkEndCommandBuffer(oneTime);
        VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &oneTime;
        check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
        vkQueueWaitIdle(queue);
        vkFreeCommandBuffers(device, commandPool, 1, &oneTime);
    }

    void allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped)
    {
        VkBufferCreateInfo info { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device, &info, nullptr, &buffer), "vkCreateBuffer");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, buffer, &requirements);
        VkMemoryAllocateInfo allocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &memory), "vkAllocateMemory");
        vkBindBufferMemory(device, buffer, memory, 0);
        check(vkMapMemory(device, memory, 0, size, 0, &mapped), "vkMapMemory");
    }

    uint32_t memoryType(uint32_t allowed, VkMemoryPropertyFlags flags) const
    {
        VkPhysicalDeviceMemoryProperties properties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            if ((allowed & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) {
                return i;
            }
        }
        throw std::runtime_error("No suitable Vulkan memory type");
    }

    void createInstance()
    {
        uint32_t extensionCount = 0;
        const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
        if (!extensions) {
            throw std::runtime_error("Vulkan is not available on this system");
        }
        VkApplicationInfo application { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        application.pApplicationName = "Kestrel";
        application.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        info.pApplicationInfo = &application;
        info.enabledExtensionCount = extensionCount;
        info.ppEnabledExtensionNames = extensions;
        check(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");
    }

    void pickDevice()
    {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance, &count, devices.data());
        int bestScore = -1;
        for (VkPhysicalDevice candidate : devices) {
            uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
            for (uint32_t family = 0; family < familyCount; ++family) {
                VkBool32 presentable = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, surface, &presentable);
                if (!(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !presentable) {
                    continue;
                }
                VkPhysicalDeviceProperties properties;
                vkGetPhysicalDeviceProperties(candidate, &properties);
                int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : 0;
                if (score > bestScore) {
                    bestScore = score;
                    physicalDevice = candidate;
                    queueFamily = family;
                }
                break;
            }
        }
        if (physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("No Vulkan device can present to this window");
        }
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        adapterName = properties.deviceName;
    }

    void createDevice()
    {
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkDeviceCreateInfo info { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queueInfo;
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = extensions;
        check(vkCreateDevice(physicalDevice, &info, nullptr, &device), "vkCreateDevice");
        vkGetDeviceQueue(device, queueFamily, 0, &queue);
    }

    void createCommands()
    {
        VkCommandPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamily;
        check(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo allocation { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocation.commandPool = commandPool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = FramesInFlight;
        check(vkAllocateCommandBuffers(device, &allocation, commandBuffers.data()), "vkAllocateCommandBuffers");
        VkSemaphoreCreateInfo semaphoreInfo { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fenceInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (uint32_t i = 0; i < FramesInFlight; ++i) {
            check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailable[i]), "vkCreateSemaphore");
            check(vkCreateFence(device, &fenceInfo, nullptr, &inFlight[i]), "vkCreateFence");
        }
    }

    /**
     * Stands in for the swapchain while the window is hidden: one image the
     * frames draw into and captures read from, never presented.
     */
    void createOffscreenTarget()
    {
        surfaceFormat = { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        extent = { std::max(requestedWidth, 1u), std::max(requestedHeight, 1u) };
        captureSupported = true;
        VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = surfaceFormat.format;
        imageInfo.extent = { extent.width, extent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        images.resize(1);
        check(vkCreateImage(device, &imageInfo, nullptr, &images[0]), "vkCreateImage");
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, images[0], &requirements);
        VkMemoryAllocateInfo allocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &offscreenMemory), "vkAllocateMemory");
        vkBindImageMemory(device, images[0], offscreenMemory, 0);
    }

    void createSwapchain()
    {
        if (offscreen) {
            createOffscreenTarget();
            createTargets();
            return;
        }
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
        surfaceFormat = formats.front();
        for (const VkSurfaceFormatKHR& candidate : formats) {
            if ((candidate.format == VK_FORMAT_B8G8R8A8_UNORM || candidate.format == VK_FORMAT_R8G8B8A8_UNORM) && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surfaceFormat = candidate;
                break;
            }
        }
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            extent = capabilities.currentExtent;
        } else {
            extent.width = std::clamp(requestedWidth, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(requestedHeight, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }
        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0) {
            imageCount = std::min(imageCount, capabilities.maxImageCount);
        }
        VkSwapchainCreateInfoKHR info { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        info.surface = surface;
        info.minImageCount = imageCount;
        info.imageFormat = surfaceFormat.format;
        info.imageColorSpace = surfaceFormat.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        captureSupported = (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        if (captureSupported) {
            info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = capabilities.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, modes.data());
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        for (VkPresentModeKHR preferred : { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR }) {
            if (!vsync && std::find(modes.begin(), modes.end(), preferred) != modes.end()) {
                info.presentMode = preferred;
                break;
            }
        }
        info.clipped = VK_TRUE;
        check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "vkCreateSwapchainKHR");

        uint32_t count = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
        images.resize(count);
        vkGetSwapchainImagesKHR(device, swapchain, &count, images.data());
        createTargets();
    }

    void createTargets()
    {
        auto count = static_cast<uint32_t>(images.size());
        if (renderPass == VK_NULL_HANDLE) {
            createRenderPass();
        }
        createDepth();
        views.resize(count);
        framebuffers.resize(count);
        renderFinished.resize(count);
        VkSemaphoreCreateInfo semaphoreInfo { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (uint32_t i = 0; i < count; ++i) {
            VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            viewInfo.image = images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = surfaceFormat.format;
            viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            check(vkCreateImageView(device, &viewInfo, nullptr, &views[i]), "vkCreateImageView");
            VkImageView attachments[] = { views[i], depthView };
            VkFramebufferCreateInfo framebufferInfo { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 2;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;
            check(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]), "vkCreateFramebuffer");
            check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinished[i]), "vkCreateSemaphore");
        }
    }

    void createRenderPass()
    {
        VkAttachmentDescription color {};
        color.format = surfaceFormat.format;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = offscreen ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentDescription depth {};
        depth.format = DepthFormat;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference reference { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthReference { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;
        subpass.pDepthStencilAttachment = &depthReference;
        VkSubpassDependency dependency {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkAttachmentDescription attachments[] = { color, depth };
        VkRenderPassCreateInfo info { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        info.attachmentCount = 2;
        info.pAttachments = attachments;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
        check(vkCreateRenderPass(device, &info, nullptr, &renderPass), "vkCreateRenderPass");

        // The same attachments picked up again after a scene copy left them
        // as copy sources; compatible with renderPass, so pipelines and
        // framebuffers work with both.
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentDescription resumed[] = { color, depth };
        dependency.srcStageMask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependency.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        dependency.dstStageMask |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        info.pAttachments = resumed;
        check(vkCreateRenderPass(device, &info, nullptr, &resumePass), "vkCreateRenderPass");
    }

    void createDepth()
    {
        VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = DepthFormat;
        imageInfo.extent = { extent.width, extent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device, &imageInfo, nullptr, &depthImage), "vkCreateImage");
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, depthImage, &requirements);
        VkMemoryAllocateInfo allocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &depthMemory), "vkAllocateMemory");
        vkBindImageMemory(device, depthImage, depthMemory, 0);
        VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = depthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = DepthFormat;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        check(vkCreateImageView(device, &viewInfo, nullptr, &depthView), "vkCreateImageView");
    }

    void destroySwapchain()
    {
        for (std::unique_ptr<VulkanTexture>& image : sceneImages) {
            image.reset();
        }
        for (VkFramebuffer framebuffer : framebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        for (VkImageView view : views) {
            vkDestroyImageView(device, view, nullptr);
        }
        for (VkSemaphore semaphore : renderFinished) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
        framebuffers.clear();
        views.clear();
        renderFinished.clear();
        if (depthView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, depthView, nullptr);
            depthView = VK_NULL_HANDLE;
        }
        if (depthImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, depthImage, nullptr);
            depthImage = VK_NULL_HANDLE;
        }
        if (depthMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, depthMemory, nullptr);
            depthMemory = VK_NULL_HANDLE;
        }
        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
        if (offscreen) {
            for (VkImage image : images) {
                vkDestroyImage(device, image, nullptr);
            }
            if (offscreenMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, offscreenMemory, nullptr);
                offscreenMemory = VK_NULL_HANDLE;
            }
        }
        images.clear();
    }

    void recreateSwapchain()
    {
        vkDeviceWaitIdle(device);
        destroySwapchain();
        createSwapchain();
        swapchainDirty = false;
    }

    SDL_Window* sdlWindow;
    uint32_t requestedWidth;
    uint32_t requestedHeight;
    bool offscreen = false;
    VkDeviceMemory offscreenMemory = VK_NULL_HANDLE;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    std::string adapterName;
    uint32_t queueFamily = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surfaceFormat {};
    VkExtent2D extent {};
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkSemaphore> renderFinished;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass resumePass = VK_NULL_HANDLE;
    std::array<std::unique_ptr<VulkanTexture>, 4> sceneImages;
    std::unique_ptr<TextureSet> sceneSet;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, FramesInFlight> commandBuffers {};
    std::array<VkSemaphore, FramesInFlight> imageAvailable {};
    std::array<VkFence, FramesInFlight> inFlight {};
    std::array<uint64_t, FramesInFlight> slotSubmissions {};
    VkSampler pixelSampler = VK_NULL_HANDLE;
    VkSampler terrainSampler = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    struct ActorPage { std::unique_ptr<Buffer> buffer; VkDescriptorSet set = VK_NULL_HANDLE; };
    std::array<std::vector<ActorPage>, FramesInFlight> actorPages;
    VkDescriptorSetLayout actorLayout = VK_NULL_HANDLE;
    VkDescriptorPool actorDescriptorPool = VK_NULL_HANDLE;
    size_t actorStride = 256;
    size_t actorCursor = 0;
    std::map<uint32_t, VkDescriptorSetLayout> setLayouts;
    std::map<std::pair<uint32_t, uint64_t>, std::pair<VkDescriptorSetLayout, VkPipelineLayout>> pipelineLayouts;
    std::unique_ptr<VulkanTexture> blankImage;
    std::unique_ptr<VulkanTexture> blankArray;
    std::vector<RetiredBuffer> retired;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkViewport viewport {};
    const VulkanPipeline* bound = nullptr;
    VkDescriptorSet boundSet = VK_NULL_HANDLE;
    uint64_t frameCounter = 0;
    uint64_t submissions = 0;
    uint64_t completed = 0;
    uint32_t frame = 0;
    uint32_t imageIndex = 0;
    bool swapchainDirty = false;
    bool vsync = false;
    bool active = false;
    bool captureSupported = false;
    bool captureWanted = false;
    VkBuffer captureBuffer = VK_NULL_HANDLE;
    VkDeviceMemory captureMemory = VK_NULL_HANDLE;
    void* captureMapped = nullptr;
    VkExtent2D captureExtent {};
    std::vector<uint8_t> capturedPixels;
    uint32_t capturedWidth = 0;
    uint32_t capturedHeight = 0;
};

void VulkanTextureSet::bind(uint32_t slot, const Texture* texture)
{
    if (slot >= count) {
        return;
    }
    owner.waitIdle();
    owner.writeDescriptor(*this, slot, texture);
}

}

std::unique_ptr<Device> Device::create(Window& window)
{
    return std::make_unique<VulkanDevice>(window);
}

}

namespace kestrel {

std::unique_ptr<Renderer> Renderer::create(Window& window)
{
    return rhi::createRenderer(rhi::Device::create(window));
}

}
