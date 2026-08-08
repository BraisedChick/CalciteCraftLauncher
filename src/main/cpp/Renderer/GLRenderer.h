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
#include "ChunkMeshScheduler.h"
#include "ChunkOcclusionCuller.h"
#include "CrackOverlayMesh.h"
#include "PanoramaView.h"
#include "SkyRenderer.h"

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
    void recreateSurface(int width, int height);  // 窗口尺寸变化（仅更新 viewport）

    // Surface 生命周期分离（切屏不闪退）
    // 注意：EGL context 绑定线程唯一，实际的 EGL 操作必须在持有 context 的渲染线程执行。
    // JNI/UI 线程只能通过 request* 接口发起请求，由渲染线程在 processSurfaceRequests() 中执行。
    void requestSurfaceRelease();                   // JNI线程：请求释放 Surface（阻塞直到渲染线程完成）
    void requestSurfaceRecreate(ANativeWindow* window); // JNI线程：请求重建 Surface（转移 window 所有权）
    void releaseSurface();                          // 渲染线程：释放 Surface，保留 Context
    bool recreateSurface(ANativeWindow* window);    // 渲染线程：用新 Window 重建 Surface
    bool hasValidSurface() const { return surface != EGL_NO_SURFACE; }
    EGLConfig getEGLConfig() const { return eglConfig; }
    EGLDisplay getEGLDisplay() const { return display; }
    EGLContext getEGLContext() const { return context; }

    void updateCamera(float cameraX, float cameraY, float cameraZ, float pitch, float yaw);
    void makeCurrent();

    // ImGui 菜单
    bool initImGui();
    void renderUI();

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
    // 网格生成/调度已迁至 ChunkMeshScheduler（公共组件），这里只消费其产出：
    // pollResults → GL 上传，pollRemovals → GL 删除
    void processCompletedWork();
    void processChunkRemovals();  // 渲染线程：删除已卸载区块的 GL 资源

    // 每帧最多上传的 chunk 数（每个 chunk 整批上传），避免一帧内创建大量 GL 资源导致卡顿
    static constexpr int MAX_CHUNKS_PER_FRAME = 2;

    bool createEGLContext(ANativeWindow* window);
    bool createShaders();
    bool createBuffers();
    // 从视图和投影矩阵计算视锥体平面
    void computeFrustumPlanes(const glm::mat4& viewProj);

    // 检查 AABB 是否在视锥体内
    bool isAABBInFrustum(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) const;

    // 视锥体平面（6 个，格式为 Ax+By+Cz+D=0）
    glm::vec4 frustumPlanes[6];

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLConfig eglConfig = nullptr;  // 保存，用于 Surface 重建

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

    std::atomic<bool> pendingClear{false};     // 标记是否需要清除所有区块（断连时）

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

    // ===== Surface 生命周期跨线程请求（JNI线程发起，渲染线程执行）=====
    void processSurfaceRequests();  // 渲染线程调用：执行挂起的释放/重建请求
    std::mutex surfaceReqMutex;
    std::condition_variable surfaceReqCV;
    bool surfaceReleaseReq = false;              // 待处理的释放请求
    ANativeWindow* surfaceRecreateReqWindow = nullptr;  // 待处理的重建请求（持有一个 ref）
    bool surfaceReqHandled = false;              // 渲染线程处理完成标志

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
    };
    
    // 缓存每个区块的渲染数据 (key: chunkX << 16 | chunkZ)
    std::unordered_map<uint64_t, ChunkRenderData> chunkRenderCache;
    std::mutex cacheMutex;  // 保护 chunkRenderCache 的线程安全

    // BFS 遮挡剔除（消费 section 连通性 visibilityData，与视锥剔除叠加）
    ChunkOcclusionCuller occlusionCuller;
    bool occlusionDirty = true;  // 渲染缓存增删后置位，触发重算
    
    // 视锥体参数
    float fov = 70.0f;
    float nearPlane = 0.1f;
    float farPlane = 500.0f;  // 渲染距离

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
    // 面像素加载/几何/动画矩阵在 PanoramaView（纯逻辑），这里只负责 GL 资源与绘制
    PanoramaView panoramaView;
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
    // 几何生成在 CrackOverlayMesh（纯逻辑，图形 API 无关），这里只负责 GL 上传与绘制
    CrackOverlayMesh crackMesh;
    GLuint crackVAO = 0;
    GLuint crackVBO = 0;
    GLuint crackEBO = 0;

    void renderCrackOverlay(const glm::mat4& viewMatrix, const glm::mat4& projMatrix, const ShaderProgramInfo& shader);

    // ===== 天空渲染 =====
    void initSky();
    void renderSky(const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                   float skyR, float skyG, float skyB,
                   float timeOfDay, float starBrightness, int moonPhase,
                   float normalizedTime);
    GLuint loadSunTexture();
    GLuint loadMoonTexture(int phase);
    // 天空着色器（纯色：天空圆盘 + 星星）
    GLuint skyColorProgram = 0;

    // 天体着色器（纹理：太阳 + 月亮）
    GLuint skyCelestialProgram = 0;

    // 天空 VAO/EBO（TRIANGLE_FAN）
    GLuint skyTopVAO = 0;
    GLuint skyTopVBO = 0;
    GLuint skyBottomVAO = 0;
    GLuint skyBottomVBO = 0;

    // 太阳/月亮 VAO/EBO
    GLuint skySunVAO = 0;
    GLuint skySunVBO = 0;
    GLuint skySunEBO = 0;
    GLuint skyMoonVAO = 0;
    GLuint skyMoonVBO = 0;
    GLuint skyMoonEBO = 0;

    // 星星 VAO/EBO（与太阳/月亮共用 skyColorProgram）
    GLuint skyStarsVAO = 0;
    GLuint skyStarsVBO = 0;
    GLuint skyStarsEBO = 0;

    // ===== 太阳和月亮纹理缓存（只加载一次） =====
    GLuint sunTextureID = 0;
    GLuint moonTextureIDs[8] = {0};
    int m_starCount = 0;
    bool skyInitialized = false;
};
