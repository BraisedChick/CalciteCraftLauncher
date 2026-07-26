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
    VkDescriptorSet getGuiTexture(const std::string& path);

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
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;  // ImGui_ImplVulkan_AddTexture 返回
    };
    bool uploadGuiTexture(const uint8_t* pixels, int width, int height, GuiTexture& out);
    void destroyGuiTextures();
    std::unordered_map<std::string, GuiTexture> guiTextureCache;

    std::vector<char> readFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkFormat findDepthFormat();
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    uint32_t findGraphicsQueueFamily();

    static const int MAX_FRAMES_IN_FLIGHT = 1;

    // Core Vulkan objects
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    uint32_t graphicsQueueFamily = 0;

    // Swapchain
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    std::vector<VkImageView> swapchainImageViews;
    uint32_t swapchainMinImageCount = 2;

    // Depth buffer
    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
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
    VkDeviceMemory vertexBufferMemory;
    uint32_t vertexCount = 0;

    // Index buffer
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    uint32_t indexCount = 0;

    // Uniform buffer
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
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
};
