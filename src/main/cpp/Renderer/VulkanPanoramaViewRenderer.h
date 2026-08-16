#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "PanoramaView.h"

VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

class VulkanPanoramaViewRenderer {
public:
    VulkanPanoramaViewRenderer(VkDevice device,
                               VmaAllocator allocator,
                               VkPhysicalDevice physicalDevice,
                               VkRenderPass renderPass,
                               VkCommandPool commandPool,
                               VkQueue graphicsQueue);
    ~VulkanPanoramaViewRenderer();

    // 禁止拷贝
    VulkanPanoramaViewRenderer(const VulkanPanoramaViewRenderer&) = delete;
    VulkanPanoramaViewRenderer& operator=(const VulkanPanoramaViewRenderer&) = delete;

    // 初始化全景资源（cubemap、几何、管线等），失败返回 false
    bool init();

    // 当 swapchain 尺寸变化时更新视口大小（管线使用动态视口，只需记录尺寸）
    void updateExtent(uint32_t width, uint32_t height);

    // 当 renderPass 重建时，必须重新创建管线（因为管线引用 renderPass）
    bool recreatePipelines(VkRenderPass newRenderPass);

    // 渲染全景图到当前命令缓冲（需在 render pass 内调用）
    void render(VkCommandBuffer cmd);

    // 销毁所有资源（析构时自动调用，也可显式调用）
    void destroy();

    bool isReady() const { return ready; }

private:
    // 辅助工具（与 VulkanRenderer 中的实现类似，但独立，避免交叉依赖）
    bool createHostBuffer(VkBufferUsageFlags usage, const void* src, VkDeviceSize size,
                          VkBuffer& outBuf, VmaAllocation& outAlloc);
    bool createDeviceImage(uint32_t width, uint32_t height, uint32_t layers,
                           VkImageCreateFlags flags, VkImage& outImage, VmaAllocation& outAlloc);
    bool createRgbaImageView(VkImage image, VkImageViewType type, uint32_t layers, VkImageView& outView);
    VkCommandBuffer beginOneTimeCommands();
    void endOneTimeCommands(VkCommandBuffer cmd);
    bool uploadPixelsToImage(const uint8_t* pixels, uint32_t width, uint32_t height,
                             uint32_t layers, VkImage image);

    bool createPipelines();  // 使用当前 renderPass 创建管线
    void destroyResources();

    // 外部传入的 Vulkan 上下文（生命周期由外部管理）
    VkDevice device;
    VmaAllocator allocator;
    VkPhysicalDevice physicalDevice;
    VkRenderPass renderPass;
    VkCommandPool commandPool;
    VkQueue graphicsQueue;

    // 全景逻辑（图形无关）
    PanoramaView panoramaView;

    // 当前视口尺寸
    uint32_t extentWidth = 0;
    uint32_t extentHeight = 0;

    // Vulkan 资源
    VkImage panoramaImage = VK_NULL_HANDLE;
    VmaAllocation panoramaImageMemory = VK_NULL_HANDLE;
    VkImageView panoramaImageView = VK_NULL_HANDLE;
    VkSampler panoramaSampler = VK_NULL_HANDLE;
    VkBuffer panoramaVertexBuffer = VK_NULL_HANDLE;
    VmaAllocation panoramaVertexMemory = VK_NULL_HANDLE;
    VkBuffer panoramaIndexBuffer = VK_NULL_HANDLE;
    VmaAllocation panoramaIndexMemory = VK_NULL_HANDLE;
    uint32_t panoramaIndexCount = 0;

    VkDescriptorSetLayout panoramaSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool panoramaDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet panoramaDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout panoramaPipelineLayout = VK_NULL_HANDLE;
    VkPipeline panoramaPipeline = VK_NULL_HANDLE;

    bool ready = false;
};