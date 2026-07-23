#pragma once

#include <vulkan/vulkan.h>
#include <android/asset_manager.h>
#include <android/native_window.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

// NDK r28 的 GLES3 头文件缺失 GL_HALF_FLOAT 定义
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif

#include <string>
#include <vector>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include "gui/GameUI.h"
// 添加 GLM 库
#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/packing.hpp>

#include "CommonTypes.h"
#include "ChunkManager.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GLRenderer", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GLRenderer", __VA_ARGS__)

#include "ResourcepackManager.h"

class GLRenderer {
public:
    GLRenderer();
    ~GLRenderer();

    bool initialize(ANativeWindow* window);
    void cleanup();
    void render(float cameraX, float cameraY, float cameraZ, float pitch, float yaw);
    void recreateSurface(int width, int height);

    void updateCamera(float cameraX, float cameraY, float cameraZ, float pitch, float yaw);
    void makeCurrent();
    void releaseCurrent();

    // ImGui 菜单
    bool initImGui();
    void renderUI();

    // 设置 ChunkManager 引用
    void setChunkManager(ChunkManager* manager);

    // FOV 控制
    void setFov(float degrees);
    float getFov() const { return fov; }

    // 渲染距离控制
    void setRenderDistance(int chunks);
    int getRenderDistance() const { return static_cast<int>(farPlane / 16.0f); }

    // Mipmap 等级（0=关闭，1-4=mipmap层级数）
    void setMipmapLevel(int level);

    // 最大帧率控制（0=垂直同步, 1-255=fps值, 256=无限制）
    void setMaxFps(int fps);
    int getMaxFps() const { return maxFps; }

    // 纹理数组初始化是否已完成（渲染线程延迟初始化）
    bool isTextureInitComplete() const { return !textureInitPending; }

    // 从区块数据重建网格，返回 true 表示还有更多区块需要处理
    bool rebuildMeshFromChunks();

    // 标记指定区块需要更新
    void markChunkForUpdate(int chunkX, int chunkZ);

    // 在渲染线程上完成纹理数组初始化（避免 ANR）
    bool finishTextureInit();

    // 断开连接时清理所有区块 VAO/VBO/EBO（线程安全，渲染线程实际执行）
    void clearChunks();
    void doClearChunks();

    // 将方块模型渲染到 GL 纹理（用于物品栏3D图标）
    // modelName: 模型名（如 "oak_stairs"），iconSize: 输出纹理尺寸
    // 返回 GL 纹理 ID，0=失败
    GLuint renderBlockIcon(const std::string& modelName, int iconSize = 32);

    // 初始化全景背景（cubemap 纹理 + 着色器 + VAO + FBO）
    void initPanorama();

    // 渲染全景到 FBO（在 ImGui::NewFrame 之前调用）
    void renderPanoramaToFBO();

    // 获取全景 FBO 纹理 ID（供 ImGui 绘制背景用）
    GLuint getPanoramaTexture() const { return panoramaFBOTexture; }

    // 预渲染所有物品对应的方块图标
    void preRenderBlockIcons();

    // 获取已缓存的方块图标纹理
    const GLuint* getBlockIcon(const std::string& name) const {
        auto it = blockIconCache.find(name);
        return it != blockIconCache.end() ? &it->second : nullptr;
    }

private:
    // ===== 压缩顶点格式（GPU 上传用，48→32 bytes）=====
    struct PackedVertex {
        float    pos[3];      //  0: 12 bytes (GL_FLOAT, 世界坐标，不压缩)
        float    texIndex;    // 12:  4 bytes (GL_FLOAT)
        uint8_t  color[4];    // 16:  4 bytes (GL_UNSIGNED_BYTE, 归一化)
        uint16_t uv[2];       // 20:  4 bytes (GL_UNSIGNED_SHORT, 归一化 [0,1]→[0,65535])
        uint16_t uv2[2];      // 24:  4 bytes (GL_UNSIGNED_SHORT, 归一化 [0,1]→[0,65535])
        int8_t   normal[4];   // 28:  4 bytes (GL_BYTE, 归一化 [-1,1]→[-127,127], w未用)
    }; // Total: 32 bytes

    // ===== 工作线程（离线网格生成）=====
    struct ChunkWorkItem {
        uint64_t chunkKey;
        int chunkX;
        int chunkZ;
        float distance;  // 距离玩家距离，优先级队列排序用

        bool operator<(const ChunkWorkItem& other) const {
            return distance > other.distance;  // 小顶堆：距离近的优先级高
        }
    };

    struct ChunkMeshResult {
        uint64_t chunkKey;
        // 改为每个 section 独立数据，不合并
        struct SectionData {
            int sectionY;
            std::vector<PackedVertex> packedVertices;  // 在工作线程压缩好，渲染线程直接上传
            std::vector<uint32_t> baseIndices;
            std::vector<uint32_t> overlayIndices;
            std::vector<uint32_t> waterIndices;
            uint64_t visibilityData = 0;  // 该 section 的方向连通性数据
        };
        std::vector<SectionData> sections;
    };

    void workerLoop();
    void startWorker();
    void stopWorker();
    void enqueueWork(ChunkWorkItem item);
    void processCompletedWork();

    // 工作线程池（多个线程并行生成网格）
    static constexpr int WORKER_THREAD_COUNT = 4;
    std::vector<std::thread> workerThreads;
    std::mutex workMutex;
    std::condition_variable workCV;
    std::priority_queue<ChunkWorkItem> workQueue;
    bool workerRunning = false;

    // 完成结果队列（worker→render 线程）
    std::mutex resultMutex;
    std::queue<ChunkMeshResult> resultQueue;
    // 待处理队列：每帧处理少量 chunk（但每个 chunk 整批上传），避免一帧内创建大量 GL 资源导致卡顿
    std::queue<ChunkMeshResult> pendingResults;
    static constexpr int MAX_CHUNKS_PER_FRAME = 2;

    // 避免重复入队同一区块
    std::unordered_set<uint64_t> pendingChunks;
    std::mutex pendingMutex;
    bool createEGLContext(ANativeWindow* window);
    bool createShaders();
    bool createBuffers();
    // 从视图和投影矩阵计算视锥体平面
    void computeFrustumPlanes(const glm::mat4& viewProj);

    // 检查 AABB 是否在视锥体内
    bool isAABBInFrustum(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) const;

    // 视锥体平面（6 个，格式为 Ax+By+Cz+D=0）
    glm::vec4 frustumPlanes[6];
    void rebuildMesh();
    void addBlockToMesh(std::vector<Vertex>& vertices,
                       std::vector<uint32_t>& indices,
                       float x, float y, float z);

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;

    GLuint textureArrayID = 0;  // 纹理数组（替代单个 textureID）
    GLuint lightmapTextureID = 0;  // 光照贴图纹理（Sampler2，默认白色=全亮）

    // ===== Mojang 官方着色器程序 =====
    ShaderProgramInfo shaderSolid;         // rendertype_solid（不透明）
    ShaderProgramInfo shaderCutout;        // rendertype_cutout（镂空，alpha<0.1 丢弃）
    ShaderProgramInfo shaderCutoutMipped;  // rendertype_cutout_mipped（带 mipmap 的镂空）
    ShaderProgramInfo shaderTranslucent;   // rendertype_translucent（半透明）

    float cameraMatrix[16];
    float projectionMatrix[16];
    float modelMatrix[16];
    
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    
    int screenWidth = 0;
    int screenHeight = 0;

    // 区块管理（原子指针，跨线程安全）
    std::atomic<ChunkManager*> chunkManager{nullptr};
    std::atomic<bool> needRebuildMesh{false};  // 标记是否需要重建网格
    std::atomic<bool> pendingClear{false};     // 标记是否需要清除所有区块（断连时）

    // 脏区块集合（只记录需要更新网格的区块，避免每次遍历所有区块）
    std::unordered_set<uint64_t> dirtyChunks;  // 由 cacheMutex 保护
    size_t lastChunkCount = 0;                  // 上次已知区块数，用于发现新区块

    // 帧计数器
    uint32_t frameCount = 0;

    // 帧率限制（0=垂直同步, 1-255=fps值, 256=无限制）
    int maxFps = 0;
    int currentSwapInterval = 1;  // 当前 eglSwapInterval 值

    // 绝对时间限帧（TIMER_ABSTIME，零漂移）
    long long frameIntervalNs = 0;
    struct timespec frameTimeBase = {0, 0};
    bool frameTimeBaseValid = false;
    static constexpr long long NANOSECONDS_PER_SECOND = 1000000000LL;

    void limitFramerate();

    // ===== 区块合批渲染优化 =====
    struct SectionRenderData {
        int sectionY = 0;
        GLuint vao = 0, vbo = 0, ebo = 0;
        uint32_t indexCount = 0;
        uint32_t overlayIndexCount = 0;
        uint32_t waterIndexCount = 0;
        uint64_t visibilityData = 0;  // 6×6 方向连通性 bitmask（Sodium 风格遮挡剔除）
        bool isVisible = true;        // 每帧 BFS 计算结果
    };

    struct ChunkRenderData {
        std::vector<SectionRenderData> sections;
        glm::vec3 position;      // 区块世界坐标
        bool visible = true;
        bool needsUpdate = false; // 是否需要重建
        bool pending = false;    // 是否已在工作队列中
    };
    
    // 缓存每个区块的渲染数据 (key: chunkX << 16 | chunkZ)
    std::unordered_map<uint64_t, ChunkRenderData> chunkRenderCache;
    std::mutex cacheMutex;  // 保护 chunkRenderCache 的线程安全
    
    // 视锥体参数
    float fov = 70.0f;
    float nearPlane = 0.1f;
    float farPlane = 500.0f;  // 渲染距离

    // 上一帧的相机位置（用于区块加载距离计算）
    float lastCameraX = 0.0f, lastCameraY = 0.0f, lastCameraZ = 0.0f;

    // 纹理数组延迟初始化标志（在渲染线程上分批完成，避免 ANR）
    bool textureInitPending = true;
    int textureTotalCount = 0;
    int textureNextBatch = 0;
    int textureWidth = 16;
    int textureHeight = 16;
    static constexpr int TEXTURES_PER_FRAME = 200;  // 每帧最多上传的纹理数

    // 方块图标缓存（物品栏3D模型预渲染）
    std::unordered_map<std::string, GLuint> blockIconCache;

    // 全景背景（主菜单旋转 cubemap → FBO → ImGui 背景图）
    GLuint panoramaCubemap = 0;
    GLuint panoramaProgram = 0;
    GLuint panoramaVAO = 0;
    GLuint panoramaVBO = 0;
    GLuint panoramaEBO = 0;
    GLuint panoramaFBO = 0;        // FBO 用于将 cubemap 渲染到 2D 纹理
    GLuint panoramaFBOTexture = 0;  // FBO 颜色附件纹理
    int panoramaFBOWidth = 0;
    int panoramaFBOHeight = 0;
    bool panoramaLoaded = false;

    // 破坏覆盖层（destroy overlay）
    GLuint crackVAO = 0;
    GLuint crackVBO = 0;
    GLuint crackEBO = 0;
    int crackLastBlockX = -9999999;
    int crackLastBlockY = -9999999;
    int crackLastBlockZ = -9999999;
    int crackLastStage = -1;

    void renderCrackOverlay(const glm::mat4& viewMatrix, const glm::mat4& projMatrix, const ShaderProgramInfo& shader);
};
