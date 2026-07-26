#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <android/native_window.h>
#include <android/asset_manager.h>
#include "CommonTypes.h"
#include "PanoramaView.h"

// VMA 句柄前置声明（完整定义在 VulkanRenderer.cpp 随 VMA_IMPLEMENTATION 展开，
// 与 vk_mem_alloc.h 内部的 VK_DEFINE_HANDLE 重复 typedef 同一类型，合法）
VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    static void setAssetManager(AAssetManager* assetManager);

    bool initialize(ANativeWindow* window, int width, int height);
    void cleanup();
    void render(float cameraX, float cameraY, float cameraZ, float pitch, float yaw);
    void updateUniformBuffer(float cameraX, float cameraY, float cameraZ,
                             float pitch, float yaw);
    void updateVertexBuffer(const std::vector<Vertex>& vertices);
    void recreateSwapchain(int width, int height);

    // ImGui Vulkan 后端接入（主界面渲染，第一步）
    bool initImGui();

    // GUI 纹理：加载 gui/<path>.png 并注册给 ImGui，返回 VkDescriptorSet 作 ImTextureID
    // （对应 GL 侧 ResourcepackManager::getGuiTexture 的角色，按路径缓存，失败返回 VK_NULL_HANDLE）
    // outWidth/outHeight 可选返回纹理像素尺寸（供标题图等按原图比例排版的场景）
    VkDescriptorSet getGuiTexture(const std::string& path, int* outWidth = nullptr, int* outHeight = nullptr);

    // 切屏后 Surface 释放/重建（防止向失效 Surface 提交）
    void invalidateSurface() { surfaceValid = false; }
    bool recreateSurface(ANativeWindow* window, int width, int height);

private:
    struct SwapchainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };
    bool checkValidationLayerSupport();
    bool createInstance();
    bool createSurface(ANativeWindow* window);
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createSwapchain(int width, int height);
    bool createImageViews();
    bool createRenderPass();
    bool createDescriptorSetLayout();
    bool createGraphicsPipeline();
    bool createFramebuffers();
    bool createCommandPool();
    bool createDepthResources();
    bool createVertexBuffer(const std::vector<Vertex>& vertices);
    bool createIndexBuffer(const std::vector<uint32_t>& indices);
    bool createUniformBuffer();
    bool createDescriptorPool();
    bool createDescriptorSets();
    bool createCommandBuffers();
    bool createSyncObjects();

    void cleanupSwapchain();

    // GUI 纹理上传（staging buffer → VkImage → SHADER_READ_ONLY）
    struct GuiTexture {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;  // ImGui_ImplVulkan_AddTexture 返回
        int width = 0;
        int height = 0;
    };
    bool uploadGuiTexture(const uint8_t* pixels, int width, int height, GuiTexture& out);
    void destroyGuiTextures();
    std::unordered_map<std::string, GuiTexture> guiTextureCache;

    // 主界面旋转全景背景（像素/几何/MVP 由 PanoramaView 提供，这里只管 Vulkan 资源与绘制）
    bool initPanorama();
    void renderPanorama(VkCommandBuffer cmd);
    void destroyPanoramaResources();

    std::vector<char> readFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    bool createAllocator();
    VkFormat findDepthFormat();
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    uint32_t findGraphicsQueueFamily();

    // 通用资源辅助（GUI 纹理与全景共用的创建/上传样板，内存统一由 VMA 分配）
    bool createHostBuffer(VkBufferUsageFlags usage, const void* src, VkDeviceSize size,
                          VkBuffer& outBuf, VmaAllocation& outAlloc);
    bool createDeviceImage(uint32_t width, uint32_t height, uint32_t layers,
                           VkImageCreateFlags flags, VkImage& outImage, VmaAllocation& outAlloc);
    bool createRgbaImageView(VkImage image, VkImageViewType type, uint32_t layers, VkImageView& outView);
    VkCommandBuffer beginOneTimeCommands();
    void endOneTimeCommands(VkCommandBuffer cmd);
    bool uploadPixelsToImage(const uint8_t* pixels, uint32_t width, uint32_t height,
                             uint32_t layers, VkImage image);

    static const int MAX_FRAMES_IN_FLIGHT = 1;

    // Core Vulkan objects
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    uint32_t graphicsQueueFamily = 0;

    // VMA 分配器（所有 buffer/image 内存经由它分配，大块预分配 + 子分配）
    VmaAllocator allocator = VK_NULL_HANDLE;

    // Swapchain
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    std::vector<VkImageView> swapchainImageViews;
    uint32_t swapchainMinImageCount = 2;

    // Depth buffer
    VkImage depthImage;
    VmaAllocation depthImageMemory;
    VkImageView depthImageView;

    // Pipeline
    VkRenderPass renderPass;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    // Framebuffers
    std::vector<VkFramebuffer> swapchainFramebuffers;

    // Command buffers
    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    // Vertex buffer
    VkBuffer vertexBuffer;
    VmaAllocation vertexBufferMemory;
    uint32_t vertexCount = 0;

    // Index buffer
    VkBuffer indexBuffer;
    VmaAllocation indexBufferMemory;
    uint32_t indexCount = 0;

    // Uniform buffer
    VkBuffer uniformBuffer;
    VmaAllocation uniformBufferMemory;
    void* uniformBufferMapped;

    // Descriptor sets
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;

    // Sync objects
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;

    int screenWidth = 0;
    int screenHeight = 0;

    // ImGui 状态
    bool imguiInitialized = false;
    bool surfaceValid = true;

    // 全景背景资源（cubemap + 独立管线，仅菜单模式使用）
    PanoramaView panoramaView;
    bool panoramaInitAttempted = false;
    bool panoramaReady = false;
    VkImage panoramaImage = VK_NULL_HANDLE;
    VmaAllocation panoramaImageMemory = VK_NULL_HANDLE;
    VkImageView panoramaImageView = VK_NULL_HANDLE;      // VK_IMAGE_VIEW_TYPE_CUBE
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
};
