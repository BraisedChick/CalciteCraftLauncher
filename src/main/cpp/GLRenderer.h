#pragma once

#include <vulkan/vulkan.h>
#include <android/asset_manager.h>
#include <android/native_window.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
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
#include "GameUI.h"
// 添加 GLM 库
#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "CommonTypes.h"
#include "ChunkManager.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GLRenderer", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GLRenderer", __VA_ARGS__)

#include "ResourcepackManager.h"

struct BlockPosition {
    int x, y, z;

    bool operator<(const BlockPosition& other) const {
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        return z < other.z;
    }

    bool operator==(const BlockPosition& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

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

    // 方块操作
    void addBlock(int x, int y, int z);
    void removeBlock(int x, int y, int z);

    // 设置 ChunkManager 引用
    void setChunkManager(ChunkManager* manager);

    // FOV 控制
    void setFov(float degrees);
    float getFov() const { return fov; }

    // 渲染距离控制
    void setRenderDistance(int chunks);
    int getRenderDistance() const { return static_cast<int>(farPlane / 16.0f); }

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

    // 预渲染所有物品对应的方块图标
    void preRenderBlockIcons();

    // 获取已缓存的方块图标纹理
    const GLuint* getBlockIcon(const std::string& name) const {
        auto it = blockIconCache.find(name);
        return it != blockIconCache.end() ? &it->second : nullptr;
    }

private:
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
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t overlayIndexCount = 0;  // 草覆盖层索引数
        uint32_t waterIndexCount = 0;    // 水索引数
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

    // 手动放置的方块
    std::map<BlockPosition, bool> blocks;
    
    // 帧计数器
    uint32_t frameCount = 0;
    
    // ===== 区块合批渲染优化 =====
    struct ChunkRenderData {
        GLuint vao = 0;          // 顶点数组对象（捕获 attrib 配置 + VBO/EBO 绑定）
        GLuint vbo = 0;          // 顶点缓冲
        GLuint ebo = 0;          // 索引缓冲
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t overlayIndexCount = 0;  // 草覆盖层索引数（需 LEQUAL 写深度）
        uint32_t waterIndexCount = 0;    // 水索引数（需 alpha blend，不写深度）
        glm::vec3 position;      // 区块世界坐标
        bool visible = true;     // 是否在视锥体内
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
};
