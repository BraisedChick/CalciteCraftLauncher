#include "GLRenderer.h"
#include <android/log.h>
#include <cmath>
#include <android/asset_manager.h>
#include <cstring>
#include <cstddef>  // for offsetof
#include "TextureLoader.h"
#include "MeshGenerator.h"
#include "TextureAtlas.h"
#include "BlockRegistry.h"
#include "BiomeColorManager.h"
#include "MinecraftVersion.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"

#define LOG_TAG "GLRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

AAssetManager* GLRenderer::g_assetManager = nullptr;

void GLRenderer::setAssetManager(AAssetManager* assetManager) {
    g_assetManager = assetManager;
}

void GLRenderer::setChunkManager(ChunkManager* manager) {
    chunkManager.store(manager);

    // 标记需要重建网格，在渲染线程中执行
    // 注意：不要遍历旧缓存设置 needsUpdate = true，
    // 新区块在 rebuildMeshFromChunks 中创建时已经自动标记了 needsUpdate
    if (manager && display != EGL_NO_DISPLAY) {
        needRebuildMesh.store(true);
    }
}

GLRenderer::GLRenderer()
        : display(EGL_NO_DISPLAY), context(EGL_NO_CONTEXT), surface(EGL_NO_SURFACE),
          shaderProgram(0), vao(0), vbo(0), ebo(0), textureArrayID(0),
          uniformModel(-1), uniformView(-1), uniformProj(-1), uniformTexture(-1),
          vertexCount(0), indexCount(0), screenWidth(0), screenHeight(0) {
    memset(cameraMatrix, 0, sizeof(cameraMatrix));
    memset(projectionMatrix, 0, sizeof(projectionMatrix));
    startWorker();
}

GLRenderer::~GLRenderer() {
    cleanup();
}

bool GLRenderer::initialize(ANativeWindow* window) {
    LOGI("=== GLRenderer::initialize START ===");
    
    screenWidth = ANativeWindow_getWidth(window);
    screenHeight = ANativeWindow_getHeight(window);

    LOGI("Window size: %dx%d", screenWidth, screenHeight);

    if (!createEGLContext(window)) {
        LOGE("Failed to create EGL context");
        return false;
    }

    if (!eglMakeCurrent(display, surface, surface, context)) {
        LOGE("Failed to make current: 0x%x", eglGetError());
        return false;
    }

    glViewport(0, 0, screenWidth, screenHeight);

    if (!createShaders()) {
        return false;
    }

    if (!createBuffers()) {
        return false;
    }

    // 初始化 ImGui
    if (!initImGui()) {
        LOGE("Failed to initialize ImGui");
        // 不返回 false，ImGui 失败不该阻止游戏渲染
    } else {
        // 设置 ImGui 显示尺寸
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)screenWidth, (float)screenHeight);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // 启用背面剔除，减少渲染的面数
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);  // 逆时针为正面

    // 同步加载 TextureAtlas（仅模型解析，快速 ~1 秒，在 UI 线程执行）
    LOGI("Initializing TextureAtlas...");
    TextureAtlas::getInstance().initialize();
    LOGI("TextureAtlas initialized: %d layers", TextureAtlas::getInstance().getLayerCount());

    // 预计算全部方块元数据（之后 getBlockMetadata 无锁访问）
    BlockRegistry::getInstance().precomputeAll();

    // 初始化 BiomeColorManager
    LOGI("Initializing BiomeColorManager...");
    BiomeColorManager::getInstance().initialize();

    // GL 纹理数组创建和上传延迟到渲染线程（finishTextureInit），避免 ANR

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    LOGI("=== GLRenderer::initialize SUCCESS ===");
    return true;
}

bool GLRenderer::finishTextureInit() {
    if (!textureInitPending) return true;

    if (textureArrayID == 0) {
        // 第一帧：创建纹理数组
        textureTotalCount = TextureAtlas::getInstance().getLayerCount();
        if (textureTotalCount <= 0) {
            LOGE("No texture layers to initialize");
            textureInitPending = false;
            return false;
        }

        // 获取第一张纹理的尺寸
        {
            TextureData firstTex = TextureLoader::loadImage(
                TextureAtlas::getInstance().getTextureFileName(0));
            if (firstTex.data) {
                textureWidth = firstTex.width;
                textureHeight = firstTex.height;
            } else {
                LOGW("No texture files found, using 16x16 placeholder textures");
            }
        }

        LOGI("Creating texture array: %d layers, %dx%d",
             textureTotalCount, textureWidth, textureHeight);

        glGenTextures(1, &textureArrayID);
        glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);

        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA,
                     textureWidth, textureHeight, textureTotalCount,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        textureNextBatch = 0;
    }

    // 逐帧分批上传纹理
    int end = std::min(textureNextBatch + TEXTURES_PER_FRAME, textureTotalCount);
    for (int i = textureNextBatch; i < end; i++) {
        std::string filename = TextureAtlas::getInstance().getTextureFileName(i);
        TextureData texData = TextureLoader::loadImage(filename);
        if (texData.data) {
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                           0, 0, i,
                           textureWidth, textureHeight, 1,
                           GL_RGBA, GL_UNSIGNED_BYTE,
                           texData.data);
        } else {
            LOGW("Texture not found: %s, using placeholder for layer %d", filename.c_str(), i);
            uint8_t r, g, b;
            TextureAtlas::getInstance().getPlaceholderColor(i, r, g, b);
            std::vector<uint8_t> placeholder(textureWidth * textureHeight * 4);
            for (int p = 0; p < textureWidth * textureHeight; p++) {
                placeholder[p * 4 + 0] = r;
                placeholder[p * 4 + 1] = g;
                placeholder[p * 4 + 2] = b;
                placeholder[p * 4 + 3] = 255;
            }
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                           0, 0, i,
                           textureWidth, textureHeight, 1,
                           GL_RGBA, GL_UNSIGNED_BYTE,
                           placeholder.data());
        }
    }
    textureNextBatch = end;

    if (textureNextBatch >= textureTotalCount) {
        // 全部纹理上传完成，加载水纹理
        LOGI("All %d textures uploaded, loading water texture...", textureTotalCount);

        glGenTextures(1, &waterTextureID);
        glBindTexture(GL_TEXTURE_2D, waterTextureID);
        {
            TextureData waterTex = TextureLoader::loadImage("water_still.png");
            if (waterTex.data) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                             waterTex.width, waterTex.height, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, waterTex.data);
                LOGI("Loaded water_still.png: %dx%d", waterTex.width, waterTex.height);
            } else {
                LOGW("water_still.png not found, using placeholder");
                std::vector<uint8_t> placeholder(16 * 512 * 4, 0);
                for (int y = 0; y < 512; y++) {
                    for (int x = 0; x < 16; x++) {
                        int p = (y * 16 + x) * 4;
                        placeholder[p + 0] = 0x3F;
                        placeholder[p + 1] = 0x76;
                        placeholder[p + 2] = 0xE4;
                        placeholder[p + 3] = 255;
                    }
                }
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                             16, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, placeholder.data());
            }
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        textureInitPending = false;
        LOGI("=== finishTextureInit COMPLETE ===");
    }

    return true;
}

bool GLRenderer::createEGLContext(ANativeWindow* window) {
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        LOGE("Failed to get EGL display");
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        LOGE("Failed to initialize EGL");
        return false;
    }

    LOGI("EGL initialized: %d.%d", major, minor);

    EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs)) {
        LOGE("Failed to choose EGL config");
        return false;
    }

    EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
    };

    context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        LOGE("Failed to create EGL context");
        return false;
    }

    surface = eglCreateWindowSurface(display, config, window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        LOGE("Failed to create EGL surface");
        return false;
    }

    LOGI("EGL context and surface created");
    return true;
}

std::string GLRenderer::loadShaderFile(const std::string& filename) {
    if (!g_assetManager) {
        LOGE("Asset manager not set!");
        return "";
    }

    AAsset* asset = AAssetManager_open(g_assetManager, filename.c_str(), AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Failed to open shader file: %s", filename.c_str());
        return "";
    }

    off_t length = AAsset_getLength(asset);
    const char* buffer = static_cast<const char*>(AAsset_getBuffer(asset));

    std::string content(buffer, length);
    AAsset_close(asset);

    LOGI("Loaded shader file: %s (%d bytes)", filename.c_str(), (int)content.size());
    return content;
}

GLuint GLRenderer::compileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        LOGE("Shader compilation failed: %s", infoLog);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint GLRenderer::createProgram(const std::string& vertSource, const std::string& fragSource) {
    GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertSource);
    if (vertShader == 0) return 0;

    GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragSource);
    if (fragShader == 0) {
        glDeleteShader(vertShader);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        LOGE("Program linking failed: %s", infoLog);
        glDeleteProgram(program);
        glDeleteShader(vertShader);
        glDeleteShader(fragShader);
        return 0;
    }

    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    return program;
}

bool GLRenderer::createShaders() {
    std::string vertSource = loadShaderFile("shaders/gl_shader.vert");
    std::string fragSource = loadShaderFile("shaders/gl_shader.frag");

    if (vertSource.empty() || fragSource.empty()) {
        LOGE("Failed to load shader files");
        return false;
    }

    shaderProgram = createProgram(vertSource, fragSource);
    if (shaderProgram == 0) {
        return false;
    }

    uniformModel = glGetUniformLocation(shaderProgram, "model");
    uniformView = glGetUniformLocation(shaderProgram, "view");
    uniformProj = glGetUniformLocation(shaderProgram, "proj");
    uniformTexture = glGetUniformLocation(shaderProgram, "textureSampler");
    uniformWaterTexture = glGetUniformLocation(shaderProgram, "waterTexture");
    uniformWaterTime = glGetUniformLocation(shaderProgram, "waterTime");
    uniformUseWaterTexture = glGetUniformLocation(shaderProgram, "useWaterTexture");

    LOGI("Uniform locations: model=%d, view=%d, proj=%d, texture=%d, waterTex=%d, waterTime=%d, useWater=%d",
         uniformModel, uniformView, uniformProj, uniformTexture,
         uniformWaterTexture, uniformWaterTime, uniformUseWaterTexture);

    LOGI("Shaders compiled and linked successfully");
    return true;
}

bool GLRenderer::createBuffers() {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    // 位置属性 (location = 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);

    // 纹理坐标属性 (location = 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    // 纹理索引属性 (location = 2)
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // 顶点颜色属性 (location = 3) — 4 bytes RGBA, normalized unsigned byte
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)(offsetof(Vertex, color)));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);

    LOGI("Buffers created with 4 vertex attributes (pos, texCoord, texIndex, color)");
    return true;
}

bool GLRenderer::rebuildMeshFromChunks() {
    auto* mgr = chunkManager.load();
    if (!mgr) {
        LOGW("ChunkManager not set!");
        return false;
    }

    int chunksEnqueued = 0;

    // 摄像机位置（使用帧开始时保存的实际坐标，而非从视图矩阵提取）
    glm::vec3 cameraPos(lastCameraX, lastCameraY, lastCameraZ);

    // ===== 第一步：处理脏区块（由 markChunkForUpdate 标记的）=====
    std::unordered_set<uint64_t> localDirty;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        localDirty.swap(dirtyChunks);
    }

    for (uint64_t chunkKey : localDirty) {
        int chunkX = (int)(chunkKey >> 32);
        int chunkZ = (int)(chunkKey & 0xFFFFFFFF);

        auto chunk = mgr->getChunk(chunkX, chunkZ);
        if (!chunk || !chunk->isLoaded) continue;

        // 距离计算
        float chunkCenterX = chunk->pos.x * 16.0f + 8.0f;
        float chunkCenterZ = chunk->pos.z * 16.0f + 8.0f;
        float distX = chunkCenterX - cameraPos.x;
        float distZ = chunkCenterZ - cameraPos.z;
        float distance = sqrt(distX * distX + distZ * distZ);

        ChunkRenderData* renderData = nullptr;

        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto [it, inserted] = chunkRenderCache.try_emplace(chunkKey);
            if (inserted) {
                if (distance > farPlane) {
                    chunkRenderCache.erase(it);
                    continue;
                }
                glGenBuffers(1, &it->second.vbo);
                glGenBuffers(1, &it->second.ebo);
                it->second.position = glm::vec3(chunk->pos.x * 16.0f, 0.0f, chunk->pos.z * 16.0f);
                it->second.needsUpdate = true;
            }
            renderData = &it->second;
        }

        // 距离剔除
        if (distance > farPlane) {
            renderData->visible = false;
            continue;
        }
        renderData->visible = true;

        if (renderData->needsUpdate && !renderData->pending) {
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                if (pendingChunks.find(chunkKey) != pendingChunks.end()) continue;
                pendingChunks.insert(chunkKey);
                renderData->pending = true;
            }
            enqueueWork({chunkKey, chunk->pos.x, chunk->pos.z, distance});
            chunksEnqueued++;
        }
    }

    // ===== 第二步：发现新区块（仅在区块数量变化时扫描）=====
    size_t currentCount = mgr->getLoadedChunkCount();
    if (currentCount != lastChunkCount) {
        lastChunkCount = currentCount;
        auto allChunks = mgr->getAllChunks();

        for (const auto& chunk : allChunks) {
            if (!chunk || !chunk->isLoaded) continue;

            uint64_t chunkKey = ((uint64_t)(chunk->pos.x & 0xFFFFFFFF) << 32) | (chunk->pos.z & 0xFFFFFFFF);

            bool exists;
            {
                std::lock_guard<std::mutex> lock(cacheMutex);
                exists = chunkRenderCache.find(chunkKey) != chunkRenderCache.end();
            }
            if (exists) continue;

            // 距离计算
            float chunkCenterX = chunk->pos.x * 16.0f + 8.0f;
            float chunkCenterZ = chunk->pos.z * 16.0f + 8.0f;
            float distX = chunkCenterX - cameraPos.x;
            float distZ = chunkCenterZ - cameraPos.z;
            float distance = sqrt(distX * distX + distZ * distZ);
            if (distance > farPlane) continue;

            ChunkRenderData* renderData = nullptr;
            {
                std::lock_guard<std::mutex> lock(cacheMutex);
                auto [it, inserted] = chunkRenderCache.try_emplace(chunkKey);
                if (inserted) {
                    glGenBuffers(1, &it->second.vbo);
                    glGenBuffers(1, &it->second.ebo);
                    it->second.position = glm::vec3(chunk->pos.x * 16.0f, 0.0f, chunk->pos.z * 16.0f);
                    it->second.needsUpdate = true;
                }
                renderData = &it->second;
            }

            renderData->visible = true;

            // 新区块立即入队网格生成
            if (renderData->needsUpdate && !renderData->pending) {
                {
                    std::lock_guard<std::mutex> lock(pendingMutex);
                    if (pendingChunks.find(chunkKey) != pendingChunks.end()) continue;
                    pendingChunks.insert(chunkKey);
                    renderData->pending = true;
                }
                enqueueWork({chunkKey, chunk->pos.x, chunk->pos.z, distance});
                chunksEnqueued++;
            }
        }
    }

    if (chunksEnqueued > 0) {
        LOGI("rebuildMeshFromChunks: enqueued %d dirty chunks", chunksEnqueued);
    }

    return false;  // 脏区块方式不需要反复调用
}

void GLRenderer::markChunkForUpdate(int chunkX, int chunkZ) {
    uint64_t chunkKey = ((uint64_t)(chunkX & 0xFFFFFFFF) << 32) | (chunkZ & 0xFFFFFFFF);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = chunkRenderCache.find(chunkKey);
        if (it != chunkRenderCache.end()) {
            it->second.needsUpdate = true;
        }
        dirtyChunks.insert(chunkKey);
    }
    needRebuildMesh.store(true);
}

// ===== 工作线程：离线网格生成，不阻塞渲染线程 =====

void GLRenderer::startWorker() {
    workerRunning = true;
    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        workerThreads.emplace_back(&GLRenderer::workerLoop, this);
    }
    LOGI("Started %d mesh worker threads", WORKER_THREAD_COUNT);
}

void GLRenderer::stopWorker() {
    {
        std::lock_guard<std::mutex> lock(workMutex);
        workerRunning = false;
    }
    workCV.notify_all();
    for (auto& t : workerThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    workerThreads.clear();
}

void GLRenderer::clearChunks() {
    pendingClear.store(true);
}

void GLRenderer::doClearChunks() {
    LOGI("doClearChunks: Clearing all chunk render data...");

    // 1. 停止所有工作线程
    stopWorker();

    // 2. 清空所有队列
    {
        std::lock_guard<std::mutex> lock(workMutex);
        while (!workQueue.empty()) workQueue.pop();
    }
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        while (!resultQueue.empty()) resultQueue.pop();
    }
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingChunks.clear();
    }

    // 3. 删除所有区块的 VAO/VBO/EBO（渲染线程，GL context 已 current）
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (auto& [chunkKey, renderData] : chunkRenderCache) {
            if (renderData.vao != 0) glDeleteVertexArrays(1, &renderData.vao);
            if (renderData.vbo != 0) glDeleteBuffers(1, &renderData.vbo);
            if (renderData.ebo != 0) glDeleteBuffers(1, &renderData.ebo);
        }
        chunkRenderCache.clear();
        dirtyChunks.clear();
        lastChunkCount = 0;
    }

    needRebuildMesh.store(false);

    // 4. 重新启动工作线程
    startWorker();

    LOGI("doClearChunks: All chunk render data cleared");
}

void GLRenderer::enqueueWork(ChunkWorkItem item) {
    {
        std::lock_guard<std::mutex> lock(workMutex);
        workQueue.push(std::move(item));
    }
    workCV.notify_one();
}

void GLRenderer::workerLoop() {
    LOGI("Mesh worker thread started");

    // 线程局部的 scratch vectors，跨 section/chunk 复用避免反复分配
    std::vector<Vertex> wl_baseVertices, wl_overlayVertices, wl_waterVertices;
    std::vector<uint32_t> wl_baseIndices, wl_overlayIndices, wl_waterIndices;

    while (workerRunning) {
        ChunkWorkItem item;
        {
            std::unique_lock<std::mutex> lock(workMutex);
            workCV.wait(lock, [this]() {
                return !workQueue.empty() || !workerRunning;
            });

            if (!workerRunning) break;

            item = workQueue.top();
            workQueue.pop();
        }

        // 在工作线程中生成网格（CPU 密集，不涉及 OpenGL）
        // shared_ptr 确保区块在工作期间不会被网络线程卸载
        auto* mgr = chunkManager.load();
        auto chunk = mgr ? mgr->getChunk(item.chunkX, item.chunkZ) : nullptr;
        if (!chunk || !chunk->isLoaded) {
            // 区块不存在，从 pending 中移除
            std::lock_guard<std::mutex> lock(pendingMutex);
            pendingChunks.erase(item.chunkKey);
            continue;
        }

        std::vector<Vertex> vertices;
        std::vector<uint32_t> baseIndices, overlayIndices, waterIndices;
        uint32_t totalOverlayIndexCount = 0;
        uint32_t totalWaterIndexCount = 0;
        auto block_start = std::chrono::steady_clock::now();
        for (size_t sectionIdx = 0; sectionIdx < chunk->sections.size(); ++sectionIdx) {
            const auto& section = chunk->sections[sectionIdx];
            if (!section || section->isEmpty) continue;

            auto meshOut = MeshGenerator::generateSectionMesh(
                *section, item.chunkX, section->y, item.chunkZ, chunkManager,
                wl_baseVertices, wl_baseIndices,
                wl_overlayVertices, wl_overlayIndices,
                wl_waterVertices, wl_waterIndices);

            uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());
            size_t regularCount = meshOut.indices.size()
                - meshOut.overlayIndexCount - meshOut.waterIndexCount;

            // 按类别分离索引，确保所有 base → overlay → water 跨 section 连续排列
            for (size_t i = 0; i < regularCount; i++) {
                baseIndices.push_back(vertexOffset + meshOut.indices[i]);
            }
            size_t ovStart = regularCount;
            for (size_t i = 0; i < meshOut.overlayIndexCount; i++) {
                overlayIndices.push_back(vertexOffset + meshOut.indices[ovStart + i]);
            }
            size_t watStart = ovStart + meshOut.overlayIndexCount;
            for (size_t i = 0; i < meshOut.waterIndexCount; i++) {
                waterIndices.push_back(vertexOffset + meshOut.indices[watStart + i]);
            }

            vertices.insert(vertices.end(), meshOut.vertices.begin(), meshOut.vertices.end());
            totalOverlayIndexCount += meshOut.overlayIndexCount;
            totalWaterIndexCount += meshOut.waterIndexCount;
        }

        // 合并为 [base | overlay | water]，匹配渲染循环的索引划分
        std::vector<uint32_t> indices;
        indices.reserve(baseIndices.size() + overlayIndices.size() + waterIndices.size());
        indices.insert(indices.end(), baseIndices.begin(), baseIndices.end());
        indices.insert(indices.end(), overlayIndices.begin(), overlayIndices.end());
        indices.insert(indices.end(), waterIndices.begin(), waterIndices.end());

        // 推入完成队列
        ChunkMeshResult result;
        result.chunkKey = item.chunkKey;
        result.vertices = std::move(vertices);
        result.indices = std::move(indices);
        result.overlayIndexCount = totalOverlayIndexCount;
        result.waterIndexCount = totalWaterIndexCount;

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            resultQueue.push(std::move(result));
        }
        auto block_end = std::chrono::steady_clock::now();
        auto block_ms = std::chrono::duration_cast<std::chrono::milliseconds>(block_end - block_start).count();
        LOGI("Chunk (%d,%d) total mesh generation took %lld ms", item.chunkX, item.chunkZ, block_ms);
    }

    LOGI("Mesh worker thread stopped");
}

void GLRenderer::processCompletedWork() {
    // 取出所有已完成的网格结果，上传到 GPU
    std::queue<ChunkMeshResult> localResults;
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        localResults.swap(resultQueue);
    }

    while (!localResults.empty()) {
        auto& result = localResults.front();

        // 找到对应的缓存条目，上传数据
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = chunkRenderCache.find(result.chunkKey);
            if (it != chunkRenderCache.end()) {
                auto& renderData = it->second;

                glBindBuffer(GL_ARRAY_BUFFER, renderData.vbo);
                glBufferData(GL_ARRAY_BUFFER, result.vertices.size() * sizeof(Vertex),
                            result.vertices.data(), GL_STATIC_DRAW);

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderData.ebo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, result.indices.size() * sizeof(uint32_t),
                            result.indices.data(), GL_STATIC_DRAW);

                // 创建并配置该 chunk 的 VAO（捕获 attrib 格式 + VBO/EBO 绑定）
                if (renderData.vao == 0) glGenVertexArrays(1, &renderData.vao);
                glBindVertexArray(renderData.vao);
                glBindBuffer(GL_ARRAY_BUFFER, renderData.vbo);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderData.ebo);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(5 * sizeof(float)));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)(offsetof(Vertex, color)));
                glEnableVertexAttribArray(3);
                glBindVertexArray(0);

                renderData.vertexCount = static_cast<uint32_t>(result.vertices.size());
                renderData.indexCount = static_cast<uint32_t>(result.indices.size());
                renderData.overlayIndexCount = result.overlayIndexCount;
                renderData.waterIndexCount = result.waterIndexCount;
                renderData.needsUpdate = false;
                renderData.pending = false;
            }
        }

        // 从 pending 集合中移除
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pendingChunks.erase(result.chunkKey);
        }

        localResults.pop();
    }
}

void GLRenderer::addBlockToMesh(std::vector<Vertex>& vertices,
                                std::vector<uint32_t>& indices,
                                float x, float y, float z) {
    uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

    // 前面 (z+)
    vertices.push_back({{x, y, z + 1.0f}, {0.0f, 0.0f}});
    vertices.push_back({{x + 1.0f, y, z + 1.0f}, {1.0f, 0.0f}});
    vertices.push_back({{x + 1.0f, y + 1.0f, z + 1.0f}, {1.0f, 1.0f}});
    vertices.push_back({{x, y + 1.0f, z + 1.0f}, {0.0f, 1.0f}});

    // 后面 (z-)
    vertices.push_back({{x + 1.0f, y, z}, {0.0f, 0.0f}});
    vertices.push_back({{x, y, z}, {1.0f, 0.0f}});
    vertices.push_back({{x, y + 1.0f, z}, {1.0f, 1.0f}});
    vertices.push_back({{x + 1.0f, y + 1.0f, z}, {0.0f, 1.0f}});

    // 上面 (y+)
    vertices.push_back({{x, y + 1.0f, z + 1.0f}, {0.0f, 0.0f}});
    vertices.push_back({{x + 1.0f, y + 1.0f, z + 1.0f}, {1.0f, 0.0f}});
    vertices.push_back({{x + 1.0f, y + 1.0f, z}, {1.0f, 1.0f}});
    vertices.push_back({{x, y + 1.0f, z}, {0.0f, 1.0f}});

    // 下面 (y-)
    vertices.push_back({{x, y, z}, {0.0f, 0.0f}});
    vertices.push_back({{x + 1.0f, y, z}, {1.0f, 0.0f}});
    vertices.push_back({{x + 1.0f, y, z + 1.0f}, {1.0f, 1.0f}});
    vertices.push_back({{x, y, z + 1.0f}, {0.0f, 1.0f}});

    // 右面 (x+)
    vertices.push_back({{x + 1.0f, y, z + 1.0f}, {0.0f, 0.0f}});
    vertices.push_back({{x + 1.0f, y, z}, {1.0f, 0.0f}});
    vertices.push_back({{x + 1.0f, y + 1.0f, z}, {1.0f, 1.0f}});
    vertices.push_back({{x + 1.0f, y + 1.0f, z + 1.0f}, {0.0f, 1.0f}});

    // 左面 (x-)
    vertices.push_back({{x, y, z}, {0.0f, 0.0f}});
    vertices.push_back({{x, y, z + 1.0f}, {1.0f, 0.0f}});
    vertices.push_back({{x, y + 1.0f, z + 1.0f}, {1.0f, 1.0f}});
    vertices.push_back({{x, y + 1.0f, z}, {0.0f, 1.0f}});

    for (int face = 0; face < 6; face++) {
        uint32_t offset = baseIndex + face * 4;
        indices.push_back(offset);
        indices.push_back(offset + 1);
        indices.push_back(offset + 2);
        indices.push_back(offset);
        indices.push_back(offset + 2);
        indices.push_back(offset + 3);
    }
}

void GLRenderer::addBlock(int x, int y, int z) {
    BlockPosition pos = {x, y, z};
    blocks[pos] = true;
    needRebuildMesh = true;
    LOGI("Added block at (%d, %d, %d)", x, y, z);
}

void GLRenderer::removeBlock(int x, int y, int z) {
    BlockPosition pos = {x, y, z};
    auto it = blocks.find(pos);
    if (it != blocks.end()) {
        blocks.erase(it);
        needRebuildMesh = true;
        LOGI("Removed block at (%d, %d, %d)", x, y, z);
    }
}

void GLRenderer::updateCamera(float cx, float cy, float cz, float pitch, float yaw) {
    // ===== 使用 Botcraft + GLM 的 Camera 算法 =====

    // 1. 加眼睛高度偏移（玩家脚部 → 眼睛，原版 1.62 格）
    cy += 1.62f;

    // 2. 限制俯仰角（Botcraft: -89° 到 +89°）
    const float maxPitch = glm::radians(89.0f);
    if (pitch > maxPitch) pitch = maxPitch;
    if (pitch < -maxPitch) pitch = -maxPitch;

    // 2. 计算前方向量（Botcraft Camera.cpp 第 108-110 行）
    glm::vec3 front;
    front.x = -sinf(yaw) * cosf(pitch);
    front.y = -sinf(pitch);
    front.z = cosf(yaw) * cosf(pitch);
    front = glm::normalize(front);

    // 3. 计算右向量和上向量（Botcraft 第 114-115 行）
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
    glm::vec3 up = glm::normalize(glm::cross(right, front));

    // 4. 构建视图矩阵（Botcraft 第 117 行：glm::lookAt(position, position + front, up)）
    glm::vec3 position(cx, cy, cz);
    glm::vec3 target = position + front;
    glm::mat4 viewMatrix = glm::lookAt(position, target, up);

    // 将 GLM 矩阵复制到数组（列主序）
    memcpy(cameraMatrix, glm::value_ptr(viewMatrix), sizeof(float) * 16);

    // 5. 透视投影矩阵（与 Botcraft 相同）
    float aspect = (float)screenWidth / screenHeight;
    float fovRad = glm::radians(fov);  // 使用可调节的 fov 成员
    float nearP = 0.1f;

    glm::mat4 projMatrix = glm::perspective(fovRad, aspect, nearP, farPlane);

    // 将 GLM 矩阵复制到数组（列主序）
    memcpy(projectionMatrix, glm::value_ptr(projMatrix), sizeof(float) * 16);
}

void GLRenderer::makeCurrent() {
    if (display && surface && context) {
        EGLBoolean result = eglMakeCurrent(display, surface, surface, context);
        if (result != EGL_TRUE) {
            LOGE("Failed to make EGL context current: 0x%x", eglGetError());
        } else {
            LOGI("EGL context made current");
        }
    }
}

void GLRenderer::releaseCurrent() {
    if (display) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        LOGI("EGL context released");
    }
}
void GLRenderer::computeFrustumPlanes(const glm::mat4& viewProj) {
    // 提取视锥体的 6 个平面（列主序矩阵）
    // 左平面
    frustumPlanes[0] = glm::vec4(
            viewProj[0][3] + viewProj[0][0],
            viewProj[1][3] + viewProj[1][0],
            viewProj[2][3] + viewProj[2][0],
            viewProj[3][3] + viewProj[3][0]
    );
    // 右平面
    frustumPlanes[1] = glm::vec4(
            viewProj[0][3] - viewProj[0][0],
            viewProj[1][3] - viewProj[1][0],
            viewProj[2][3] - viewProj[2][0],
            viewProj[3][3] - viewProj[3][0]
    );
    // 底平面
    frustumPlanes[2] = glm::vec4(
            viewProj[0][3] + viewProj[0][1],
            viewProj[1][3] + viewProj[1][1],
            viewProj[2][3] + viewProj[2][1],
            viewProj[3][3] + viewProj[3][1]
    );
    // 顶平面
    frustumPlanes[3] = glm::vec4(
            viewProj[0][3] - viewProj[0][1],
            viewProj[1][3] - viewProj[1][1],
            viewProj[2][3] - viewProj[2][1],
            viewProj[3][3] - viewProj[3][1]
    );
    // 近平面
    frustumPlanes[4] = glm::vec4(
            viewProj[0][3] + viewProj[0][2],
            viewProj[1][3] + viewProj[1][2],
            viewProj[2][3] + viewProj[2][2],
            viewProj[3][3] + viewProj[3][2]
    );
    // 远平面
    frustumPlanes[5] = glm::vec4(
            viewProj[0][3] - viewProj[0][2],
            viewProj[1][3] - viewProj[1][2],
            viewProj[2][3] - viewProj[2][2],
            viewProj[3][3] - viewProj[3][2]
    );

    // 归一化所有平面
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(frustumPlanes[i]));
        if (len > 0.001f) {
            frustumPlanes[i] /= len;
        }
    }
}

bool GLRenderer::isAABBInFrustum(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) const {
    // 检查 AABB 的 8 个顶点是否完全在某个平面的外侧
    glm::vec3 corners[8] = {
            {minX, minY, minZ}, {maxX, minY, minZ},
            {minX, maxY, minZ}, {maxX, maxY, minZ},
            {minX, minY, maxZ}, {maxX, minY, maxZ},
            {minX, maxY, maxZ}, {maxX, maxY, maxZ}
    };

    for (int p = 0; p < 6; p++) {
        int outsideCount = 0;
        for (int c = 0; c < 8; c++) {
            float dist = frustumPlanes[p].x * corners[c].x +
                         frustumPlanes[p].y * corners[c].y +
                         frustumPlanes[p].z * corners[c].z +
                         frustumPlanes[p].w;
            if (dist < 0) {
                outsideCount++;
            }
        }
        // 如果所有顶点都在平面外侧，AABB 不可见
        if (outsideCount == 8) {
            return false;
        }
    }
    return true;
}
void GLRenderer::render(float cx, float cy, float cz, float pitch, float yaw) {
    if (!display || !context) {
        LOGE("EGL not initialized");
        return;
    }

    // 保存当前帧的相机位置（供 rebuildMeshFromChunks 等使用）
    lastCameraX = cx;
    lastCameraY = cy;
    lastCameraZ = cz;

    auto& ui = GameUI::getInstance();

    // 菜单状态：只渲染 ImGui，不渲染 3D 场景
    if (ui.getState() != UIState::IN_GAME) {
        glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        renderUI();
        eglSwapBuffers(display, surface);
        return;
    }

    // 检查是否需要清除所有区块（断连时清理 VAO/VBO）
    if (pendingClear.exchange(false)) {
        doClearChunks();
    }

    // 处理工作线程完成的网格结果（每帧优先上传）
    processCompletedWork();

    // 逐帧分批加载纹理数组（每帧 TEXTURES_PER_FRAME 张，不阻塞 UI 线程）
    if (textureInitPending) {
        finishTextureInit();
    }

    // 如果需要重建网格，在渲染线程中入队新任务
    if (needRebuildMesh) {
        needRebuildMesh = rebuildMeshFromChunks();
    }

    glClearColor(0.53f, 0.81f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 帧计数器递增
    frameCount++;

    updateCamera(cx, cy, cz, pitch, yaw);
    glm::mat4 viewMatrix = glm::make_mat4(cameraMatrix);
    glm::mat4 projMatrix = glm::make_mat4(projectionMatrix);
    computeFrustumPlanes(projMatrix * viewMatrix);

    glUseProgram(shaderProgram);

    // ===== 水动画时间（帧率无关）=====
    {
        auto now = std::chrono::steady_clock::now();
        float deltaSec = std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;
        if (deltaSec > 0.0f && deltaSec < 1.0f) {
            waterAnimTime = fmodf(waterAnimTime + deltaSec / 3.2f, 1.0f);
        }
    }

    // ===== 绑定水纹理到 GL_TEXTURE1 =====
    if (waterTextureID != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, waterTextureID);
        if (uniformWaterTexture != -1) glUniform1i(uniformWaterTexture, 1);
        if (uniformWaterTime != -1) glUniform1f(uniformWaterTime, waterAnimTime);
    }

    // 设置矩阵 uniform
    float modelMatrix[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
    };
    glUniformMatrix4fv(uniformModel, 1, GL_FALSE, modelMatrix);
    glUniformMatrix4fv(uniformView, 1, GL_FALSE, cameraMatrix);
    glUniformMatrix4fv(uniformProj, 1, GL_FALSE, projectionMatrix);

    // 绑定纹理数组
    if (textureArrayID != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);
        glUniform1i(uniformTexture, 0);

        // 启用纹理采样
        GLint useTextureLoc = glGetUniformLocation(shaderProgram, "useTexture");
        if (useTextureLoc != -1) {
            glUniform1i(useTextureLoc, 1);
        }
    } else {
        // 没有纹理时使用红色
        GLint useTextureLoc = glGetUniformLocation(shaderProgram, "useTexture");
        if (useTextureLoc != -1) {
            glUniform1i(useTextureLoc, 0);
        }
    }

    // ===== 合批渲染所有可见区块 =====
    int chunksRendered = 0;
    int totalTriangles = 0;

    // 默认使用主纹理数组
    if (uniformUseWaterTexture != -1) glUniform1i(uniformUseWaterTexture, 0);

    // 获取维度 Y 范围用于 chunk AABB
    const auto& dim = VersionManager::getInstance().getDimensionConfig();
    float worldMinY = (float)dim.minY;
    float worldMaxY = (float)dim.maxY;

    // 启用透明混合（玻璃等透明方块需要）
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 加锁保护 chunkRenderCache，防止网络线程的 markChunkForUpdate 并发修改
    std::lock_guard<std::mutex> renderLock(cacheMutex);
    for (auto& [chunkKey, renderData] : chunkRenderCache) {

        if (!renderData.visible || renderData.indexCount == 0) {
            continue;
        }

        // ===== 距离剔除（基于渲染距离设置）=====
        float chunkCenterX = renderData.position.x + 8.0f;
        float chunkCenterZ = renderData.position.z + 8.0f;
        float dx = chunkCenterX - lastCameraX;
        float dz = chunkCenterZ - lastCameraZ;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist > farPlane) {
            continue;
        }

        // ===== 视锥体裁剪：检查区块 AABB 是否在视锥体内 =====
        float minX = renderData.position.x;
        float maxX = minX + 16.0f;
        float minZ = renderData.position.z;
        float maxZ = minZ + 16.0f;
        if (!isAABBInFrustum(minX, worldMinY, minZ, maxX, worldMaxY, maxZ)) {
            // 区块不在视锥体内，跳过渲染
            continue;
        }

        // 绑定该区块的 VAO（captures VBO/EBO + attrib 配置）
        glBindVertexArray(renderData.vao);

        // 索引区域划分：[不透明基体 | 草覆盖层(共面，需 LEQUAL) | 水(透明)]
        uint32_t baseEnd = renderData.indexCount - renderData.overlayIndexCount - renderData.waterIndexCount;
        uint32_t grassEnd = baseEnd + renderData.overlayIndexCount;

        // 第一遍：不透明几何体（写深度，LESS）
        glDrawElements(GL_TRIANGLES, baseEnd, GL_UNSIGNED_INT, 0);

        // 第二遍：草覆盖层（与基础层共面，LEQUAL 覆盖但不写深度，避免 z-fighting）
        if (renderData.overlayIndexCount > 0) {
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            glDrawElements(GL_TRIANGLES, renderData.overlayIndexCount, GL_UNSIGNED_INT,
                           (const GLvoid*)(uintptr_t)(baseEnd * sizeof(uint32_t)));
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
        }

        chunksRendered++;
        totalTriangles += renderData.indexCount / 3;
    }

    // ===== 第二阶段：所有区块的水（在不透明几何体之后统一渲染）=====
    bool waterStateSet = false;
    for (auto& [chunkKey, renderData] : chunkRenderCache) {
        if (!renderData.visible || renderData.indexCount == 0) continue;
        if (renderData.waterIndexCount == 0) continue;

        // 距离剔除
        float cx = renderData.position.x + 8.0f;
        float cz = renderData.position.z + 8.0f;
        float dx = cx - lastCameraX, dz = cz - lastCameraZ;
        if (sqrtf(dx * dx + dz * dz) > farPlane) continue;

        float minX = renderData.position.x;
        float maxX = minX + 16.0f;
        float minZ = renderData.position.z;
        float maxZ = minZ + 16.0f;
        if (!isAABBInFrustum(minX, worldMinY, minZ, maxX, worldMaxY, maxZ)) continue;

        // 遇到第一个可见水区块时才设置渲染状态
        if (!waterStateSet) {
            glEnable(GL_BLEND);
            glBlendColor(1.0f, 1.0f, 1.0f, 0.5f);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            if (uniformUseWaterTexture != -1) glUniform1i(uniformUseWaterTexture, 1);
            waterStateSet = true;
        }

        glBindVertexArray(renderData.vao);

        uint32_t baseEnd = renderData.indexCount - renderData.overlayIndexCount - renderData.waterIndexCount;
        uint32_t grassEnd = baseEnd + renderData.overlayIndexCount;

        glDrawElements(GL_TRIANGLES, renderData.waterIndexCount, GL_UNSIGNED_INT,
                       (const GLvoid*)(uintptr_t)(grassEnd * sizeof(uint32_t)));
    }

    // 有水的区块才设置了状态，需要恢复
    if (waterStateSet) {
        if (uniformUseWaterTexture != -1) glUniform1i(uniformUseWaterTexture, 0);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_BLEND);
    }

    glBindVertexArray(0);

    // 每 60 帧打印一次统计信息
    if (frameCount % 60 == 0) {
        LOGI("Frame %u: Rendered %d chunks, %d triangles",
             frameCount, chunksRendered, totalTriangles);
    }

    // 游戏内 UI 叠加（摇杆 + 升降按钮）
    renderUI();

    eglSwapBuffers(display, surface);
}

bool GLRenderer::initImGui() {
    return GameUI::getInstance().init();
}

void GLRenderer::renderUI() {
    GameUI::getInstance().render();
}

void GLRenderer::setFov(float degrees) {
    fov = degrees;
}

void GLRenderer::setRenderDistance(int chunks) {
    farPlane = chunks * 16.0f;
}

void GLRenderer::recreateSurface(int width, int height) {
    screenWidth = width;
    screenHeight = height;
    glViewport(0, 0, width, height);

    // 更新 ImGui 显示尺寸
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
}

void GLRenderer::cleanup() {
    // 先停止工作线程
    stopWorker();

    // 清空工作队列
    {
        std::lock_guard<std::mutex> lock(workMutex);
        while (!workQueue.empty()) workQueue.pop();
    }
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        while (!resultQueue.empty()) resultQueue.pop();
    }
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingChunks.clear();
    }

    // 重置纹理初始化状态
    textureInitPending = true;

    // 清理纹理数组
    if (textureArrayID != 0) {
        glDeleteTextures(1, &textureArrayID);
        textureArrayID = 0;
    }

    // 清理水纹理
    if (waterTextureID != 0) {
        glDeleteTextures(1, &waterTextureID);
        waterTextureID = 0;
    }
    
    // 清理所有区块的渲染数据
    for (auto& [chunkKey, renderData] : chunkRenderCache) {
        if (renderData.vao != 0) {
            glDeleteVertexArrays(1, &renderData.vao);
        }
        if (renderData.vbo != 0) {
            glDeleteBuffers(1, &renderData.vbo);
        }
        if (renderData.ebo != 0) {
            glDeleteBuffers(1, &renderData.ebo);
        }
    }
    chunkRenderCache.clear();
    
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (ebo != 0) {
        glDeleteBuffers(1, &ebo);
        ebo = 0;
    }
    if (shaderProgram != 0) {
        glDeleteProgram(shaderProgram);
        shaderProgram = 0;
    }

    if (display) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (context) {
            eglDestroyContext(display, context);
            context = EGL_NO_CONTEXT;
        }

        if (surface) {
            eglDestroySurface(display, surface);
            surface = EGL_NO_SURFACE;
        }

        eglTerminate(display);
        display = EGL_NO_DISPLAY;
    }

    // 关闭 ImGui
    GameUI::getInstance().shutdown();

    LOGI("OpenGL ES renderer cleaned up");
}
