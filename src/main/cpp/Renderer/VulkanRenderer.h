#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <map>
#include <atomic>
#include <time.h>
#include <android/native_window.h>
#include <android/asset_manager.h>
#include "CommonTypes.h"
#include "PanoramaView.h"
#include "ChunkOcclusionCuller.h"

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
    void recreateSwapchain(int width, int height);

    // ImGui Vulkan 后端接入（主界面渲染，第一步）
    bool initImGui();

    // GUI 纹理：加载 gui/<path>.png 并注册给 ImGui，返回 VkDescriptorSet 作 ImTextureID
    // （对应 GL 侧 ResourcepackManager::getGuiTexture 的角色，按路径缓存，失败返回 VK_NULL_HANDLE）
    // outWidth/outHeight 可选返回纹理像素尺寸（供标题图等按原图比例排版的场景）
    VkDescriptorSet getGuiTexture(const std::string& path, int* outWidth = nullptr, int* outHeight = nullptr);

    // 任意 assets 路径纹理（含扩展名，如 "misc/unknown_server.png"），缓存键 = 完整路径
    VkDescriptorSet getAssetTexture(const std::string& assetPath, int* outWidth = nullptr, int* outHeight = nullptr);

    // 内存 PNG 字节解码上传（如服务器 favicon），按 cacheKey 缓存
    VkDescriptorSet getMemoryTexture(const std::string& cacheKey, const uint8_t* pngData, size_t pngSize);

    // 物品图标（hotbar/物品栏）：优先 BlockIconRasterizer CPU 光栅化 3D 方块图标，
    // 回退 item/、block/ 2D 贴图（对应 GL 侧 ResourcepackManager::getItemTexture）
    VkDescriptorSet getItemTexture(const std::string& itemName);

    // 切屏后 Surface 释放/重建（防止向失效 Surface 提交）
    void invalidateSurface() { surfaceValid = false; }
    bool recreateSurface(ANativeWindow* window, int width, int height);

    // 断连时清空全部区块渲染数据（任意线程可调，实际清理在渲染线程执行）
    void clearChunks() { pendingChunkClear.store(true); }

    // 渲染距离（视频设置，单位区块；换算与 GLRenderer 一致：farPlane = chunks * 16）
    void setRenderDistance(int chunks) { chunkFar = chunks * 16.0f; }
    int getRenderDistance() const { return static_cast<int>(chunkFar / 16.0f); }

    // 最大帧率控制（三段式语义与 GLRenderer 一致：0=垂直同步, 1-255=fps值, 256=无限制）
    // swapchain 固定 FIFO 自带 vsync：0/256 两档交给 vsync 节拍，1-255 由 CPU 绝对时间限帧
    void setMaxFps(int fps);
    int getMaxFps() const { return maxFps; }

private:
    // ===== 区块渲染（消费图形 API 无关的 ChunkMeshScheduler 产出）=====
    // 与 GLRenderer 的 SectionRenderData 对位，但网格不独占 buffer：
    // 顶点/索引存于共享网格池（meshPool）块内区间，绘制用 firstIndex/vertexOffset 寻址，
    // 索引顺序 base | overlay | water 三段合并
    struct ChunkSectionRenderData {
        int sectionY = 0;
        int poolBlock = -1;               // 所属网格池块下标
        VkDeviceSize vertexOffset = 0;    // 块内字节偏移（PackedVertex 尺寸天然对齐）
        VkDeviceSize vertexSize = 0;
        VkDeviceSize indexOffset = 0;
        VkDeviceSize indexSize = 0;
        uint32_t indexCount = 0;
        uint32_t overlayIndexCount = 0;
        uint32_t waterIndexCount = 0;
        uint64_t visibilityData = 0;      // section 方向连通性（BFS 遮挡剔除消费）
    };

    // 共享网格池块：大块 VB/IB + first-fit 子分配（空闲表 offset→size，释放邻接合并）。
    // 所有 section 网格合入少量大 buffer，每块一次绑定 + 逐 section 直绘
    // （MDI 合批已回滚：Adreno 610 实测负优化）
    struct MeshPoolBlock {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexMemory = VK_NULL_HANDLE;
        void* vertexMapped = nullptr;     // 持久映射（UMA 直写免 staging）
        VkDeviceSize vertexCapacity = 0;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VmaAllocation indexMemory = VK_NULL_HANDLE;
        void* indexMapped = nullptr;
        VkDeviceSize indexCapacity = 0;
        std::map<VkDeviceSize, VkDeviceSize> vertexFree;  // offset → size
        std::map<VkDeviceSize, VkDeviceSize> indexFree;
    };
    struct ChunkRenderData {
        std::vector<ChunkSectionRenderData> sections;
        float posX = 0.0f;   // 区块世界坐标（chunkX * 16）
        float posZ = 0.0f;
    };

    bool initChunkResources();        // 首个游戏帧懒初始化：UBO/采样器/lightmap/描述符/管线
    void destroyChunkResources();     // cleanup 时销毁全部区块资源（含缓存 buffer）
    bool createChunkPipelines();      // 3 条管线：cutout(写深度)/overlay(不写)/water(translucent)
    void destroyChunkPipelines();     // swapchain 重建路径销毁（renderPass 依赖）
    void processChunkAtlas();         // 分批解码方块纹理，齐全后整体上传 2D array + 写描述符
    void updateChunkLightmap();       // Light 像素变化时重传 16x16 lightmap
    void processChunkCompletedWork(); // pollRemovals + pollResults → 网格池上传
    bool uploadSectionMesh(const void* vtxData, VkDeviceSize vtxSize,
                           const void* idxData, VkDeviceSize idxSize,
                           ChunkSectionRenderData& sec);   // 池内分配 + 持久映射直写
    void freeSectionMesh(ChunkSectionRenderData& sec);     // 归还池内区间（邻接合并）
    bool createMeshPoolBlock(VkDeviceSize vtxSize, VkDeviceSize idxSize);
    void destroyMeshPool();   // 断连/cleanup：缓存已清空后整池销毁
    static bool poolRangeAlloc(std::map<VkDeviceSize, VkDeviceSize>& freeMap,
                               VkDeviceSize size, VkDeviceSize& outOffset);
    static void poolRangeFree(std::map<VkDeviceSize, VkDeviceSize>& freeMap,
                              VkDeviceSize offset, VkDeviceSize size);
    void renderChunks(VkCommandBuffer cmd);  // 三段绘制（命令录制期调用）
    void updateChunkUniforms(float cameraX, float cameraY, float cameraZ,
                             float pitch, float yaw, float skyR, float skyG, float skyB);
    void computeFrustumPlanes(const float* viewProj);  // 列主序 mat4
    bool isAABBInFrustum(float minX, float minY, float minZ,
                         float maxX, float maxY, float maxZ) const;
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
    bool createFramebuffers();
    bool createCommandPool();
    bool createDepthResources();
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

    // 区块渲染常量（与 GLRenderer 对齐）
    static constexpr float CHUNK_FOV = 70.0f;
    static constexpr float CHUNK_NEAR = 0.1f;
    static constexpr int MAX_CHUNKS_PER_FRAME = 2;    // 每帧最多上传的区块数
    static constexpr int TEXTURES_PER_FRAME = 200;    // 图集每帧解码的纹理层数
    // 网格池块容量（顶点:索引 ≈ 4:1——每 quad 4 顶点 128 字节 vs 6 索引 24 字节，留冗余）
    static constexpr VkDeviceSize MESH_POOL_VERTEX_BLOCK_SIZE = 16 * 1024 * 1024;
    static constexpr VkDeviceSize MESH_POOL_INDEX_BLOCK_SIZE = 4 * 1024 * 1024;

    // 远平面/渲染距离（视频设置驱动，setRenderDistance 修改，对应 GLRenderer::farPlane）
    float chunkFar = 500.0f;

    // 帧率限制（对应 GLRenderer 的绝对时间限帧：TIMER_ABSTIME，零漂移，present 后调用）
    int maxFps = 0;
    long long frameIntervalNs = 0;
    struct timespec frameTimeBase = {0, 0};
    bool frameTimeBaseValid = false;
    static constexpr long long NANOSECONDS_PER_SECOND = 1000000000LL;
    void limitFramerate();

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

    // Framebuffers
    std::vector<VkFramebuffer> swapchainFramebuffers;

    // Command buffers
    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    // Sync objects
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;

    int screenWidth = 0;
    int screenHeight = 0;

    // ImGui 状态
    bool imguiInitialized = false;
    bool surfaceValid = true;

    // ===== 区块渲染资源 =====
    bool chunkInitAttempted = false;   // 懒初始化只尝试一次（失败不反复重试）
    bool chunkResourcesReady = false;  // UBO/描述符/管线就绪
    // 管线（依赖 renderPass，swapchain 重建时随之重建）
    VkDescriptorSetLayout chunkSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout chunkPipelineLayout = VK_NULL_HANDLE;
    VkPipeline chunkPipelineCutout = VK_NULL_HANDLE;    // 基体：写深度
    VkPipeline chunkPipelineOverlay = VK_NULL_HANDLE;   // 覆盖层：cutout shader，不写深度
    VkPipeline chunkPipelineWater = VK_NULL_HANDLE;     // 水：translucent shader，不写深度
    // 描述符（binding0=UBO, binding1=图集, binding2=lightmap）
    VkDescriptorPool chunkDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet chunkDescriptorSet = VK_NULL_HANDLE;
    // UBO（持久映射）
    VkBuffer chunkUniformBuffer = VK_NULL_HANDLE;
    VmaAllocation chunkUniformMemory = VK_NULL_HANDLE;
    void* chunkUniformMapped = nullptr;
    // 方块纹理数组（2D array，分批解码收集后一次上传）
    VkImage chunkAtlasImage = VK_NULL_HANDLE;
    VmaAllocation chunkAtlasMemory = VK_NULL_HANDLE;
    VkImageView chunkAtlasView = VK_NULL_HANDLE;
    VkSampler chunkAtlasSampler = VK_NULL_HANDLE;       // NEAREST + REPEAT
    bool atlasReady = false;
    int atlasLayerCount = 0;
    int atlasWidth = 16;
    int atlasHeight = 16;
    int atlasNextLayer = 0;
    std::vector<uint8_t> atlasStagingPixels;            // 分批解码累积区，上传后释放
    // 光照贴图 16x16（LINEAR + CLAMP）
    VkImage lightmapImage = VK_NULL_HANDLE;
    VmaAllocation lightmapMemory = VK_NULL_HANDLE;
    VkImageView lightmapView = VK_NULL_HANDLE;
    VkSampler lightmapSampler = VK_NULL_HANDLE;
    // renderChunks 每帧暂存（按池块归组；成员持有以保留 capacity；
    // 复用 VkDrawIndexedIndirectCommand 仅作普通绘制参数容器，不走 indirect 提交）
    std::vector<std::vector<VkDrawIndexedIndirectCommand>> drawsCutout, drawsOverlay, drawsWater;
    // 共享网格池（host 可见持久映射；单帧 in-flight，帧首 fence 后重写安全）
    std::vector<MeshPoolBlock> meshPool;
    // 区块渲染缓存（仅渲染线程访问）与视锥平面
    std::unordered_map<uint64_t, ChunkRenderData> chunkRenderCache;
    float frustumPlanes[6][4] = {};
    std::atomic<bool> pendingChunkClear{false};  // 断连清屏请求（跨线程）
    // Section 级 BFS 遮挡剔除（与 GL 后端共用同一实现）
    ChunkOcclusionCuller occlusionCuller;
    bool occlusionDirty = true;  // 渲染缓存增删后置位，触发下帧重算

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
