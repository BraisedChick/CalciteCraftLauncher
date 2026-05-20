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

    static void setAssetManager(AAssetManager* assets);

    bool initialize(ANativeWindow* window);
    void cleanup();
    void render(float cameraX, float cameraY, float cameraZ, float pitch, float yaw);
    void recreateSurface(int width, int height);

    void updateCamera(float cameraX, float cameraY, float cameraZ, float pitch, float yaw);
    void makeCurrent();
    void releaseCurrent();

    // 方块操作
    void addBlock(int x, int y, int z);
    void removeBlock(int x, int y, int z);

    // 设置 ChunkManager 引用
    void setChunkManager(ChunkManager* manager);

    // 从区块数据重建网格
    void rebuildMeshFromChunks();
    
    // 标记指定区块需要更新
    void markChunkForUpdate(int chunkX, int chunkZ);

private:
    bool createEGLContext(ANativeWindow* window);
    bool createShaders();
    bool createBuffers();
    std::string loadShaderFile(const std::string& filename);
    GLuint compileShader(GLenum type, const std::string& source);
    GLuint createProgram(const std::string& vertSource, const std::string& fragSource);

    void rebuildMesh();
    void addBlockToMesh(std::vector<Vertex>& vertices,
                       std::vector<uint32_t>& indices,
                       float x, float y, float z);

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;

    GLuint shaderProgram = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLuint textureArrayID = 0;  // 纹理数组（替代单个 textureID）

    GLint uniformModel = -1;
    GLint uniformView = -1;
    GLint uniformProj = -1;
    GLint uniformTexture = -1;

    float cameraMatrix[16];
    float projectionMatrix[16];
    float modelMatrix[16];
    
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    
    int screenWidth = 0;
    int screenHeight = 0;

    static AAssetManager* g_assetManager;

    // 区块管理
    ChunkManager* chunkManager = nullptr;
    bool needRebuildMesh = false;  // 标记是否需要重建网格

    // 手动放置的方块
    std::map<BlockPosition, bool> blocks;
    
    // 帧计数器
    uint32_t frameCount = 0;
    
    // ===== 区块合批渲染优化 =====
    struct ChunkRenderData {
        GLuint vbo = 0;          // 顶点缓冲
        GLuint ebo = 0;          // 索引缓冲
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        glm::vec3 position;      // 区块世界坐标
        bool visible = true;     // 是否在视锥体内
        bool needsUpdate = false; // 是否需要重建
    };
    
    // 缓存每个区块的渲染数据 (key: chunkX << 16 | chunkZ)
    std::unordered_map<uint64_t, ChunkRenderData> chunkRenderCache;
    
    // 视锥体参数
    float fov = 70.0f;
    float nearPlane = 0.1f;
    float farPlane = 500.0f;  // 渲染距离
};
