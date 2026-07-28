#include "VulkanRenderer.h"
#include <android/log.h>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>  // offsetof
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "gui/GameUI.h"
#include "TextureLoader.h"
#include "Camera.h"
#include "stb_image.h"
#include "TextureAtlas.h"
#include "Light.h"
#include "ChunkMeshScheduler.h"
#include "BlockIconRasterizer.h"
#include "ClientEngine/ClientEngine.h"
#include "ClientEngine/GameEngine.h"

// VMA 实现（全项目唯一展开点）：直链 libvulkan 故用静态函数；
// 实例是 Vulkan 1.0，锁定函数集避免引用旧设备不存在的 1.1+ 入口
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_VULKAN_VERSION 1000000
#include "3rdparty/vk_mem_alloc.h"

#define LOG_TAG "VulkanRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static AAssetManager* g_assetManager = nullptr;

void VulkanRenderer::setAssetManager(AAssetManager* assetManager) {
    g_assetManager = assetManager;
}

VulkanRenderer::VulkanRenderer()
    : instance(VK_NULL_HANDLE),
      surface(VK_NULL_HANDLE),
      device(VK_NULL_HANDLE),
      swapchain(VK_NULL_HANDLE),
      renderPass(VK_NULL_HANDLE),
      commandPool(VK_NULL_HANDLE),
      imageAvailableSemaphore(VK_NULL_HANDLE),
      renderFinishedSemaphore(VK_NULL_HANDLE),
      inFlightFence(VK_NULL_HANDLE) {
}

VulkanRenderer::~VulkanRenderer() {
    cleanup();
}

bool VulkanRenderer::initialize(ANativeWindow* window, int width, int height) {
    screenWidth = width;
    screenHeight = height;

    if (!createInstance()) return false;
    if (!createSurface(window)) return false;
    if (!selectPhysicalDevice()) return false;
    if (!createLogicalDevice()) return false;
    if (!createAllocator()) return false;
    if (!createSwapchain(width, height)) return false;
    if (!createImageViews()) return false;
    if (!createRenderPass()) return false;
    if (!createDepthResources()) return false;
    if (!createFramebuffers()) return false;
    if (!createCommandPool()) return false;
    if (!createCommandBuffers()) return false;
    if (!createSyncObjects()) return false;

    LOGI("Vulkan renderer initialized successfully");
    return true;
}
bool VulkanRenderer::createDepthResources() {
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapchainExtent.width;
    imageInfo.extent.height = swapchainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaInfo{};
    vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(allocator, &imageInfo, &vmaInfo, &depthImage, &depthImageMemory, nullptr) != VK_SUCCESS) {
        LOGE("Failed to create depth image");
        return false;
    }

    // 创建深度视图
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
        LOGE("Failed to create depth image view");
        return false;
    }

    LOGI("Depth resources created");
    return true;
}

void VulkanRenderer::cleanup() {
    // 先等待 GPU 空闲，再销毁 ImGui 后端（必须在 device 销毁之前）
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }
    // GUI 纹理引用了 ImGui 后端描述符池，必须先于 ImGui_ImplVulkan_Shutdown 释放
    destroyGuiTextures();
    destroyPanoramaResources();
    destroyChunkResources();
    if (imguiInitialized) {
        GameUI::getInstance().shutdown();  // 内部调用 ImGui_ImplVulkan_Shutdown
        imguiInitialized = false;
    }

    cleanupSwapchain();

    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }

    if (imageAvailableSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
        imageAvailableSemaphore = VK_NULL_HANDLE;
    }
    if (renderFinishedSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
        renderFinishedSemaphore = VK_NULL_HANDLE;
    }
    if (inFlightFence != VK_NULL_HANDLE) {
        vkDestroyFence(device, inFlightFence, nullptr);
        inFlightFence = VK_NULL_HANDLE;
    }

    // 所有 VMA 分配已释放，销毁分配器（必须在 device 之前；若有泄漏 VMA 会断言/报告）
    if (allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }

    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }

    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::cleanupSwapchain() {
    // 区块管线依赖 renderPass，随 swapchain 一起销毁（recreate 路径按需重建）
    destroyChunkPipelines();

    for (auto framebuffer : swapchainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    swapchainFramebuffers.clear();

    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }

    if (depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, depthImage, depthImageMemory);
        depthImage = VK_NULL_HANDLE;
        depthImageMemory = VK_NULL_HANDLE;
    }

    // 只释放命令缓冲区，不销毁命令池
    if (commandPool != VK_NULL_HANDLE && !commandBuffers.empty()) {
        vkFreeCommandBuffers(device, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
        commandBuffers.clear();
    }

    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    swapchainImageViews.clear();

    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::recreateSwapchain(int width, int height) {
    vkDeviceWaitIdle(device);

    cleanupSwapchain();

    screenWidth = width;
    screenHeight = height;

    createSwapchain(width, height);
    createImageViews();
    createDepthResources();  // 重新创建深度资源
    createRenderPass();
    if (chunkResourcesReady) createChunkPipelines();
    createFramebuffers();
    createCommandBuffers();
}

// 切屏回来后重建 Surface + Swapchain（旧 Surface 已失效）
bool VulkanRenderer::recreateSurface(ANativeWindow* window, int width, int height) {
    if (device == VK_NULL_HANDLE || instance == VK_NULL_HANDLE) return false;
    vkDeviceWaitIdle(device);

    cleanupSwapchain();

    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    if (!createSurface(window)) return false;

    screenWidth = width;
    screenHeight = height;

    if (!createSwapchain(width, height)) return false;
    if (!createImageViews()) return false;
    if (!createDepthResources()) return false;
    if (!createRenderPass()) return false;
    if (chunkResourcesReady) createChunkPipelines();
    if (!createFramebuffers()) return false;
    if (!createCommandBuffers()) return false;

    surfaceValid = true;
    // 重置限帧基准（Surface 重建后时间线不连续）
    frameTimeBaseValid = false;
    LOGI("Vulkan surface recreated: %dx%d", width, height);
    return true;
}

// ImGui Vulkan 后端接入：先初始化 GameUI（字体/样式/回调），再初始化 Vulkan 后端
bool VulkanRenderer::initImGui() {
    if (imguiInitialized) return true;

    GameUI::getInstance().setVulkanBackend(true);
    if (!GameUI::getInstance().init()) {
        LOGE("Failed to init GameUI for Vulkan backend");
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)swapchainExtent.width, (float)swapchainExtent.height);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = device;
    initInfo.QueueFamily = graphicsQueueFamily;
    initInfo.Queue = graphicsQueue;
    initInfo.DescriptorPoolSize = 64;  // 后端自建描述符池
    initInfo.MinImageCount = swapchainMinImageCount;
    initInfo.ImageCount = (uint32_t)swapchainImages.size();
    initInfo.PipelineInfoMain.RenderPass = renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        LOGE("Failed to initialize ImGui Vulkan backend");
        return false;
    }

    imguiInitialized = true;
    LOGI("ImGui Vulkan backend initialized");
    return true;
}

// ============================================================
// 通用资源辅助：buffer/image 创建、一次性命令、staging 像素上传
// （GUI 纹理与全景 cubemap 共用，收敛重复样板）
// ============================================================

bool VulkanRenderer::createHostBuffer(VkBufferUsageFlags usage, const void* src, VkDeviceSize size,
                                      VkBuffer& outBuf, VmaAllocation& outAlloc) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaInfo{};
    vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;
    vmaInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    if (vmaCreateBuffer(allocator, &bufferInfo, &vmaInfo, &outBuf, &outAlloc, nullptr) != VK_SUCCESS) {
        return false;
    }

    // map + memcpy + flush 一步到位（coherent 内存上 flush 为空操作）
    if (src && vmaCopyMemoryToAllocation(allocator, src, outAlloc, 0, size) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, outBuf, outAlloc);
        outBuf = VK_NULL_HANDLE;
        outAlloc = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanRenderer::createDeviceImage(uint32_t width, uint32_t height, uint32_t layers,
                                       VkImageCreateFlags flags, VkImage& outImage, VmaAllocation& outAlloc) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = flags;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = layers;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // 无 host 访问需求，AUTO 自动选 DEVICE_LOCAL
    VmaAllocationCreateInfo vmaInfo{};
    vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;
    return vmaCreateImage(allocator, &imageInfo, &vmaInfo, &outImage, &outAlloc, nullptr) == VK_SUCCESS;
}

bool VulkanRenderer::createRgbaImageView(VkImage image, VkImageViewType type, uint32_t layers, VkImageView& outView) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = type;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers };
    return vkCreateImageView(device, &viewInfo, nullptr, &outView) == VK_SUCCESS;
}

VkCommandBuffer VulkanRenderer::beginOneTimeCommands() {
    VkCommandBufferAllocateInfo cbAlloc{};
    cbAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.commandPool = commandPool;
    cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cbAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void VulkanRenderer::endOneTimeCommands(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

// staging 上传 RGBA 像素（各层等尺寸、连续排列）到 image，完成后转 SHADER_READ_ONLY
bool VulkanRenderer::uploadPixelsToImage(const uint8_t* pixels, uint32_t width, uint32_t height,
                                         uint32_t layers, VkImage image) {
    VkDeviceSize layerBytes = (VkDeviceSize)width * height * 4;
    VkBuffer stagingBuffer;
    VmaAllocation stagingMemory;
    if (!createHostBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, pixels, layerBytes * layers,
                          stagingBuffer, stagingMemory)) {
        return false;
    }

    VkCommandBuffer cmd = beginOneTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers };
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    std::vector<VkBufferImageCopy> regions(layers);
    for (uint32_t i = 0; i < layers; i++) {
        regions[i] = {};
        regions[i].bufferOffset = layerBytes * i;
        regions[i].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, i, 1 };
        regions[i].imageExtent = { width, height, 1 };
    }
    vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           layers, regions.data());

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    endOneTimeCommands(cmd);

    vmaDestroyBuffer(allocator, stagingBuffer, stagingMemory);
    return true;
}

// ============================================================
// GUI 纹理（主界面按钮等 ImGui 用图）
// 像素解码复用 TextureLoader（图形 API 无关），这里只做 Vulkan 上传与 ImGui 注册
// ============================================================

VkDescriptorSet VulkanRenderer::getGuiTexture(const std::string& path, int* outWidth, int* outHeight) {
    return getAssetTexture("gui/" + path + ".png", outWidth, outHeight);
}

VkDescriptorSet VulkanRenderer::getAssetTexture(const std::string& assetPath, int* outWidth, int* outHeight) {
    auto it = guiTextureCache.find(assetPath);
    if (it != guiTextureCache.end()) {
        if (outWidth) *outWidth = it->second.width;
        if (outHeight) *outHeight = it->second.height;
        return it->second.descriptorSet;
    }

    // AddTexture 依赖 ImGui Vulkan 后端的描述符池
    if (!imguiInitialized) return VK_NULL_HANDLE;

    TextureData tex = TextureLoader::loadImage(assetPath);
    if (!tex.data || tex.width <= 0 || tex.height <= 0) {
        LOGE("Failed to load GUI texture: %s", assetPath.c_str());
        guiTextureCache[assetPath] = GuiTexture{};  // 缓存失败，避免每帧重试
        return VK_NULL_HANDLE;
    }

    GuiTexture gt;
    if (!uploadGuiTexture(tex.data, tex.width, tex.height, gt)) {
        LOGE("Failed to upload GUI texture: %s", assetPath.c_str());
        guiTextureCache[assetPath] = GuiTexture{};
        return VK_NULL_HANDLE;
    }
    gt.width = tex.width;
    gt.height = tex.height;

    // 注册给 ImGui：VkDescriptorSet 即 ImTextureID
    gt.descriptorSet = ImGui_ImplVulkan_AddTexture(gt.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    guiTextureCache[assetPath] = gt;
    LOGI("GUI texture loaded (Vulkan): %s (%dx%d)", assetPath.c_str(), tex.width, tex.height);
    if (outWidth) *outWidth = gt.width;
    if (outHeight) *outHeight = gt.height;
    return gt.descriptorSet;
}

VkDescriptorSet VulkanRenderer::getMemoryTexture(const std::string& cacheKey, const uint8_t* pngData, size_t pngSize) {
    auto it = guiTextureCache.find(cacheKey);
    if (it != guiTextureCache.end()) return it->second.descriptorSet;

    if (!imguiInitialized || !pngData || pngSize == 0) return VK_NULL_HANDLE;

    int w = 0, h = 0, ch = 0;
    uint8_t* pixels = stbi_load_from_memory(pngData, (int)pngSize, &w, &h, &ch, 4);
    if (!pixels || w <= 0 || h <= 0) {
        LOGE("Failed to decode memory texture: %s", cacheKey.c_str());
        guiTextureCache[cacheKey] = GuiTexture{};  // 缓存失败，避免每帧重试
        return VK_NULL_HANDLE;
    }

    GuiTexture gt;
    bool ok = uploadGuiTexture(pixels, w, h, gt);
    stbi_image_free(pixels);
    if (!ok) {
        LOGE("Failed to upload memory texture: %s", cacheKey.c_str());
        guiTextureCache[cacheKey] = GuiTexture{};
        return VK_NULL_HANDLE;
    }
    gt.width = w;
    gt.height = h;

    gt.descriptorSet = ImGui_ImplVulkan_AddTexture(gt.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    guiTextureCache[cacheKey] = gt;
    LOGI("Memory texture loaded (Vulkan): %s (%dx%d)", cacheKey.c_str(), w, h);
    return gt.descriptorSet;
}

VkDescriptorSet VulkanRenderer::getItemTexture(const std::string& itemName) {
    std::string cacheKey = "item:" + itemName;
    auto it = guiTextureCache.find(cacheKey);
    if (it != guiTextureCache.end()) return it->second.descriptorSet;

    if (!imguiInitialized) return VK_NULL_HANDLE;

    // 1) 3D 方块图标：CPU 光栅化（BlockIconRasterizer 与 GL 侧共用纯逻辑层）
    auto* atlas = ClientEngine::getInstance() ? ClientEngine::getInstance()->getTextureAtlas() : nullptr;
    if (atlas) {
        const auto& modelCache = atlas->getItemModelCache();
        auto mit = modelCache.find(itemName);
        if (mit != modelCache.end()) {
            std::vector<uint8_t> pixels;
            if (BlockIconRasterizer::rasterize(atlas, mit->second, 64, pixels)) {
                GuiTexture gt;
                if (uploadGuiTexture(pixels.data(), 64, 64, gt)) {
                    gt.width = 64;
                    gt.height = 64;
                    gt.descriptorSet = ImGui_ImplVulkan_AddTexture(gt.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    guiTextureCache[cacheKey] = gt;
                    return gt.descriptorSet;
                }
            }
        }
    }

    // 2) 2D 贴图回退：item/ 优先（工具等非方块），再试 block/
    for (const char* prefix : {"item/", "block/"}) {
        TextureData tex = TextureLoader::loadImage(std::string(prefix) + itemName + ".png");
        if (!tex.data || tex.width <= 0 || tex.height <= 0) continue;
        GuiTexture gt;
        if (!uploadGuiTexture(tex.data, tex.width, tex.height, gt)) break;
        gt.width = tex.width;
        gt.height = tex.height;
        gt.descriptorSet = ImGui_ImplVulkan_AddTexture(gt.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        guiTextureCache[cacheKey] = gt;
        return gt.descriptorSet;
    }

    LOGE("Failed to load item texture (Vulkan): %s", itemName.c_str());
    guiTextureCache[cacheKey] = GuiTexture{};  // 缓存失败，避免每帧重试
    return VK_NULL_HANDLE;
}

bool VulkanRenderer::uploadGuiTexture(const uint8_t* pixels, int width, int height, GuiTexture& out) {
    if (!createDeviceImage((uint32_t)width, (uint32_t)height, 1, 0, out.image, out.memory)) return false;
    if (!uploadPixelsToImage(pixels, (uint32_t)width, (uint32_t)height, 1, out.image) ||
        !createRgbaImageView(out.image, VK_IMAGE_VIEW_TYPE_2D, 1, out.view)) {
        vmaDestroyImage(allocator, out.image, out.memory);
        out.image = VK_NULL_HANDLE;
        out.memory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void VulkanRenderer::destroyGuiTextures() {
    for (auto& [path, gt] : guiTextureCache) {
        if (gt.descriptorSet != VK_NULL_HANDLE) ImGui_ImplVulkan_RemoveTexture(gt.descriptorSet);
        if (gt.view != VK_NULL_HANDLE) vkDestroyImageView(device, gt.view, nullptr);
        if (gt.image != VK_NULL_HANDLE) vmaDestroyImage(allocator, gt.image, gt.memory);
    }
    guiTextureCache.clear();
}

// ============================================================
// 主界面旋转全景背景
// 面像素/立方体几何/MVP 由 PanoramaView 提供（图形 API 无关），
// 这里负责 cubemap 上传、独立管线与绘制（对应 GL 侧 initPanorama/renderPanoramaToFBO）
// ============================================================

bool VulkanRenderer::initPanorama() {
    // 1. 加载 6 个面像素（已水平翻转，缺失面为占位色）
    PanoramaView::FacePixels faces[6];
    int loadedCount = panoramaView.loadFacePixels(faces);
    LOGI("Panorama faces loaded (Vulkan): %d/6", loadedCount);

    // cubemap 要求 6 层等尺寸：以首个成功面为准，尺寸不符的面（如占位色）最近邻重采样
    int faceSize = 16;
    for (int i = 0; i < 6; i++) {
        if (faces[i].loaded) { faceSize = faces[i].width; break; }
    }
    VkDeviceSize faceBytes = (VkDeviceSize)faceSize * faceSize * 4;
    std::vector<uint8_t> allPixels((size_t)faceBytes * 6);
    for (int i = 0; i < 6; i++) {
        uint8_t* dst = allPixels.data() + (size_t)faceBytes * i;
        const PanoramaView::FacePixels& f = faces[i];
        if (f.width == faceSize && f.height == faceSize) {
            memcpy(dst, f.rgba.data(), (size_t)faceBytes);
        } else {
            for (int y = 0; y < faceSize; y++) {
                int sy = y * f.height / faceSize;
                for (int x = 0; x < faceSize; x++) {
                    int sx = x * f.width / faceSize;
                    memcpy(dst + ((size_t)y * faceSize + x) * 4,
                           f.rgba.data() + ((size_t)sy * f.width + sx) * 4, 4);
                }
            }
        }
    }

    // 2. cubemap 创建 + staging 上传（6 面连续，layout 转换在一次性命令内完成）+ CUBE 视图
    if (!createDeviceImage((uint32_t)faceSize, (uint32_t)faceSize, 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
                           panoramaImage, panoramaImageMemory) ||
        !uploadPixelsToImage(allPixels.data(), (uint32_t)faceSize, (uint32_t)faceSize, 6, panoramaImage) ||
        !createRgbaImageView(panoramaImage, VK_IMAGE_VIEW_TYPE_CUBE, 6, panoramaImageView)) {
        destroyPanoramaResources();
        return false;
    }

    // 3. 采样器（Linear + CLAMP_TO_EDGE）
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &panoramaSampler) != VK_SUCCESS) {
        destroyPanoramaResources();
        return false;
    }

    // 4. 立方体顶点/索引 buffer（几何来自 PanoramaView，小数据量直接 HOST_VISIBLE）
    size_t cubeVertFloats = 0, cubeIdxCount = 0;
    const float* cubeVerts = PanoramaView::cubeVertices(cubeVertFloats);
    const uint16_t* cubeIdx = PanoramaView::cubeIndices(cubeIdxCount);
    panoramaIndexCount = (uint32_t)cubeIdxCount;

    if (!createHostBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, cubeVerts,
                          cubeVertFloats * sizeof(float), panoramaVertexBuffer, panoramaVertexMemory) ||
        !createHostBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, cubeIdx,
                          cubeIdxCount * sizeof(uint16_t), panoramaIndexBuffer, panoramaIndexMemory)) {
        destroyPanoramaResources();
        return false;
    }

    // 5. 描述符（独立小池：1 个 combined image sampler）
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &panoramaSetLayout) != VK_SUCCESS) {
        destroyPanoramaResources();
        return false;
    }

    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &panoramaDescriptorPool) != VK_SUCCESS) {
        destroyPanoramaResources();
        return false;
    }

    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = panoramaDescriptorPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &panoramaSetLayout;
    if (vkAllocateDescriptorSets(device, &dsAlloc, &panoramaDescriptorSet) != VK_SUCCESS) {
        destroyPanoramaResources();
        return false;
    }

    VkDescriptorImageInfo imageDescInfo{};
    imageDescInfo.sampler = panoramaSampler;
    imageDescInfo.imageView = panoramaImageView;
    imageDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = panoramaDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageDescInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    // 6. 管线（push constant 传 MVP，动态 viewport/scissor，无深度读写）
    auto vertCode = readFile("shaders/panorama_vert.spv");
    auto fragCode = readFile("shaders/panorama_frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        LOGE("Panorama SPIR-V shaders missing (shaders/panorama_*.spv), fallback to clear color");
        destroyPanoramaResources();
        return false;
    }
    VkShaderModule vertModule = createShaderModule(vertCode);
    VkShaderModule fragModule = createShaderModule(fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        destroyPanoramaResources();
        return false;
    }

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &panoramaSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(device, &plInfo, nullptr, &panoramaPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        destroyPanoramaResources();
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = 3 * sizeof(float);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrDesc{};
    attrDesc.binding = 0;
    attrDesc.location = 0;
    attrDesc.format = VK_FORMAT_R32G32B32_SFLOAT;
    attrDesc.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attrDesc;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // 动态 viewport/scissor：swapchain 尺寸变化时无需重建管线
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;   // 相机在立方体内部，与 GL 侧一致不剔面
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 背景层：不读不写深度（render pass 含深度附件，状态必须提供）
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = panoramaPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkResult pr = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &panoramaPipeline);
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
    if (pr != VK_SUCCESS) {
        LOGE("Failed to create panorama pipeline");
        destroyPanoramaResources();
        return false;
    }

    LOGI("Panorama initialized (Vulkan): faceSize=%d, %d/6 faces", faceSize, loadedCount);
    return true;
}

void VulkanRenderer::renderPanorama(VkCommandBuffer cmd) {
    // 旋转动画 MVP 由 PanoramaView 计算，叠加 Vulkan clip 空间 Y 翻转修正
    glm::mat4 mvp = panoramaView.computeMVP((int)swapchainExtent.width, (int)swapchainExtent.height);
    glm::mat4 yFlip(1.0f);
    yFlip[1][1] = -1.0f;
    mvp = yFlip * mvp;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, panoramaPipeline);

    VkViewport viewport{};
    viewport.width = (float)swapchainExtent.width;
    viewport.height = (float)swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ {0, 0}, swapchainExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdPushConstants(cmd, panoramaPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, panoramaPipelineLayout,
                            0, 1, &panoramaDescriptorSet, 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &panoramaVertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, panoramaIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cmd, panoramaIndexCount, 1, 0, 0, 0);
}

void VulkanRenderer::destroyPanoramaResources() {
    if (panoramaPipeline != VK_NULL_HANDLE)        { vkDestroyPipeline(device, panoramaPipeline, nullptr); panoramaPipeline = VK_NULL_HANDLE; }
    if (panoramaPipelineLayout != VK_NULL_HANDLE)  { vkDestroyPipelineLayout(device, panoramaPipelineLayout, nullptr); panoramaPipelineLayout = VK_NULL_HANDLE; }
    if (panoramaDescriptorPool != VK_NULL_HANDLE)  { vkDestroyDescriptorPool(device, panoramaDescriptorPool, nullptr); panoramaDescriptorPool = VK_NULL_HANDLE; }
    panoramaDescriptorSet = VK_NULL_HANDLE;  // 随池销毁
    if (panoramaSetLayout != VK_NULL_HANDLE)       { vkDestroyDescriptorSetLayout(device, panoramaSetLayout, nullptr); panoramaSetLayout = VK_NULL_HANDLE; }
    if (panoramaVertexBuffer != VK_NULL_HANDLE)    { vmaDestroyBuffer(allocator, panoramaVertexBuffer, panoramaVertexMemory); panoramaVertexBuffer = VK_NULL_HANDLE; panoramaVertexMemory = VK_NULL_HANDLE; }
    if (panoramaIndexBuffer != VK_NULL_HANDLE)     { vmaDestroyBuffer(allocator, panoramaIndexBuffer, panoramaIndexMemory); panoramaIndexBuffer = VK_NULL_HANDLE; panoramaIndexMemory = VK_NULL_HANDLE; }
    if (panoramaSampler != VK_NULL_HANDLE)         { vkDestroySampler(device, panoramaSampler, nullptr); panoramaSampler = VK_NULL_HANDLE; }
    if (panoramaImageView != VK_NULL_HANDLE)       { vkDestroyImageView(device, panoramaImageView, nullptr); panoramaImageView = VK_NULL_HANDLE; }
    if (panoramaImage != VK_NULL_HANDLE)           { vmaDestroyImage(allocator, panoramaImage, panoramaImageMemory); panoramaImage = VK_NULL_HANDLE; panoramaImageMemory = VK_NULL_HANDLE; }
    panoramaReady = false;
}

// ============================================================
// 区块渲染（消费图形 API 无关的 ChunkMeshScheduler 产出）
// 三段绘制与 GLRenderer 对位：cutout 基体（写深度）→ overlay（不写）
// → water（translucent shader，不写），索引顺序 base | overlay | water
// ============================================================

// std140 布局，与 chunk.vert / chunk_*.frag 的 ChunkUBO 一致（176B）
struct ChunkUBO {
    float modelViewMat[16];
    float projMat[16];       // 已含 Vulkan clip 空间 Y 翻转
    float fogColor[4];
    float fogParams[4];      // x = FogStart, y = FogEnd
    float colorModulator[4];
};

bool VulkanRenderer::initChunkResources() {
    auto* engine = ClientEngine::getInstance();
    auto* atlas = engine ? engine->getTextureAtlas() : nullptr;
    if (!atlas || atlas->getLayerCount() <= 0) {
        LOGE("initChunkResources: TextureAtlas not initialized");
        return false;
    }
    atlasLayerCount = atlas->getLayerCount();

    // 首张纹理定图集层尺寸（缺失时用 16x16 占位）
    {
        TextureData firstTex = TextureLoader::loadImage(atlas->getTextureFileName(0));
        if (firstTex.data) {
            atlasWidth = firstTex.width;
            atlasHeight = firstTex.height;
        }
    }
    LOGI("Chunk atlas: %d layers, %dx%d", atlasLayerCount, atlasWidth, atlasHeight);

    // 图集 2D array：先建 image/view，像素分帧解码到累积区后一次上传（processChunkAtlas）
    atlasNextLayer = 0;
    atlasReady = false;
    atlasStagingPixels.assign((size_t)atlasWidth * atlasHeight * 4 * atlasLayerCount, 0);
    if (!createDeviceImage((uint32_t)atlasWidth, (uint32_t)atlasHeight, (uint32_t)atlasLayerCount, 0,
                           chunkAtlasImage, chunkAtlasMemory) ||
        !createRgbaImageView(chunkAtlasImage, VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                             (uint32_t)atlasLayerCount, chunkAtlasView)) {
        LOGE("Failed to create chunk atlas image");
        destroyChunkResources();
        return false;
    }

    // 图集采样器：NEAREST + REPEAT（与 GL 侧方块纹理参数对齐，暂不做 mipmap）
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &chunkAtlasSampler) != VK_SUCCESS) {
        destroyChunkResources();
        return false;
    }

    // 光照贴图 16x16：先上传全白（转入 SHADER_READ_ONLY，保证首帧采样合法），
    // 之后像素变化时整图重传（updateChunkLightmap）
    {
        std::vector<uint8_t> white(16 * 16 * 4, 255);
        if (!createDeviceImage(16, 16, 1, 0, lightmapImage, lightmapMemory) ||
            !uploadPixelsToImage(white.data(), 16, 16, 1, lightmapImage) ||
            !createRgbaImageView(lightmapImage, VK_IMAGE_VIEW_TYPE_2D, 1, lightmapView)) {
            LOGE("Failed to create lightmap image");
            destroyChunkResources();
            return false;
        }
        VkSamplerCreateInfo lmSampler{};
        lmSampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        lmSampler.magFilter = VK_FILTER_LINEAR;
        lmSampler.minFilter = VK_FILTER_LINEAR;
        lmSampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        lmSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        lmSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        lmSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device, &lmSampler, nullptr, &lightmapSampler) != VK_SUCCESS) {
            destroyChunkResources();
            return false;
        }
        // 失效缓存：保证首次 getLightmapPixelsIfChanged 必返回真实像素
        auto* game = engine->getGame();
        if (game && game->getLight()) game->getLight()->invalidateLightmapCache();
    }

    // 区块 UBO（VMA 持久映射，每帧 memcpy + flush）
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(ChunkUBO);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo vmaInfo{};
        vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;
        vmaInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                        VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocResult{};
        if (vmaCreateBuffer(allocator, &bufferInfo, &vmaInfo, &chunkUniformBuffer,
                            &chunkUniformMemory, &allocResult) != VK_SUCCESS) {
            LOGE("Failed to create chunk uniform buffer");
            destroyChunkResources();
            return false;
        }
        chunkUniformMapped = allocResult.pMappedData;
    }

    // 描述符：b0=UBO(VERT|FRAG) b1=图集(FRAG) b2=lightmap(VERT，Mojang 在顶点阶段采样)
    {
        VkDescriptorSetLayoutBinding bindings[3]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 3;
        layoutInfo.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &chunkSetLayout) != VK_SUCCESS) {
            destroyChunkResources();
            return false;
        }

        VkDescriptorPoolSize poolSizes[2] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 },
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &chunkDescriptorPool) != VK_SUCCESS) {
            destroyChunkResources();
            return false;
        }

        VkDescriptorSetAllocateInfo dsAlloc{};
        dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAlloc.descriptorPool = chunkDescriptorPool;
        dsAlloc.descriptorSetCount = 1;
        dsAlloc.pSetLayouts = &chunkSetLayout;
        if (vkAllocateDescriptorSets(device, &dsAlloc, &chunkDescriptorSet) != VK_SUCCESS) {
            destroyChunkResources();
            return false;
        }

        // 一次写齐三个绑定（图集此时 layout 还是 UNDEFINED，但 atlasReady 前不会发生 draw 引用，合法）
        VkDescriptorBufferInfo uboInfo{ chunkUniformBuffer, 0, sizeof(ChunkUBO) };
        VkDescriptorImageInfo atlasInfo{ chunkAtlasSampler, chunkAtlasView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo lightInfo{ lightmapSampler, lightmapView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet writes[3]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = chunkDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &uboInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = chunkDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &atlasInfo;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = chunkDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].pImageInfo = &lightInfo;
        vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
    }

    // 管线布局（三条管线共用）
    {
        VkPipelineLayoutCreateInfo plInfo{};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &chunkSetLayout;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &chunkPipelineLayout) != VK_SUCCESS) {
            destroyChunkResources();
            return false;
        }
    }

    if (!createChunkPipelines()) {
        destroyChunkResources();
        return false;
    }

    LOGI("Chunk resources initialized (Vulkan)");
    return true;
}

bool VulkanRenderer::createChunkPipelines() {
    destroyChunkPipelines();  // recreate 路径幂等

    auto vertCode = readFile("shaders/chunk_vert.spv");
    auto cutoutCode = readFile("shaders/chunk_cutout_frag.spv");
    auto translucentCode = readFile("shaders/chunk_translucent_frag.spv");
    if (vertCode.empty() || cutoutCode.empty() || translucentCode.empty()) {
        LOGE("Chunk SPIR-V shaders missing (shaders/chunk_*.spv)");
        return false;
    }
    VkShaderModule vertModule = createShaderModule(vertCode);
    VkShaderModule cutoutModule = createShaderModule(cutoutCode);
    VkShaderModule translucentModule = createShaderModule(translucentCode);
    if (vertModule == VK_NULL_HANDLE || cutoutModule == VK_NULL_HANDLE || translucentModule == VK_NULL_HANDLE) {
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (cutoutModule) vkDestroyShaderModule(device, cutoutModule, nullptr);
        if (translucentModule) vkDestroyShaderModule(device, translucentModule, nullptr);
        return false;
    }

    // 顶点输入：PackedVertex 32B，normal 着色器未消费故省略；
    // uv2 用 UINT（USCALED 非强制支持格式），shader 侧 uvec2 再转 float
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(PackedVertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[5]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PackedVertex, pos) };
    attrs[1] = { 1, 0, VK_FORMAT_R16G16_UNORM,     offsetof(PackedVertex, uv) };
    attrs[2] = { 2, 0, VK_FORMAT_R32_SFLOAT,       offsetof(PackedVertex, texIndex) };
    attrs[3] = { 3, 0, VK_FORMAT_R8G8B8A8_UNORM,   offsetof(PackedVertex, color) };
    attrs[4] = { 4, 0, VK_FORMAT_R16G16_UINT,      offsetof(PackedVertex, uv2) };
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = 5;
    vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // 网格绕序是 GL 惯例 CCW；Vulkan 的朝向在 framebuffer 空间（Y 向下）判定，
    // 符号约定已吸收 Y 轴差异：proj Y 翻转后 GL-CCW 几何仍是 COUNTER_CLOCKWISE 正面
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 三阶段均开 alpha blend（与 GL 侧一致：玻璃/树叶/水）
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    // 只有 frag module 与 depthWrite 不同，其余状态共享
    auto buildPipeline = [&](VkShaderModule fragModule, VkBool32 depthWrite, VkPipeline& outPipeline) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

        // 对齐原版 RenderSystem 全局 LEQUAL：共面几何后画者确定性覆盖
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = depthWrite;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = chunkPipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        return vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline) == VK_SUCCESS;
    };

    bool ok = buildPipeline(cutoutModule, VK_TRUE, chunkPipelineCutout) &&
              buildPipeline(cutoutModule, VK_FALSE, chunkPipelineOverlay) &&
              buildPipeline(translucentModule, VK_FALSE, chunkPipelineWater);

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, cutoutModule, nullptr);
    vkDestroyShaderModule(device, translucentModule, nullptr);

    if (!ok) {
        LOGE("Failed to create chunk pipelines");
        destroyChunkPipelines();
        return false;
    }
    LOGI("Chunk pipelines created (cutout/overlay/water)");
    return true;
}

void VulkanRenderer::destroyChunkPipelines() {
    if (chunkPipelineCutout != VK_NULL_HANDLE)  { vkDestroyPipeline(device, chunkPipelineCutout, nullptr); chunkPipelineCutout = VK_NULL_HANDLE; }
    if (chunkPipelineOverlay != VK_NULL_HANDLE) { vkDestroyPipeline(device, chunkPipelineOverlay, nullptr); chunkPipelineOverlay = VK_NULL_HANDLE; }
    if (chunkPipelineWater != VK_NULL_HANDLE)   { vkDestroyPipeline(device, chunkPipelineWater, nullptr); chunkPipelineWater = VK_NULL_HANDLE; }
}

void VulkanRenderer::destroyChunkResources() {
    destroyChunkPipelines();
    if (chunkPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, chunkPipelineLayout, nullptr); chunkPipelineLayout = VK_NULL_HANDLE; }
    if (chunkDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device, chunkDescriptorPool, nullptr); chunkDescriptorPool = VK_NULL_HANDLE; }
    chunkDescriptorSet = VK_NULL_HANDLE;  // 随池销毁
    if (chunkSetLayout != VK_NULL_HANDLE)      { vkDestroyDescriptorSetLayout(device, chunkSetLayout, nullptr); chunkSetLayout = VK_NULL_HANDLE; }
    if (chunkUniformBuffer != VK_NULL_HANDLE)  { vmaDestroyBuffer(allocator, chunkUniformBuffer, chunkUniformMemory); chunkUniformBuffer = VK_NULL_HANDLE; chunkUniformMemory = VK_NULL_HANDLE; chunkUniformMapped = nullptr; }
    if (chunkAtlasSampler != VK_NULL_HANDLE)   { vkDestroySampler(device, chunkAtlasSampler, nullptr); chunkAtlasSampler = VK_NULL_HANDLE; }
    if (chunkAtlasView != VK_NULL_HANDLE)      { vkDestroyImageView(device, chunkAtlasView, nullptr); chunkAtlasView = VK_NULL_HANDLE; }
    if (chunkAtlasImage != VK_NULL_HANDLE)     { vmaDestroyImage(allocator, chunkAtlasImage, chunkAtlasMemory); chunkAtlasImage = VK_NULL_HANDLE; chunkAtlasMemory = VK_NULL_HANDLE; }
    if (lightmapSampler != VK_NULL_HANDLE)     { vkDestroySampler(device, lightmapSampler, nullptr); lightmapSampler = VK_NULL_HANDLE; }
    if (lightmapView != VK_NULL_HANDLE)        { vkDestroyImageView(device, lightmapView, nullptr); lightmapView = VK_NULL_HANDLE; }
    if (lightmapImage != VK_NULL_HANDLE)       { vmaDestroyImage(allocator, lightmapImage, lightmapMemory); lightmapImage = VK_NULL_HANDLE; lightmapMemory = VK_NULL_HANDLE; }
    for (auto& [chunkKey, renderData] : chunkRenderCache) {
        for (auto& sec : renderData.sections) destroySectionBuffers(sec);
    }
    chunkRenderCache.clear();
    atlasStagingPixels.clear();
    atlasStagingPixels.shrink_to_fit();
    atlasReady = false;
    atlasNextLayer = 0;
    chunkResourcesReady = false;
    chunkInitAttempted = false;  // 允许下次进游戏重新初始化
}

void VulkanRenderer::destroySectionBuffers(ChunkSectionRenderData& sec) {
    if (sec.vertexBuffer != VK_NULL_HANDLE) { vmaDestroyBuffer(allocator, sec.vertexBuffer, sec.vertexMemory); sec.vertexBuffer = VK_NULL_HANDLE; sec.vertexMemory = VK_NULL_HANDLE; }
    if (sec.indexBuffer != VK_NULL_HANDLE)  { vmaDestroyBuffer(allocator, sec.indexBuffer, sec.indexMemory); sec.indexBuffer = VK_NULL_HANDLE; sec.indexMemory = VK_NULL_HANDLE; }
}

// 分帧解码方块纹理到 CPU 累积区（每帧 TEXTURES_PER_FRAME 张，避免卡顿），
// 全部就绪后一次 staging 上传 2D array（对位 GL 侧 finishTextureInit）
void VulkanRenderer::processChunkAtlas() {
    if (atlasReady || chunkAtlasImage == VK_NULL_HANDLE || atlasNextLayer >= atlasLayerCount) return;
    auto* atlas = ClientEngine::getInstance() ? ClientEngine::getInstance()->getTextureAtlas() : nullptr;
    if (!atlas) return;

    size_t layerBytes = (size_t)atlasWidth * atlasHeight * 4;
    int end = std::min(atlasNextLayer + TEXTURES_PER_FRAME, atlasLayerCount);
    for (int i = atlasNextLayer; i < end; i++) {
        uint8_t* dst = atlasStagingPixels.data() + layerBytes * i;
        TextureData texData = TextureLoader::loadImage(atlas->getTextureFileName(i));
        if (texData.data && texData.width == atlasWidth && texData.height == atlasHeight) {
            memcpy(dst, texData.data, layerBytes);
        } else {
            // 缺失/尺寸不符：占位色填充（与 GL 侧同策略）
            uint8_t r, g, b;
            atlas->getPlaceholderColor(i, r, g, b);
            for (size_t p = 0; p < layerBytes; p += 4) {
                dst[p + 0] = r;
                dst[p + 1] = g;
                dst[p + 2] = b;
                dst[p + 3] = 255;
            }
        }
    }
    atlasNextLayer = end;

    if (atlasNextLayer >= atlasLayerCount) {
        if (uploadPixelsToImage(atlasStagingPixels.data(), (uint32_t)atlasWidth, (uint32_t)atlasHeight,
                                (uint32_t)atlasLayerCount, chunkAtlasImage)) {
            atlasReady = true;
            LOGI("Chunk atlas uploaded: %d layers", atlasLayerCount);
        } else {
            LOGE("Chunk atlas upload failed");
        }
        atlasStagingPixels.clear();
        atlasStagingPixels.shrink_to_fit();
    }
}

// 昼夜循环：Light 产出像素，变化时整图重传（16x16，小到可忽略；
// oldLayout 用 UNDEFINED 合法因为全覆盖重写）
void VulkanRenderer::updateChunkLightmap() {
    auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!game || !game->getLight() || lightmapImage == VK_NULL_HANDLE) return;
    auto* light = game->getLight();
    light->update();

    uint8_t lightmapPixels[16 * 16 * 4];
    if (light->getLightmapPixelsIfChanged(lightmapPixels)) {
        uploadPixelsToImage(lightmapPixels, 16, 16, 1, lightmapImage);
    }
}

// pollRemovals + pollResults → section buffer 上传（镜像 GL processCompletedWork，
// 仅渲染线程访问故无锁；帧首 fence 已过，删旧 buffer 安全）
void VulkanRenderer::processChunkCompletedWork() {
    auto* scheduler = ClientEngine::getInstance() ? ClientEngine::getInstance()->getMeshScheduler() : nullptr;
    if (!scheduler) return;

    // 先处理服务端要求卸载的区块（避免给已卸载区块白建资源）
    std::vector<uint64_t> removeKeys;
    if (scheduler->pollRemovals(removeKeys)) {
        for (uint64_t chunkKey : removeKeys) {
            auto it = chunkRenderCache.find(chunkKey);
            if (it == chunkRenderCache.end()) continue;
            for (auto& sec : it->second.sections) destroySectionBuffers(sec);
            chunkRenderCache.erase(it);
        }
    }

    std::vector<ChunkMeshResult> results;
    if (scheduler->pollResults(results, MAX_CHUNKS_PER_FRAME) == 0) return;

    for (auto& result : results) {
        auto it = chunkRenderCache.find(result.chunkKey);
        if (it == chunkRenderCache.end()) {
            if (result.sections.empty()) continue;
            int chunkX = (int)(result.chunkKey >> 32);
            int chunkZ = (int)(result.chunkKey & 0xFFFFFFFF);
            it = chunkRenderCache.try_emplace(result.chunkKey).first;
            it->second.posX = chunkX * 16.0f;
            it->second.posZ = chunkZ * 16.0f;
        }
        auto& renderData = it->second;

        // 只删除掩码覆盖的旧 section 资源（部分替换，其余 section 不动）
        for (auto secIt = renderData.sections.begin(); secIt != renderData.sections.end();) {
            if (result.sectionMask & (1ULL << ((secIt->sectionY >> 4) & 63))) {
                destroySectionBuffers(*secIt);
                secIt = renderData.sections.erase(secIt);
            } else {
                ++secIt;
            }
        }
        renderData.sections.reserve(renderData.sections.size() + result.sections.size());

        // 整批上传掩码内重建的 section（同一帧内完成，保证视觉连续性；
        // 移动端 UMA，host-visible 直传免 staging）
        for (auto& secData : result.sections) {
            ChunkSectionRenderData sec;
            sec.sectionY = secData.sectionY;

            std::vector<uint32_t> merged;
            merged.reserve(secData.baseIndices.size() + secData.overlayIndices.size() + secData.waterIndices.size());
            merged.insert(merged.end(), secData.baseIndices.begin(), secData.baseIndices.end());
            merged.insert(merged.end(), secData.overlayIndices.begin(), secData.overlayIndices.end());
            merged.insert(merged.end(), secData.waterIndices.begin(), secData.waterIndices.end());
            if (merged.empty() || secData.packedVertices.empty()) continue;

            sec.overlayIndexCount = static_cast<uint32_t>(secData.overlayIndices.size());
            sec.waterIndexCount = static_cast<uint32_t>(secData.waterIndices.size());
            sec.indexCount = static_cast<uint32_t>(merged.size());

            if (!createHostBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, secData.packedVertices.data(),
                                  secData.packedVertices.size() * sizeof(PackedVertex),
                                  sec.vertexBuffer, sec.vertexMemory) ||
                !createHostBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, merged.data(),
                                  merged.size() * sizeof(uint32_t),
                                  sec.indexBuffer, sec.indexMemory)) {
                LOGE("Failed to create section buffers (sectionY=%d)", sec.sectionY);
                destroySectionBuffers(sec);
                continue;
            }

            renderData.sections.push_back(sec);
        }
    }
}

void VulkanRenderer::updateChunkUniforms(float cameraX, float cameraY, float cameraZ,
                                         float pitch, float yaw, float skyR, float skyG, float skyB) {
    if (!chunkUniformMapped) return;

    glm::mat4 view = Camera::computeViewMatrix(cameraX, cameraY, cameraZ, pitch, yaw);
    float aspect = (float)swapchainExtent.width / (float)swapchainExtent.height;
    glm::mat4 proj = Camera::computeProjectionMatrix(CHUNK_FOV, aspect, CHUNK_NEAR, chunkFar);

    // 视锥从未修正的 proj*view 提取（平面提取公式基于 GL 惯例 clip 空间）
    glm::mat4 viewProj = proj * view;
    computeFrustumPlanes(&viewProj[0][0]);

    // glm::perspective 默认 GL 深度 [-1,1] → Vulkan [0,1]（z' = 0.5z + 0.5w）
    glm::mat4 clipFix(1.0f);
    clipFix[2][2] = 0.5f;
    clipFix[3][2] = 0.5f;
    proj = clipFix * proj;
    proj[1][1] *= -1.0f;  // Vulkan NDC Y 轴向下

    ChunkUBO ubo{};
    memcpy(ubo.modelViewMat, &view[0][0], sizeof(float) * 16);
    memcpy(ubo.projMat, &proj[0][0], sizeof(float) * 16);
    ubo.fogColor[0] = skyR;
    ubo.fogColor[1] = skyG;
    ubo.fogColor[2] = skyB;
    ubo.fogColor[3] = 1.0f;
    ubo.fogParams[0] = chunkFar * 0.7f;  // FogStart
    ubo.fogParams[1] = chunkFar;         // FogEnd
    ubo.colorModulator[0] = 1.0f;
    ubo.colorModulator[1] = 1.0f;
    ubo.colorModulator[2] = 1.0f;
    ubo.colorModulator[3] = 1.0f;

    memcpy(chunkUniformMapped, &ubo, sizeof(ubo));
    vmaFlushAllocation(allocator, chunkUniformMemory, 0, sizeof(ubo));
}

void VulkanRenderer::computeFrustumPlanes(const float* m) {
    // 列主序 mat4：m[col*4+row]；六平面：左/右/底/顶/近/远（与 GL 侧同源）
    for (int i = 0; i < 3; i++) {
        for (int col = 0; col < 4; col++) {
            frustumPlanes[i * 2 + 0][col] = m[col * 4 + 3] + m[col * 4 + i];
            frustumPlanes[i * 2 + 1][col] = m[col * 4 + 3] - m[col * 4 + i];
        }
    }
    for (auto& plane : frustumPlanes) {
        float len = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
        if (len > 0.001f) {
            for (float& v : plane) v /= len;
        }
    }
}

bool VulkanRenderer::isAABBInFrustum(float minX, float minY, float minZ,
                                     float maxX, float maxY, float maxZ) const {
    // p-vertex 测试：每平面只检法线方向投影最大的角点
    for (const auto& pl : frustumPlanes) {
        float px = (pl[0] > 0) ? maxX : minX;
        float py = (pl[1] > 0) ? maxY : minY;
        float pz = (pl[2] > 0) ? maxZ : minZ;
        if (pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0) {
            return false;
        }
    }
    return true;
}

void VulkanRenderer::renderChunks(VkCommandBuffer cmd) {
    // 图集就绪前不绘制（描述符引用的图集 layout 还是 UNDEFINED）
    if (!chunkResourcesReady || !atlasReady || chunkPipelineCutout == VK_NULL_HANDLE) return;

    VkViewport viewport{};
    viewport.width = (float)swapchainExtent.width;
    viewport.height = (float)swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ {0, 0}, swapchainExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, chunkPipelineLayout,
                            0, 1, &chunkDescriptorSet, 0, nullptr);

    VkDeviceSize vbOffset = 0;

    // ---- Phase 1a: 基体几何（cutout，写深度）----
    bool cutoutBound = false;
    for (auto& [chunkKey, renderData] : chunkRenderCache) {
        float minX = renderData.posX;
        float maxX = minX + 16.0f;
        float minZ = renderData.posZ;
        float maxZ = minZ + 16.0f;
        for (auto& sec : renderData.sections) {
            uint32_t baseEnd = sec.indexCount - sec.overlayIndexCount - sec.waterIndexCount;
            if (baseEnd == 0) continue;
            float secMinY = (float)sec.sectionY;
            if (!isAABBInFrustum(minX, secMinY, minZ, maxX, secMinY + 16.0f, maxZ)) continue;

            if (!cutoutBound) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, chunkPipelineCutout);
                cutoutBound = true;
            }
            vkCmdBindVertexBuffers(cmd, 0, 1, &sec.vertexBuffer, &vbOffset);
            vkCmdBindIndexBuffer(cmd, sec.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, baseEnd, 1, 0, 0, 0);
        }
    }

    // ---- Phase 1b: 覆盖层（cutout shader，LEQUAL 不写深度）----
    bool overlayBound = false;
    for (auto& [chunkKey, renderData] : chunkRenderCache) {
        float minX = renderData.posX;
        float maxX = minX + 16.0f;
        float minZ = renderData.posZ;
        float maxZ = minZ + 16.0f;
        for (auto& sec : renderData.sections) {
            if (sec.overlayIndexCount == 0) continue;
            float secMinY = (float)sec.sectionY;
            if (!isAABBInFrustum(minX, secMinY, minZ, maxX, secMinY + 16.0f, maxZ)) continue;

            if (!overlayBound) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, chunkPipelineOverlay);
                overlayBound = true;
            }
            uint32_t baseEnd = sec.indexCount - sec.overlayIndexCount - sec.waterIndexCount;
            vkCmdBindVertexBuffers(cmd, 0, 1, &sec.vertexBuffer, &vbOffset);
            vkCmdBindIndexBuffer(cmd, sec.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, sec.overlayIndexCount, 1, baseEnd, 0, 0);
        }
    }

    // ---- Phase 2: 水（translucent shader，不写深度）----
    bool waterBound = false;
    for (auto& [chunkKey, renderData] : chunkRenderCache) {
        float minX = renderData.posX;
        float maxX = minX + 16.0f;
        float minZ = renderData.posZ;
        float maxZ = minZ + 16.0f;
        for (auto& sec : renderData.sections) {
            if (sec.waterIndexCount == 0) continue;
            float secMinY = (float)sec.sectionY;
            if (!isAABBInFrustum(minX, secMinY, minZ, maxX, secMinY + 16.0f, maxZ)) continue;

            if (!waterBound) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, chunkPipelineWater);
                waterBound = true;
            }
            uint32_t overlayEnd = sec.indexCount - sec.waterIndexCount;
            vkCmdBindVertexBuffers(cmd, 0, 1, &sec.vertexBuffer, &vbOffset);
            vkCmdBindIndexBuffer(cmd, sec.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, sec.waterIndexCount, 1, overlayEnd, 0, 0);
        }
    }
}

void VulkanRenderer::render(float cameraX, float cameraY, float cameraZ,
                            float pitch, float yaw) {
    static int frameCount = 0;
    frameCount++;

    // Surface 已失效（切屏）：跳过渲染防止崩溃
    if (!surfaceValid) return;

    if (frameCount <= 5 || frameCount % 60 == 0) {
        LOGI("=== Rendering frame %d ===", frameCount);
        LOGI("Camera: pos=(%.1f, %.1f, %.1f), pitch=%.1f, yaw=%.1f",
             cameraX, cameraY, cameraZ, pitch * 180.0f/3.14159f, yaw * 180.0f/3.14159f);
    }

    // 检查关键资源是否有效
    if (device == VK_NULL_HANDLE || swapchain == VK_NULL_HANDLE ||
        commandPool == VK_NULL_HANDLE || inFlightFence == VK_NULL_HANDLE) {
        LOGE("Vulkan resources not initialized, skipping render");
        return;
    }

    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                           imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        LOGI("Swapchain out of date during acquire");
        recreateSwapchain(screenWidth, screenHeight);
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOGE("Failed to acquire swap chain image! Result: %d", result);
        return;
    }

    if (frameCount <= 5 || frameCount % 60 == 0) {
        LOGI("Acquired image index: %u", imageIndex);
    }

    // 检查 imageIndex 是否有效
    if (imageIndex >= commandBuffers.size() || imageIndex >= swapchainFramebuffers.size()) {
        LOGE("Invalid image index: %u, commandBuffers size: %zu, framebuffers size: %zu",
             imageIndex, commandBuffers.size(), swapchainFramebuffers.size());
        return;
    }

    vkResetFences(device, 1, &inFlightFence);
    vkResetCommandBuffer(commandBuffers[imageIndex], 0);

    // 断连清屏请求（上帧 fence 已过，GPU 不再引用旧 buffer，删除安全；
    // 必须在 menuMode 判断之前处理：断连后 UI 状态已切回主菜单）
    if (pendingChunkClear.exchange(false)) {
        if (auto* scheduler = ClientEngine::getInstance() ? ClientEngine::getInstance()->getMeshScheduler() : nullptr) {
            scheduler->clearAll();
        }
        for (auto& [chunkKey, renderData] : chunkRenderCache) {
            for (auto& sec : renderData.sections) destroySectionBuffers(sec);
        }
        chunkRenderCache.clear();
        LOGI("Chunk render data cleared (Vulkan)");
    }

    // 主界面模式：非游戏状态下绘制全景背景
    bool menuMode = imguiInitialized &&
                    GameUI::getInstance().getState() != UIState::IN_GAME;
    if (menuMode && !panoramaInitAttempted) {
        // 全景资源懒初始化（一次性上传命令必须在帧命令录制之前提交）
        panoramaInitAttempted = true;
        panoramaReady = initPanorama();
    }
    // ImGui 绘制数据构建（主界面菜单与游戏内 HUD 共用，命令录制前；
    // 内部懒加载的 GUI/物品纹理一次性上传命令也必须在此提交）
    if (imguiInitialized) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)swapchainExtent.width, (float)swapchainExtent.height);
        // 真实帧间隔（菜单动画/双击判定/挖掘冷却依赖）
        static auto lastFrameTime = std::chrono::high_resolution_clock::now();
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;
        io.DeltaTime = dt > 0.0001f ? dt : 0.0001f;

        GameUI::getInstance().render();  // Vulkan 模式下仅构建 draw data，不提交
    }

    // 游戏内：区块资源懒初始化 + 图集/lightmap/网格上传 + 调度 + UBO 更新
    //（内部的一次性上传命令必须在帧命令录制之前提交）
    float skyR = 0.53f, skyG = 0.81f, skyB = 0.92f;
    bool inGame = GameUI::getInstance().getState() == UIState::IN_GAME;
    if (inGame) {
        if (!chunkInitAttempted) {
            chunkInitAttempted = true;
            chunkResourcesReady = initChunkResources();
            if (!chunkResourcesReady) {
                LOGE("Chunk resources init failed, chunk rendering disabled");
            }
        }
        if (chunkResourcesReady) {
            processChunkAtlas();
            updateChunkLightmap();
            processChunkCompletedWork();
            if (auto* scheduler = ClientEngine::getInstance() ? ClientEngine::getInstance()->getMeshScheduler() : nullptr) {
                scheduler->update(cameraX, cameraY, cameraZ, chunkFar);
            }
            auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
            if (game && game->getLight()) {
                skyR = game->getLight()->getSkyColorR();
                skyG = game->getLight()->getSkyColorG();
                skyB = game->getLight()->getSkyColorB();
            }
            updateChunkUniforms(cameraX, cameraY, cameraZ, pitch, yaw, skyR, skyG, skyB);
        }
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffers[imageIndex], &beginInfo) != VK_SUCCESS) {
        LOGE("Failed to begin recording command buffer!");
        return;
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchainExtent;

    VkClearValue clearValues[2];
    clearValues[0].color.float32[0] = skyR;  // 天空色（游戏内随昼夜循环）
    clearValues[0].color.float32[1] = skyG;
    clearValues[0].color.float32[2] = skyB;
    clearValues[0].color.float32[3] = 1.0f;

    clearValues[1].depthStencil.depth = 1.0f;     // 清除深度为最大值
    clearValues[1].depthStencil.stencil = 0;

    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    if (frameCount <= 5 || frameCount % 60 == 0) {
        LOGI("Beginning render pass with sky color: R=%.2f G=%.2f B=%.2f",
             clearValues[0].color.float32[0], clearValues[0].color.float32[1],
             clearValues[0].color.float32[2]);
    }

    vkCmdBeginRenderPass(commandBuffers[imageIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (menuMode) {
        // 主界面：旋转全景背景（失败时回退清屏色）
        if (panoramaReady) {
            renderPanorama(commandBuffers[imageIndex]);
        }
    } else {
        // 游戏内：区块三段绘制（图集就绪前只出天空色清屏）
        renderChunks(commandBuffers[imageIndex]);
    }
    // ImGui 最后叠加（主界面菜单 / 游戏内 HUD）
    if (imguiInitialized) {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffers[imageIndex]);
    }

    vkCmdEndRenderPass(commandBuffers[imageIndex]);
    vkEndCommandBuffer(commandBuffers[imageIndex]);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        LOGI("Swapchain out of date after present, recreating");
        recreateSwapchain(screenWidth, screenHeight);
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        // SUBOPTIMAL(1000001003) 静默接受：成功码，图像已呈现。
        // 本项目 preTransform 故意声明 IDENTITY 让合成器负责旋转（见 createSwapchain），
        // 与 currentTransform（如 ROTATE_90）不符是设计内常态，重建无法消除，
        // 若因此重建会每帧 vkDeviceWaitIdle+全量重建导致帧率暴跌
        LOGE("Failed to present image: %d", result);
    }

    // 帧率限制（统一使用绝对时间，与 GLRenderer 帧末 limitFramerate 对位）
    limitFramerate();
}

void VulkanRenderer::setMaxFps(int fps) {
    maxFps = fps;
    // FIFO present 自带 vsync：0（垂直同步）与 256（无限制）都交给 vsync 节拍，
    // 仅 1-255 时由 CPU 限帧（低于刷新率时生效，高于时被 vsync 封顶）
    if (fps > 0 && fps < 256) {
        frameIntervalNs = NANOSECONDS_PER_SECOND / fps;
    } else {
        frameIntervalNs = 0;
    }
    // 重置基准，下次限帧时重新以当前时间开始
    frameTimeBaseValid = false;
    LOGI("MaxFps set to %d, interval=%lld ns (Vulkan)", fps, frameIntervalNs);
}

void VulkanRenderer::limitFramerate() {
    if (frameIntervalNs <= 0) {
        frameTimeBaseValid = false;
        return;
    }

    // 首次启用或刚调整 FPS，以当前时刻为起点
    if (!frameTimeBaseValid) {
        clock_gettime(CLOCK_MONOTONIC, &frameTimeBase);
        frameTimeBaseValid = true;
    }

    // 计算下一帧的绝对唤醒时间
    frameTimeBase.tv_nsec += frameIntervalNs;
    while (frameTimeBase.tv_nsec >= NANOSECONDS_PER_SECOND) {
        frameTimeBase.tv_nsec -= NANOSECONDS_PER_SECOND;
        frameTimeBase.tv_sec += 1;
    }

    // 绝对时间睡眠。如果目标时间已过（渲染超时），立即返回
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &frameTimeBase, nullptr);
}

std::vector<char> VulkanRenderer::readFile(const std::string& filename) {
    if (!g_assetManager) {
        LOGE("Asset manager not set!");
        return {};
    }

    AAsset* asset = AAssetManager_open(g_assetManager, filename.c_str(), AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Failed to open asset: %s", filename.c_str());
        return {};
    }

    off_t length = AAsset_getLength(asset);
    const void* buffer = AAsset_getBuffer(asset);

    std::vector<char> data((const char*)buffer, (const char*)buffer + length);
    AAsset_close(asset);

    LOGI("Loaded file: %s (%d bytes)", filename.c_str(), (int)data.size());
    return data;
}

VkShaderModule VulkanRenderer::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}

// VMA 分配器：接管全部 vkAllocateMemory/findMemoryType 样板，大块预分配避免碰 maxMemoryAllocationCount 上限
bool VulkanRenderer::createAllocator() {
    VmaAllocatorCreateInfo info{};
    info.vulkanApiVersion = VK_API_VERSION_1_0;
    info.physicalDevice = physicalDevice;
    info.device = device;
    info.instance = instance;
    if (vmaCreateAllocator(&info, &allocator) != VK_SUCCESS) {
        LOGE("Failed to create VMA allocator");
        return false;
    }
    LOGI("VMA allocator created");
    return true;
}

bool VulkanRenderer::checkValidationLayerSupport() {
    return true;
}

bool VulkanRenderer::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "MinecraftClient";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    const char* extensions[] = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };
    createInfo.enabledExtensionCount = 2;
    createInfo.ppEnabledExtensionNames = extensions;

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        LOGE("Failed to create Vulkan instance");
        return false;
    }

    LOGI("Vulkan instance created");
    return true;
}

bool VulkanRenderer::createSurface(ANativeWindow* window) {
    VkAndroidSurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    createInfo.window = window;

    if (vkCreateAndroidSurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS) {
        LOGE("Failed to create Android surface");
        return false;
    }

    LOGI("Android surface created");
    return true;
}

bool VulkanRenderer::selectPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        LOGE("No Vulkan devices found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    physicalDevice = devices[0];

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    LOGI("Selected GPU: %s", properties.deviceName);

    return true;
}

bool VulkanRenderer::createLogicalDevice() {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    int graphicsFamily = -1;
    int presentFamily = -1;

    for (int i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
        if (presentSupport) {
            presentFamily = i;
        }

        if (graphicsFamily >= 0 && presentFamily >= 0) break;
    }

    if (graphicsFamily < 0 || presentFamily < 0) {
        LOGE("Could not find required queue families");
        return false;
    }

    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfos[2] = {};
    queueCreateInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfos[0].queueFamilyIndex = graphicsFamily;
    queueCreateInfos[0].queueCount = 1;
    queueCreateInfos[0].pQueuePriorities = &queuePriority;

    queueCreateInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfos[1].queueFamilyIndex = presentFamily;
    queueCreateInfos[1].queueCount = 1;
    queueCreateInfos[1].pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 2;
    createInfo.pQueueCreateInfos = queueCreateInfos;

    const char* deviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = deviceExtensions;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        LOGE("Failed to create logical device");
        return false;
    }

    vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
    graphicsQueueFamily = (uint32_t)graphicsFamily;  // 供命令池/ImGui 后端使用

    LOGI("Logical device created");
    return true;
}

bool VulkanRenderer::createSwapchain(int width, int height) {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    // 查询支持的格式
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> availableFormats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, availableFormats.data());

    // 选择最合适的格式
    VkSurfaceFormatKHR surfaceFormat;
    bool foundPreferred = false;

    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = availableFormat;
            foundPreferred = true;
            break;
        }
    }

    if (!foundPreferred && !availableFormats.empty()) {
        surfaceFormat = availableFormats[0];
        LOGI("Using fallback format: %d, colorSpace: %d",
             surfaceFormat.format, surfaceFormat.colorSpace);
    }

    // 查询支持的呈现模式
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> availablePresentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, availablePresentModes.data());

    // 选择 FIFO（垂直同步）
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    bool fifoSupported = false;
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_FIFO_KHR) {
            fifoSupported = true;
            break;
        }
    }

    if (!fifoSupported && !availablePresentModes.empty()) {
        presentMode = availablePresentModes[0];
        LOGI("FIFO not supported, using mode: %d", presentMode);
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = width;
        extent.height = height;
    }

    // Android 预旋转：声明 IDENTITY 让合成器负责旋转（与 GL 路径行为一致）。
    // 若直接传 currentTransform（如 ROTATE_90）则相当于声明内容已自行预旋转，
    // 而我们按横屏坐标直接绘制，会导致画面以竖屏姿态显示。
    VkSurfaceTransformFlagBitsKHR preTransform;
    if (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    } else {
        preTransform = capabilities.currentTransform;
    }
    LOGI("Swapchain preTransform: %d (currentTransform: %d)",
         preTransform, capabilities.currentTransform);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    swapchainMinImageCount = imageCount;  // 供 ImGui 后端使用（>= 2）

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = preTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS) {
        LOGE("Failed to create swapchain");
        return false;
    }

    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

    swapchainImageFormat = surfaceFormat.format;
    swapchainExtent = extent;

    LOGI("Swapchain created: %dx%d, format: %d, colorSpace: %d, presentMode: %d",
         extent.width, extent.height, surfaceFormat.format,
         surfaceFormat.colorSpace, presentMode);
    return true;
}

bool VulkanRenderer::createImageViews() {
    swapchainImageViews.resize(swapchainImages.size());

    for (size_t i = 0; i < swapchainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) {
            LOGE("Failed to create image view");
            return false;
        }
    }

    LOGI("Created %zu image views", swapchainImageViews.size());
    return true;
}

bool VulkanRenderer::createRenderPass() {
    // 颜色附件
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // 深度附件
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;  // 32位深度
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // 子_pass_依赖（确保深度缓冲正确转换）
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        LOGE("Failed to create render pass");
        return false;
    }

    LOGI("Render pass created with depth attachment");
    return true;
}

bool VulkanRenderer::createFramebuffers() {
    swapchainFramebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        VkImageView attachments[] = {swapchainImageViews[i], depthImageView};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
            LOGE("Failed to create framebuffer");
            return false;
        }
    }

    LOGI("Created %zu framebuffers with depth attachment", swapchainFramebuffers.size());
    return true;
}

bool VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    uint32_t queueFamilyIndex = graphicsQueueFamily;
    poolInfo.queueFamilyIndex = queueFamilyIndex;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        LOGE("Failed to create command pool");
        return false;
    }

    LOGI("Command pool created");
    return true;
}

bool VulkanRenderer::createCommandBuffers() {
    commandBuffers.resize(swapchainFramebuffers.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        LOGE("Failed to allocate command buffers");
        return false;
    }

    LOGI("Command buffers created: %zu", commandBuffers.size());
    return true;
}

bool VulkanRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) !=
        VK_SUCCESS ||
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) !=
        VK_SUCCESS ||
        vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
        LOGE("Failed to create synchronization objects");
        return false;
    }

    LOGI("Sync objects created successfully");
    return true;
}