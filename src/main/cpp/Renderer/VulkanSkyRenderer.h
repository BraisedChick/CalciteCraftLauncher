#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include "3rdparty/vk_mem_alloc.h"
#include "SkyRenderer.h"
struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T*;

class VulkanSkyRenderer {
public:
    VulkanSkyRenderer(VkDevice device, VmaAllocator allocator,
                      VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                      VkCommandPool commandPool, VkQueue graphicsQueue);
    ~VulkanSkyRenderer();

    bool init();
    void render(VkCommandBuffer cmd, const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                float skyR, float skyG, float skyB, const SkyRenderParams& params);
    void updateExtent(uint32_t width, uint32_t height);
    void destroy();

private:
    // ---------- 辅助函数 ----------
    bool createHostBuffer(VkBufferUsageFlags usage, const void* src, VkDeviceSize size,
                          VkBuffer& outBuf, VmaAllocation& outAlloc);
    bool createDeviceImage(uint32_t width, uint32_t height, uint32_t layers,
                           VkImageCreateFlags flags, VkImage& outImage, VmaAllocation& outAlloc);
    bool createRgbaImageView(VkImage image, VkImageViewType type, uint32_t layers, VkImageView& outView);
    VkCommandBuffer beginOneTimeCommands();
    void endOneTimeCommands(VkCommandBuffer cmd);
    bool uploadPixelsToImage(const uint8_t* pixels, uint32_t width, uint32_t height,
                             uint32_t layers, VkImage image);
    bool loadCelestialTexture(const std::string& path, VkImage& outImage,
                              VmaAllocation& outAlloc, VkImageView& outView);

    // ---------- 核心句柄 ----------
    VkDevice device;
    VmaAllocator allocator;
    VkPhysicalDevice physicalDevice;
    VkRenderPass renderPass;
    VkCommandPool commandPool;
    VkQueue graphicsQueue;
    VkExtent2D extent = { 1280, 720 };

    bool initialized = false;

    // ---------- 管线 ----------
    VkPipelineLayout skyColorPipelineLayout = VK_NULL_HANDLE;
    VkPipeline skyColorPipeline = VK_NULL_HANDLE;
    VkPipelineLayout skyTexturePipelineLayout = VK_NULL_HANDLE;
    VkPipeline skyTexturePipeline = VK_NULL_HANDLE;

    // ---------- 天空圆盘 ----------
    VkBuffer skyTopVertexBuffer = VK_NULL_HANDLE;
    VmaAllocation skyTopVertexMemory = VK_NULL_HANDLE;
    uint32_t skyTopVertexCount = 0;

    // ---------- 太阳 ----------
    VkBuffer skySunVertexBuffer = VK_NULL_HANDLE;
    VmaAllocation skySunVertexMemory = VK_NULL_HANDLE;
    VkBuffer skySunIndexBuffer = VK_NULL_HANDLE;
    VmaAllocation skySunIndexMemory = VK_NULL_HANDLE;
    uint32_t skySunIndexCount = 0;

    // ---------- 月亮 ----------
    VkBuffer skyMoonVertexBuffer = VK_NULL_HANDLE;
    VmaAllocation skyMoonVertexMemory = VK_NULL_HANDLE;
    VkBuffer skyMoonIndexBuffer = VK_NULL_HANDLE;
    VmaAllocation skyMoonIndexMemory = VK_NULL_HANDLE;
    uint32_t skyMoonIndexCount = 0;

    // ---------- 星星 ----------
    VkBuffer skyStarsVertexBuffer = VK_NULL_HANDLE;
    VmaAllocation skyStarsVertexMemory = VK_NULL_HANDLE;
    VkBuffer skyStarsIndexBuffer = VK_NULL_HANDLE;
    VmaAllocation skyStarsIndexMemory = VK_NULL_HANDLE;
    uint32_t skyStarsIndexCount = 0;

    // ---------- 纹理描述符（9 个 set：1 个太阳 + 8 个月亮）----------
    VkDescriptorSetLayout skyTextureSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool skyTextureDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet skyDescriptorSetSun = VK_NULL_HANDLE;
    VkDescriptorSet skyDescriptorSetMoon[8] = {};
    VkSampler skyTextureSampler = VK_NULL_HANDLE;

    // ---------- 太阳纹理 ----------
    VkImage sunTextureImage = VK_NULL_HANDLE;
    VmaAllocation sunTextureMemory = VK_NULL_HANDLE;
    VkImageView sunTextureView = VK_NULL_HANDLE;

    // ---------- 8 个月相纹理 ----------
    VkImage moonTextureImages[8] = {};
    VmaAllocation moonTextureMemories[8] = {};
    VkImageView moonTextureViews[8] = {};
};