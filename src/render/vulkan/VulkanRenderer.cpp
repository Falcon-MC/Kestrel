#include "render/Renderer.h"

#include "platform/Window.h"
#include "render/vulkan/UiShaders.h"
#include "ui/DrawList.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace kestrel {

namespace {

constexpr uint32_t FramesInFlight = 2;
constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;

void check(VkResult result, const char* what)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed (" + std::to_string(result) + ")");
    }
}

class VulkanRenderer final : public Renderer {
public:
    explicit VulkanRenderer(Window& window)
        : glfwWindow(static_cast<GLFWwindow*>(window.nativeHandle()))
        , width(window.width())
        , height(window.height())
    {
        createInstance();
        check(glfwCreateWindowSurface(instance, glfwWindow, nullptr, &surface), "glfwCreateWindowSurface");
        pickDevice();
        createDevice();
        createCommands();
        createDescriptors();
        createSwapchain();
        createPipeline();
        createWorldPipeline();
    }

    ~VulkanRenderer() override
    {
        vkDeviceWaitIdle(device);
        destroyAtlas();
        destroyBlockTextures();
        destroyEntityTextures();
        for (auto& [id, chunk] : chunks) {
            for (Buffer& buffer : chunk.buffers) {
                destroyBuffer(buffer);
            }
        }
        for (RetiredBuffer& entry : retired) {
            destroyBuffer(entry.buffer);
        }
        vkDestroyPipeline(device, worldPipeline, nullptr);
        vkDestroyPipeline(device, modelPipeline, nullptr);
        vkDestroyPipeline(device, blendPipeline, nullptr);
        vkDestroyPipeline(device, modelBlendPipeline, nullptr);
        vkDestroyPipeline(device, skyPipeline, nullptr);
        vkDestroyPipelineLayout(device, worldLayout, nullptr);
        vkDestroyDescriptorPool(device, worldDescriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(device, worldDescriptorLayout, nullptr);
        vkDestroySampler(device, blockSampler, nullptr);
        for (FrameBuffers& buffers : frameBuffers) {
            destroyBuffer(buffers.sky);
            destroyBuffer(buffers.entities);
            destroyBuffer(buffers.vertices);
            destroyBuffer(buffers.indices);
        }
        destroySwapchain();
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);
        vkDestroySampler(device, sampler, nullptr);
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
        for (uint32_t i = 0; i < FramesInFlight; ++i) {
            vkDestroySemaphore(device, imageAvailable[i], nullptr);
            vkDestroyFence(device, inFlight[i], nullptr);
        }
        vkDestroyCommandPool(device, commandPool, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
    }

    void resize(uint32_t newWidth, uint32_t newHeight) override
    {
        width = newWidth;
        height = newHeight;
        swapchainDirty = true;
    }

    void uploadUiAtlas(const uint8_t* pixels, uint32_t atlasWidth, uint32_t atlasHeight) override
    {
        vkDeviceWaitIdle(device);
        destroyAtlas();

        VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = { atlasWidth, atlasHeight, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device, &imageInfo, nullptr, &atlasImage), "vkCreateImage");

        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, atlasImage, &requirements);
        VkMemoryAllocateInfo allocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &atlasMemory), "vkAllocateMemory");
        vkBindImageMemory(device, atlasImage, atlasMemory, 0);

        Buffer staging = createBuffer(static_cast<VkDeviceSize>(atlasWidth) * atlasHeight * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        std::memcpy(staging.mapped, pixels, static_cast<size_t>(atlasWidth) * atlasHeight * 4);

        VkCommandBufferAllocateInfo commandInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandInfo.commandPool = commandPool;
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        VkCommandBuffer command;
        check(vkAllocateCommandBuffers(device, &commandInfo, &command), "vkAllocateCommandBuffers");

        VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(command, &begin);

        transition(command, atlasImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region {};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { atlasWidth, atlasHeight, 1 };
        vkCmdCopyBufferToImage(command, staging.buffer, atlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        transition(command, atlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        vkEndCommandBuffer(command);

        VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
        vkQueueWaitIdle(queue);
        vkFreeCommandBuffers(device, commandPool, 1, &command);
        destroyBuffer(staging);

        VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = atlasImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        check(vkCreateImageView(device, &viewInfo, nullptr, &atlasView), "vkCreateImageView");

        VkDescriptorImageInfo imageDescriptor {};
        imageDescriptor.sampler = sampler;
        imageDescriptor.imageView = atlasView;
        imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void uploadBlockTextures(const BlockTextureUpload& textures) override
    {
        if (textures.layers == 0) {
            return;
        }
        vkDeviceWaitIdle(device);
        destroyBlockTextures();
        for (uint32_t page = 0; page < BlockTexturePages; ++page) {
            uint32_t first = page * BlockTexturePageLayers;
            uint32_t count = textures.layers > first ? std::min(textures.layers - first, BlockTexturePageLayers) : 0;
            uploadBlockTexturePage(textures, page, first, count);
        }
        if (entityImage == VK_NULL_HANDLE) {
            uploadEntityTextures(nullptr, 0, 0);
        }
    }

    void uploadEntityTextures(const uint8_t* pixels, uint32_t size, uint32_t layers) override
    {
        vkDeviceWaitIdle(device);
        destroyEntityTextures();
        std::vector<uint8_t> blank;
        if (layers == 0) {
            size = 1;
            layers = 1;
            blank.assign(4, 0);
            pixels = blank.data();
        }
        entitySize = size;
        entityLayers = layers;
        VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = { size, size, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = layers;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device, &imageInfo, nullptr, &entityImage), "vkCreateImage");
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, entityImage, &requirements);
        VkMemoryAllocateInfo allocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &entityMemory), "vkAllocateMemory");
        vkBindImageMemory(device, entityImage, entityMemory, 0);
        copyEntityLayers(pixels, 0, layers, VK_IMAGE_LAYOUT_UNDEFINED);

        VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = entityImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers };
        check(vkCreateImageView(device, &viewInfo, nullptr, &entityView), "vkCreateImageView");

        VkDescriptorImageInfo imageDescriptor {};
        imageDescriptor.sampler = blockSampler;
        imageDescriptor.imageView = entityView;
        imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = worldDescriptorSet;
        write.dstBinding = BlockTexturePages;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void updateEntityTexture(uint32_t layer, const uint8_t* pixels) override
    {
        if (entityImage == VK_NULL_HANDLE || layer >= entityLayers) {
            return;
        }
        vkDeviceWaitIdle(device);
        copyEntityLayers(pixels, layer, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    /**
     * Copies consecutive entity texture layers from tightly packed pixels and
     * leaves them ready for sampling.
     */
    void copyEntityLayers(const uint8_t* pixels, uint32_t first, uint32_t count, VkImageLayout from)
    {
        VkDeviceSize bytes = VkDeviceSize(entitySize) * entitySize * 4 * count;
        Buffer staging = createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        std::memcpy(staging.mapped, pixels, size_t(bytes));
        VkBufferImageCopy region {};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, first, count };
        region.imageExtent = { entitySize, entitySize, 1 };

        VkCommandBuffer command = beginOneTime();
        VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = from;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = entityImage;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, first, count };
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        vkCmdCopyBufferToImage(command, staging.buffer, entityImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        endOneTime(command);
        destroyBuffer(staging);
    }

    void destroyEntityTextures()
    {
        if (entityView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, entityView, nullptr);
            entityView = VK_NULL_HANDLE;
        }
        if (entityImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, entityImage, nullptr);
            entityImage = VK_NULL_HANDLE;
        }
        if (entityMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, entityMemory, nullptr);
            entityMemory = VK_NULL_HANDLE;
        }
    }

    /**
     * Uploads one page of the block texture layers into its own array image.
     * An empty page still gets a one-layer image so its binding stays valid.
     */
    void uploadBlockTexturePage(const BlockTextureUpload& textures, uint32_t page, uint32_t first, uint32_t count)
    {
        uint32_t layers = std::max(count, 1u);
        VkImage& blockImage = blockImages[page];
        VkDeviceMemory& blockMemory = blockMemories[page];
        VkImageView& blockView = blockViews[page];
        VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = { textures.size, textures.size, 1 };
        imageInfo.mipLevels = textures.mipLevels;
        imageInfo.arrayLayers = layers;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device, &imageInfo, nullptr, &blockImage), "vkCreateImage");

        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, blockImage, &requirements);
        VkMemoryAllocateInfo allocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &blockMemory), "vkAllocateMemory");
        vkBindImageMemory(device, blockImage, blockMemory, 0);

        VkDeviceSize total = 0;
        for (uint32_t mip = 0; mip < textures.mipLevels; ++mip) {
            uint32_t side = std::max<uint32_t>(textures.size >> mip, 1);
            total += VkDeviceSize(side) * side * 4 * layers;
        }
        Buffer staging = createBuffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        std::vector<VkBufferImageCopy> regions;
        VkDeviceSize offset = 0;
        for (uint32_t mip = 0; mip < textures.mipLevels; ++mip) {
            uint32_t side = std::max<uint32_t>(textures.size >> mip, 1);
            size_t layerBytes = size_t(side) * side * 4;
            if (count) {
                std::memcpy(static_cast<uint8_t*>(staging.mapped) + offset, textures.mips[mip] + layerBytes * first, layerBytes * count);
            } else {
                std::memset(static_cast<uint8_t*>(staging.mapped) + offset, 0, layerBytes);
            }
            size_t bytes = layerBytes * layers;
            VkBufferImageCopy region {};
            region.bufferOffset = offset;
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, layers };
            region.imageExtent = { side, side, 1 };
            regions.push_back(region);
            offset += bytes;
        }

        VkCommandBuffer command = beginOneTime();
        VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = blockImage;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, textures.mipLevels, 0, layers };
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        vkCmdCopyBufferToImage(command, staging.buffer, blockImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), regions.data());
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        endOneTime(command);
        destroyBuffer(staging);

        VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = blockImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, textures.mipLevels, 0, layers };
        check(vkCreateImageView(device, &viewInfo, nullptr, &blockView), "vkCreateImageView");

        VkDescriptorImageInfo imageDescriptor {};
        imageDescriptor.sampler = blockSampler;
        imageDescriptor.imageView = blockView;
        imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = worldDescriptorSet;
        write.dstBinding = page;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
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
            chunk.buffers[stream] = uploadBytes(sources[stream], size_t(counts[stream]) * StreamStride[stream]);
            chunk.counts[stream] = counts[stream];
        }
        chunk.origin = { originX, originY, originZ };
        chunks.emplace(id, chunk);
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

    void drawWorld(const WorldView& view) override
    {
        if (!recording || !blockViews[0]) {
            return;
        }
        VkCommandBuffer command = commandBuffers[frame];
        VkViewport viewport { 0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f };
        VkRect2D scissor { { 0, 0 }, extent };

        WorldConstants constants(view);
        auto bind = [&](VkPipeline pipeline) {
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, worldLayout, 0, 1, &worldDescriptorSet, 0, nullptr);
            vkCmdSetViewport(command, 0, 1, &viewport);
            vkCmdSetScissor(command, 0, 1, &scissor);
        };
        auto pushOrigin = [&](float x, float y, float z) {
            constants.setOrigin(x, y, z);
            vkCmdPushConstants(command, worldLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 32, constants.values.data());
        };
        auto drawStream = [&](const ChunkBuffer& chunk, size_t stream) {
            uint32_t count = chunk.counts[stream];
            if (count == 0) {
                return;
            }
            pushOrigin(static_cast<float>(chunk.origin[0] - view.cameraX), static_cast<float>(chunk.origin[1] - view.cameraY), static_cast<float>(chunk.origin[2] - view.cameraZ));
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(command, 0, 1, &chunk.buffers[stream].buffer, &offset);
            vkCmdDraw(command, 6, count, 0, 0);
        };

        if (view.backgroundCount) {
            FrameBuffers& buffers = frameBuffers[frame];
            size_t bytes = size_t(view.backgroundCount) * sizeof(SkyVertex);
            ensure(buffers.sky, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            std::memcpy(buffers.sky.mapped, view.background, bytes);
            bind(skyPipeline);
            pushOrigin(0.0f, 0.0f, 0.0f);
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(command, 0, 1, &buffers.sky.buffer, &offset);
            vkCmdDraw(command, view.backgroundCount, 1, 0, 0);
        }

        ChunkFrustum frustum(view);
        std::vector<const ChunkBuffer*> visible;
        visible.reserve(chunks.size());
        for (const auto& [id, chunk] : chunks) {
            if (frustum.contains(view, chunk.origin[0], chunk.origin[1], chunk.origin[2])) {
                visible.push_back(&chunk);
            }
        }
        for (size_t stream : { size_t(0), size_t(1) }) {
            bind(stream == 0 ? worldPipeline : modelPipeline);
            for (const ChunkBuffer* chunk : visible) {
                drawStream(*chunk, stream);
            }
        }
        recordedOpaque = static_cast<uint32_t>(std::count_if(visible.begin(), visible.end(), [](const ChunkBuffer* chunk) {
            return chunk->counts[0] || chunk->counts[1];
        }));

        auto drawEntities = [&](VkPipeline pipeline, uint32_t first, uint32_t count, float maxDepth) {
            if (count == 0) {
                return;
            }
            bind(pipeline);
            if (maxDepth < 1.0f) {
                VkViewport squeezed = viewport;
                squeezed.maxDepth = maxDepth;
                vkCmdSetViewport(command, 0, 1, &squeezed);
            }
            pushOrigin(view.entityOrigin[0], view.entityOrigin[1], view.entityOrigin[2]);
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(command, 0, 1, &frameBuffers[frame].entities.buffer, &offset);
            vkCmdDraw(command, 6, count, 0, first);
        };
        if (view.entityTotal()) {
            FrameBuffers& buffers = frameBuffers[frame];
            size_t bytes = size_t(view.entityTotal()) * ModelQuadBytes;
            ensure(buffers.entities, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            std::memcpy(buffers.entities.mapped, view.entityQuads, bytes);
        }
        drawEntities(modelPipeline, 0, view.entityQuadCount, 1.0f);

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
            bind(modelBlendPipeline);
            drawStream(*chunk, 3);
            bind(blendPipeline);
            drawStream(*chunk, 2);
        }
        drawEntities(modelBlendPipeline, view.entityQuadCount, view.entityBlendCount, 1.0f);
        drawEntities(modelPipeline, view.entityQuadCount + view.entityBlendCount, view.handQuadCount, HandDepthRange);
    }

    void beginFrame(float r, float g, float b) override
    {
        recording = false;
        if (width == 0 || height == 0) {
            return;
        }
        if (swapchainDirty) {
            recreateSwapchain();
        }

        vkWaitForFences(device, 1, &inFlight[frame], VK_TRUE, std::numeric_limits<uint64_t>::max());
        if (slotFrames[frame].submission > completed.submission) {
            completed = slotFrames[frame];
        }
        recordedOpaque = 0;
        VkResult acquired = vkAcquireNextImageKHR(device, swapchain, std::numeric_limits<uint64_t>::max(), imageAvailable[frame], VK_NULL_HANDLE, &imageIndex);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchainDirty = true;
            return;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
            check(acquired, "vkAcquireNextImageKHR");
        }
        vkResetFences(device, 1, &inFlight[frame]);

        VkCommandBuffer command = commandBuffers[frame];
        vkResetCommandBuffer(command, 0);
        VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(command, &begin);

        ++frameCounter;
        std::erase_if(retired, [this](RetiredBuffer& entry) {
            if (entry.frame + FramesInFlight >= frameCounter) {
                return false;
            }
            destroyBuffer(entry.buffer);
            return true;
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
        recording = true;
    }

    void drawUi(const ui::DrawList& list) override
    {
        if (!recording || !atlasView || list.indices().empty()) {
            return;
        }

        FrameBuffers& buffers = frameBuffers[frame];
        size_t vertexBytes = list.vertices().size() * sizeof(ui::UiVertex);
        size_t indexBytes = list.indices().size() * sizeof(uint32_t);
        ensure(buffers.vertices, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        ensure(buffers.indices, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        std::memcpy(buffers.vertices.mapped, list.vertices().data(), vertexBytes);
        std::memcpy(buffers.indices.mapped, list.indices().data(), indexBytes);

        VkCommandBuffer command = commandBuffers[frame];
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        const float viewport[2] = { static_cast<float>(extent.width), static_cast<float>(extent.height) };
        vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(viewport), viewport);

        VkViewport view { 0.0f, 0.0f, viewport[0], viewport[1], 0.0f, 1.0f };
        VkRect2D scissor { { 0, 0 }, extent };
        vkCmdSetViewport(command, 0, 1, &view);
        vkCmdSetScissor(command, 0, 1, &scissor);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &buffers.vertices.buffer, &offset);
        vkCmdBindIndexBuffer(command, buffers.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command, static_cast<uint32_t>(list.indices().size()), 1, 0, 0, 0);
    }

    std::string_view backendName() const override
    {
        return "Vulkan";
    }

    const std::string& deviceName() const override
    {
        return adapterName;
    }

    uint64_t submittedFrames() const override
    {
        return submissions;
    }

    CompletedFrame completedFrame() const override
    {
        return completed;
    }

    void endFrame() override
    {
        if (!recording) {
            return;
        }
        recording = false;

        VkCommandBuffer command = commandBuffers[frame];
        vkCmdEndRenderPass(command);
        vkEndCommandBuffer(command);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &imageAvailable[frame];
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &renderFinished[imageIndex];
        check(vkQueueSubmit(queue, 1, &submit, inFlight[frame]), "vkQueueSubmit");
        slotFrames[frame] = { ++submissions, recordedOpaque };

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
        frame = (frame + 1) % FramesInFlight;
    }

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize capacity = 0;
        void* mapped = nullptr;
    };

    struct FrameBuffers {
        Buffer vertices;
        Buffer indices;
        Buffer sky;
        Buffer entities;
    };

    static constexpr uint32_t StreamStride[4] = { CubeQuadBytes, ModelQuadBytes, CubeQuadBytes, ModelQuadBytes };

    struct ChunkBuffer {
        std::array<Buffer, 4> buffers;
        std::array<uint32_t, 4> counts {};
        std::array<int32_t, 3> origin {};
    };

    struct RetiredBuffer {
        Buffer buffer;
        uint64_t frame = 0;
    };

    Buffer uploadBytes(const void* data, size_t size)
    {
        if (size == 0) {
            return {};
        }
        Buffer buffer = createBuffer(VkDeviceSize(size), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        std::memcpy(buffer.mapped, data, size);
        return buffer;
    }

    void retire(ChunkBuffer& chunk)
    {
        for (Buffer& buffer : chunk.buffers) {
            if (buffer.buffer != VK_NULL_HANDLE) {
                retired.push_back({ buffer, frameCounter });
            }
        }
    }

    VkCommandBuffer beginOneTime()
    {
        VkCommandBufferAllocateInfo commandInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandInfo.commandPool = commandPool;
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        VkCommandBuffer command;
        check(vkAllocateCommandBuffers(device, &commandInfo, &command), "vkAllocateCommandBuffers");
        VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(command, &begin);
        return command;
    }

    void endOneTime(VkCommandBuffer command)
    {
        vkEndCommandBuffer(command);
        VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
        vkQueueWaitIdle(queue);
        vkFreeCommandBuffers(device, commandPool, 1, &command);
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
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
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

    void destroyDepth()
    {
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
    }

    void destroyBlockTextures()
    {
        for (uint32_t page = 0; page < BlockTexturePages; ++page) {
            if (blockViews[page] != VK_NULL_HANDLE) {
                vkDestroyImageView(device, blockViews[page], nullptr);
                blockViews[page] = VK_NULL_HANDLE;
            }
            if (blockImages[page] != VK_NULL_HANDLE) {
                vkDestroyImage(device, blockImages[page], nullptr);
                blockImages[page] = VK_NULL_HANDLE;
            }
            if (blockMemories[page] != VK_NULL_HANDLE) {
                vkFreeMemory(device, blockMemories[page], nullptr);
                blockMemories[page] = VK_NULL_HANDLE;
            }
        }
    }

    void createWorldPipeline()
    {
        std::array<VkDescriptorSetLayoutBinding, BlockTexturePages + 1> bindings {};
        for (uint32_t binding = 0; binding < bindings.size(); ++binding) {
            bindings[binding].binding = binding;
            bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[binding].descriptorCount = 1;
            bindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layoutInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &worldDescriptorLayout), "vkCreateDescriptorSetLayout");

        VkDescriptorPoolSize size { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, BlockTexturePages + 1 };
        VkDescriptorPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &worldDescriptorPool), "vkCreateDescriptorPool");

        VkDescriptorSetAllocateInfo allocation { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocation.descriptorPool = worldDescriptorPool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &worldDescriptorLayout;
        check(vkAllocateDescriptorSets(device, &allocation, &worldDescriptorSet), "vkAllocateDescriptorSets");

        VkSamplerCreateInfo samplerInfo { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
        check(vkCreateSampler(device, &samplerInfo, nullptr, &blockSampler), "vkCreateSampler");

        VkShaderModuleCreateInfo vertexInfo { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        vertexInfo.codeSize = sizeof(shaders::WorldVertex);
        vertexInfo.pCode = shaders::WorldVertex;
        VkShaderModule vertexModule;
        check(vkCreateShaderModule(device, &vertexInfo, nullptr, &vertexModule), "vkCreateShaderModule");

        VkShaderModuleCreateInfo fragmentInfo { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        fragmentInfo.codeSize = sizeof(shaders::WorldFragment);
        fragmentInfo.pCode = shaders::WorldFragment;
        VkShaderModule fragmentModule;
        check(vkCreateShaderModule(device, &fragmentInfo, nullptr, &fragmentModule), "vkCreateShaderModule");

        VkPipelineShaderStageCreateInfo stages[2] {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertexModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragmentModule;
        stages[1].pName = "main";

        VkVertexInputBindingDescription vertexBinding { 0, CubeQuadBytes, VK_VERTEX_INPUT_RATE_INSTANCE };
        VkVertexInputAttributeDescription attributes[] = {
            { 0, 0, VK_FORMAT_R32G32B32A32_UINT, 0 },
            { 1, 0, VK_FORMAT_R32_UINT, 16 },
        };
        VkPipelineVertexInputStateCreateInfo vertexInput { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &vertexBinding;
        vertexInput.vertexAttributeDescriptionCount = 2;
        vertexInput.pVertexAttributeDescriptions = attributes;

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

        VkPipelineColorBlendAttachmentState blend {};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blending { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blending.attachmentCount = 1;
        blending.pAttachments = &blend;

        VkPipelineDepthStencilStateCreateInfo depthState { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depthState.depthTestEnable = VK_TRUE;
        depthState.depthWriteEnable = VK_TRUE;
        depthState.depthCompareOp = VK_COMPARE_OP_LESS;

        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        VkPushConstantRange pushConstant { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 32 };
        VkPipelineLayoutCreateInfo pipelineLayoutInfo { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &worldDescriptorLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
        check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &worldLayout), "vkCreatePipelineLayout");

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
        info.layout = worldLayout;
        info.renderPass = renderPass;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &worldPipeline), "vkCreateGraphicsPipelines");

        VkShaderModuleCreateInfo modelInfo { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        modelInfo.codeSize = sizeof(shaders::ModelVertex);
        modelInfo.pCode = shaders::ModelVertex;
        VkShaderModule modelModule;
        check(vkCreateShaderModule(device, &modelInfo, nullptr, &modelModule), "vkCreateShaderModule");
        stages[0].module = modelModule;

        VkVertexInputBindingDescription modelBinding { 0, ModelQuadBytes, VK_VERTEX_INPUT_RATE_INSTANCE };
        VkVertexInputAttributeDescription modelAttributes[] = {
            { 0, 0, VK_FORMAT_R32G32B32A32_UINT, 0 },
            { 1, 0, VK_FORMAT_R32G32B32A32_UINT, 16 },
            { 2, 0, VK_FORMAT_R32G32B32A32_UINT, 32 },
            { 3, 0, VK_FORMAT_R32G32B32A32_UINT, 48 },
        };
        VkPipelineVertexInputStateCreateInfo modelInput { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        modelInput.vertexBindingDescriptionCount = 1;
        modelInput.pVertexBindingDescriptions = &modelBinding;
        modelInput.vertexAttributeDescriptionCount = 4;
        modelInput.pVertexAttributeDescriptions = modelAttributes;
        info.pVertexInputState = &modelInput;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &modelPipeline), "vkCreateGraphicsPipelines");

        VkShaderModuleCreateInfo blendInfo { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        blendInfo.codeSize = sizeof(shaders::BlendFragment);
        blendInfo.pCode = shaders::BlendFragment;
        VkShaderModule blendModule;
        check(vkCreateShaderModule(device, &blendInfo, nullptr, &blendModule), "vkCreateShaderModule");
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        depthState.depthWriteEnable = VK_FALSE;
        stages[1].module = blendModule;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &modelBlendPipeline), "vkCreateGraphicsPipelines");
        stages[0].module = vertexModule;
        info.pVertexInputState = &vertexInput;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &blendPipeline), "vkCreateGraphicsPipelines");

        VkShaderModuleCreateInfo skyVertexInfo { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        skyVertexInfo.codeSize = sizeof(shaders::SkyVertex);
        skyVertexInfo.pCode = shaders::SkyVertex;
        VkShaderModule skyVertexModule;
        check(vkCreateShaderModule(device, &skyVertexInfo, nullptr, &skyVertexModule), "vkCreateShaderModule");
        VkShaderModuleCreateInfo skyFragmentInfo { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        skyFragmentInfo.codeSize = sizeof(shaders::SkyFragment);
        skyFragmentInfo.pCode = shaders::SkyFragment;
        VkShaderModule skyFragmentModule;
        check(vkCreateShaderModule(device, &skyFragmentInfo, nullptr, &skyFragmentModule), "vkCreateShaderModule");
        VkVertexInputBindingDescription skyBinding { 0, sizeof(SkyVertex), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription skyAttributes[] = {
            { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
            { 1, 0, VK_FORMAT_R32G32_SFLOAT, 12 },
            { 2, 0, VK_FORMAT_R32_UINT, 20 },
            { 3, 0, VK_FORMAT_R32_UINT, 24 },
            { 4, 0, VK_FORMAT_R32_UINT, 28 },
        };
        VkPipelineVertexInputStateCreateInfo skyInput { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        skyInput.vertexBindingDescriptionCount = 1;
        skyInput.pVertexBindingDescriptions = &skyBinding;
        skyInput.vertexAttributeDescriptionCount = 5;
        skyInput.pVertexAttributeDescriptions = skyAttributes;
        stages[0].module = skyVertexModule;
        stages[1].module = skyFragmentModule;
        info.pVertexInputState = &skyInput;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &skyPipeline), "vkCreateGraphicsPipelines");

        vkDestroyShaderModule(device, skyVertexModule, nullptr);
        vkDestroyShaderModule(device, skyFragmentModule, nullptr);
        vkDestroyShaderModule(device, blendModule, nullptr);
        vkDestroyShaderModule(device, modelModule, nullptr);
        vkDestroyShaderModule(device, vertexModule, nullptr);
        vkDestroyShaderModule(device, fragmentModule, nullptr);
    }

    void createInstance()
    {
        uint32_t extensionCount = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
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

    void createDescriptors()
    {
        VkDescriptorSetLayoutBinding binding {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layoutInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout), "vkCreateDescriptorSetLayout");

        VkDescriptorPoolSize size { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
        VkDescriptorPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");

        VkDescriptorSetAllocateInfo allocation { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocation.descriptorPool = descriptorPool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &descriptorLayout;
        check(vkAllocateDescriptorSets(device, &allocation, &descriptorSet), "vkAllocateDescriptorSets");

        VkSamplerCreateInfo samplerInfo { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 1.0f;
        check(vkCreateSampler(device, &samplerInfo, nullptr, &sampler), "vkCreateSampler");
    }

    void createSwapchain()
    {
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
            extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
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
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = capabilities.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, modes.data());
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        for (VkPresentModeKHR preferred : { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR }) {
            if (std::find(modes.begin(), modes.end(), preferred) != modes.end()) {
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

            VkImageView framebufferAttachments[] = { views[i], depthView };
            VkFramebufferCreateInfo framebufferInfo { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 2;
            framebufferInfo.pAttachments = framebufferAttachments;
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
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depth {};
        depth.format = DepthFormat;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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
    }

    void createPipeline()
    {
        VkShaderModuleCreateInfo vertexInfo { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        vertexInfo.codeSize = sizeof(shaders::UiVertex);
        vertexInfo.pCode = shaders::UiVertex;
        VkShaderModule vertexModule;
        check(vkCreateShaderModule(device, &vertexInfo, nullptr, &vertexModule), "vkCreateShaderModule");

        VkShaderModuleCreateInfo fragmentInfo { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        fragmentInfo.codeSize = sizeof(shaders::UiFragment);
        fragmentInfo.pCode = shaders::UiFragment;
        VkShaderModule fragmentModule;
        check(vkCreateShaderModule(device, &fragmentInfo, nullptr, &fragmentModule), "vkCreateShaderModule");

        VkPipelineShaderStageCreateInfo stages[2] {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertexModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragmentModule;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding { 0, sizeof(ui::UiVertex), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attributes[] = {
            { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 },
            { 1, 0, VK_FORMAT_R32G32_SFLOAT, 8 },
            { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, 16 },
            { 3, 0, VK_FORMAT_R32G32_SFLOAT, 20 },
            { 4, 0, VK_FORMAT_R32G32_SFLOAT, 28 },
            { 5, 0, VK_FORMAT_R32G32_SFLOAT, 36 },
            { 6, 0, VK_FORMAT_R32_SFLOAT, 44 },
        };
        VkPipelineVertexInputStateCreateInfo vertexInput { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(attributes));
        vertexInput.pVertexAttributeDescriptions = attributes;

        VkPipelineInputAssemblyStateCreateInfo assembly { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blend {};
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blending { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blending.attachmentCount = 1;
        blending.pAttachments = &blend;

        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        VkPipelineDepthStencilStateCreateInfo depthState { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depthState.depthTestEnable = VK_TRUE;
        depthState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthState.depthWriteEnable = VK_FALSE;

        VkPushConstantRange pushConstant { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2 };
        VkPipelineLayoutCreateInfo layoutInfo { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;
        check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout");

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
        info.layout = pipelineLayout;
        info.renderPass = renderPass;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "vkCreateGraphicsPipelines");

        vkDestroyShaderModule(device, vertexModule, nullptr);
        vkDestroyShaderModule(device, fragmentModule, nullptr);
    }

    void destroySwapchain()
    {
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
        destroyDepth();
        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
    }

    void recreateSwapchain()
    {
        vkDeviceWaitIdle(device);
        destroySwapchain();
        createSwapchain();
        swapchainDirty = false;
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

    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
    {
        Buffer result;
        VkBufferCreateInfo info { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device, &info, nullptr, &result.buffer), "vkCreateBuffer");

        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, result.buffer, &requirements);
        VkMemoryAllocateInfo allocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &result.memory), "vkAllocateMemory");
        vkBindBufferMemory(device, result.buffer, result.memory, 0);
        check(vkMapMemory(device, result.memory, 0, size, 0, &result.mapped), "vkMapMemory");
        result.capacity = size;
        return result;
    }

    void destroyBuffer(Buffer& buffer)
    {
        if (buffer.buffer == VK_NULL_HANDLE) {
            return;
        }
        vkUnmapMemory(device, buffer.memory);
        vkDestroyBuffer(device, buffer.buffer, nullptr);
        vkFreeMemory(device, buffer.memory, nullptr);
        buffer = Buffer {};
    }

    void ensure(Buffer& buffer, size_t needed, VkBufferUsageFlags usage)
    {
        if (buffer.capacity >= needed) {
            return;
        }
        VkDeviceSize capacity = std::max<VkDeviceSize>({ needed, buffer.capacity * 2, 64 * 1024 });
        destroyBuffer(buffer);
        buffer = createBuffer(capacity, usage);
    }

    void destroyAtlas()
    {
        if (atlasView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, atlasView, nullptr);
            atlasView = VK_NULL_HANDLE;
        }
        if (atlasImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, atlasImage, nullptr);
            atlasImage = VK_NULL_HANDLE;
        }
        if (atlasMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, atlasMemory, nullptr);
            atlasMemory = VK_NULL_HANDLE;
        }
    }

    void transition(VkCommandBuffer command, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
    {
        VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(command, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    GLFWwindow* glfwWindow;
    uint32_t width;
    uint32_t height;
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
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, FramesInFlight> commandBuffers {};
    std::array<VkSemaphore, FramesInFlight> imageAvailable {};
    std::array<VkFence, FramesInFlight> inFlight {};
    std::array<FrameBuffers, FramesInFlight> frameBuffers {};
    VkImage atlasImage = VK_NULL_HANDLE;
    VkDeviceMemory atlasMemory = VK_NULL_HANDLE;
    VkImageView atlasView = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    std::array<VkImage, BlockTexturePages> blockImages {};
    std::array<VkDeviceMemory, BlockTexturePages> blockMemories {};
    std::array<VkImageView, BlockTexturePages> blockViews {};
    VkImage entityImage = VK_NULL_HANDLE;
    VkDeviceMemory entityMemory = VK_NULL_HANDLE;
    VkImageView entityView = VK_NULL_HANDLE;
    uint32_t entitySize = 0;
    uint32_t entityLayers = 0;
    VkSampler blockSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout worldDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool worldDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet worldDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout worldLayout = VK_NULL_HANDLE;
    VkPipeline worldPipeline = VK_NULL_HANDLE;
    VkPipeline modelPipeline = VK_NULL_HANDLE;
    VkPipeline blendPipeline = VK_NULL_HANDLE;
    VkPipeline modelBlendPipeline = VK_NULL_HANDLE;
    VkPipeline skyPipeline = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, ChunkBuffer> chunks;
    std::vector<RetiredBuffer> retired;
    uint64_t frameCounter = 0;
    uint32_t frame = 0;
    uint64_t submissions = 0;
    uint32_t recordedOpaque = 0;
    std::array<CompletedFrame, FramesInFlight> slotFrames {};
    CompletedFrame completed;
    uint32_t imageIndex = 0;
    bool swapchainDirty = false;
    bool recording = false;
};

}

std::unique_ptr<Renderer> Renderer::create(Window& window)
{
    return std::make_unique<VulkanRenderer>(window);
}

}
