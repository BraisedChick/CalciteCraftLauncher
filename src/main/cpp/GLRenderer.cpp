#include "GLRenderer.h"
#include <android/log.h>
#include <cmath>
#include <android/asset_manager.h>
#include <cstring>
#include <cstddef>  // for offsetof
#include "TextureLoader.h"
#include "MeshGenerator.h"
#include "TextureAtlas.h"
#include "BiomeColorManager.h"

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
    // 已有缓存的区块也会标记 needsUpdate，确保所有区块使用最新的网格生成逻辑
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

    // 加载纹理到纹理数组
    LOGI("Loading textures to texture array...");

    int textureCount = TEXTURE_LAYER_COUNT;
    int texWidth = 16;
    int texHeight = 16;

    // 先尝试加载第一个纹理获取尺寸
    {
        TextureData firstTex = TextureLoader::loadImage(getTextureFileName(0));
        if (firstTex.data) {
            texWidth = firstTex.width;
            texHeight = firstTex.height;
        } else {
            LOGW("No texture files found at all, using 16x16 placeholder textures");
        }
    }

    // 创建 2D 纹理数组
    glGenTextures(1, &textureArrayID);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);

    // 分配存储空间
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA,
                 texWidth, texHeight, textureCount,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // 加载每个纹理到对应的层
    for (int i = 0; i < textureCount; i++) {
        std::string filename = getTextureFileName(i);
        TextureData texData = TextureLoader::loadImage(filename);
        if (texData.data) {
            // 纹理文件加载成功，上传到 GPU
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                           0, 0, i,  // x, y, layer
                           texWidth, texHeight, 1,
                           GL_RGBA, GL_UNSIGNED_BYTE,
                           texData.data);
            LOGI("Loaded texture %d: %s (layer %d)", i, filename.c_str(), i);
        } else {
            // 纹理文件不存在，生成纯色占位纹理
            LOGW("Texture not found: %s, using placeholder color for layer %d", filename.c_str(), i);
            uint8_t r, g, b;
            getPlaceholderColor(i, r, g, b);

            std::vector<uint8_t> placeholder(texWidth * texHeight * 4);
            for (int p = 0; p < texWidth * texHeight; p++) {
                placeholder[p * 4 + 0] = r;
                placeholder[p * 4 + 1] = g;
                placeholder[p * 4 + 2] = b;
                placeholder[p * 4 + 3] = 255;
            }

            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                           0, 0, i,
                           texWidth, texHeight, 1,
                           GL_RGBA, GL_UNSIGNED_BYTE,
                           placeholder.data());
        }
    }

    // 初始化 BiomeColorManager（加载 colormap 和 biome JSON）
    LOGI("Initializing BiomeColorManager...");
    if (g_assetManager) {
        BiomeColorManager::getInstance().initialize(g_assetManager);
    } else {
        LOGE("Cannot initialize BiomeColorManager: asset manager not set");
    }

    // 设置纹理参数（必须设置，默认 GL_NEAREST_MIPMAP_LINEAR 在没有 mipmap 时会显示黑色）
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    
    // 启用背面剔除，减少渲染的面数
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);  // 逆时针为正面

    // 强制清空旧缓存，重新构建（确保使用最新的 MeshGenerator 代码）
    LOGI("Clearing chunk render cache...");
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (auto& [chunkKey, renderData] : chunkRenderCache) {
            if (renderData.vbo != 0) {
                glDeleteBuffers(1, &renderData.vbo);
            }
            if (renderData.ebo != 0) {
                glDeleteBuffers(1, &renderData.ebo);
            }
        }
        chunkRenderCache.clear();
    }
    
    // 初始化时创建测试方块
    LOGI("Rebuilding mesh from chunks...");
    rebuildMeshFromChunks();

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    LOGI("=== GLRenderer::initialize SUCCESS ===");
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

    LOGI("Uniform locations: model=%d, view=%d, proj=%d, texture=%d",
         uniformModel, uniformView, uniformProj, uniformTexture);

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

    auto allChunks = mgr->getAllChunks();
    LOGI("rebuildMeshFromChunks: got %zu chunks from ChunkManager", allChunks.size());

    int chunksProcessed = 0;
    int chunksSkipped = 0;
    int chunksEnqueued = 0;
    const int MAX_ENQUEUE_PER_CALL = 32;

    // 摄像机位置（用于视锥体裁剪）
    glm::vec3 cameraPos(cameraMatrix[12], cameraMatrix[13], cameraMatrix[14]);

    for (const auto& chunk : allChunks) {
        if (!chunk || !chunk->isLoaded) {
            continue;
        }

        // 距离计算
        float chunkCenterX = chunk->pos.x * 16.0f + 8.0f;
        float chunkCenterZ = chunk->pos.z * 16.0f + 8.0f;
        float distX = chunkCenterX - cameraPos.x;
        float distZ = chunkCenterZ - cameraPos.z;
        float distance = sqrt(distX * distX + distZ * distZ);

        uint64_t chunkKey = ((uint64_t)(chunk->pos.x & 0xFFFFFFFF) << 32) | (chunk->pos.z & 0xFFFFFFFF);

        ChunkRenderData* renderData = nullptr;

        // 短时锁定 cacheMutex，保护 chunkRenderCache 的线程安全
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = chunkRenderCache.find(chunkKey);

            if (it == chunkRenderCache.end()) {
                if (distance > farPlane) {
                    chunksSkipped++;
                    continue;
                }

                // 创建新的渲染数据（分配 GPU 缓冲区）
                ChunkRenderData newData;
                glGenBuffers(1, &newData.vbo);
                glGenBuffers(1, &newData.ebo);
                newData.position = glm::vec3(chunk->pos.x * 16.0f, 0.0f, chunk->pos.z * 16.0f);
                newData.needsUpdate = true;

                auto [insertIt, inserted] = chunkRenderCache.emplace(chunkKey, std::move(newData));
                renderData = &insertIt->second;
            } else {
                renderData = &it->second;
                // 强制重新生成网格（修复旧 overlay 索引布局等需要重建的场景）
                if (!renderData->pending) {
                    renderData->needsUpdate = true;
                }
            }
        }

        // 距离剔除
        if (distance > farPlane) {
            renderData->visible = false;
            chunksSkipped++;
            continue;
        }
        renderData->visible = true;

        // 需要更新且未在队列中 → 入队工作线程处理
        if (renderData->needsUpdate && !renderData->pending) {
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                if (pendingChunks.find(chunkKey) != pendingChunks.end()) continue;
                pendingChunks.insert(chunkKey);
                // 在 pendingMutex 保护下一起设 pending，与 processCompletedWork 中的清除对应
                renderData->pending = true;
            }
            enqueueWork({chunkKey, chunk->pos.x, chunk->pos.z});
            chunksEnqueued++;

            if (chunksEnqueued >= MAX_ENQUEUE_PER_CALL) break;
        }

        chunksProcessed++;
    }

    if (chunksEnqueued > 0) {
        LOGI("Chunks processed: %d, enqueued: %d, skipped: %d",
             chunksProcessed, chunksEnqueued, chunksSkipped);
    }

    return chunksEnqueued >= MAX_ENQUEUE_PER_CALL;
}

void GLRenderer::markChunkForUpdate(int chunkX, int chunkZ) {
    uint64_t chunkKey = ((uint64_t)(chunkX & 0xFFFFFFFF) << 32) | (chunkZ & 0xFFFFFFFF);
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto it = chunkRenderCache.find(chunkKey);
    if (it != chunkRenderCache.end()) {
        it->second.needsUpdate = true;
    }
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

void GLRenderer::enqueueWork(ChunkWorkItem item) {
    {
        std::lock_guard<std::mutex> lock(workMutex);
        workQueue.push(std::move(item));
    }
    workCV.notify_one();
}

void GLRenderer::workerLoop() {
    LOGI("Mesh worker thread started");

    while (workerRunning) {
        ChunkWorkItem item;
        {
            std::unique_lock<std::mutex> lock(workMutex);
            workCV.wait(lock, [this]() {
                return !workQueue.empty() || !workerRunning;
            });

            if (!workerRunning) break;

            item = workQueue.front();
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
        std::vector<uint32_t> indices;
        uint32_t totalOverlayIndexCount = 0;

        for (size_t sectionIdx = 0; sectionIdx < chunk->sections.size(); ++sectionIdx) {
            const auto& section = chunk->sections[sectionIdx];
            if (!section || section->isEmpty) continue;

            auto meshOut = MeshGenerator::generateSectionMesh(
                *section, item.chunkX, section->y, item.chunkZ, chunkManager);

            uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());
            for (uint32_t idx : meshOut.indices) {
                indices.push_back(vertexOffset + idx);
            }
            vertices.insert(vertices.end(), meshOut.vertices.begin(), meshOut.vertices.end());
            totalOverlayIndexCount += meshOut.overlayIndexCount;
        }

        // 推入完成队列
        ChunkMeshResult result;
        result.chunkKey = item.chunkKey;
        result.vertices = std::move(vertices);
        result.indices = std::move(indices);
        result.overlayIndexCount = totalOverlayIndexCount;

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            resultQueue.push(std::move(result));
        }
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

                renderData.vertexCount = static_cast<uint32_t>(result.vertices.size());
                renderData.indexCount = static_cast<uint32_t>(result.indices.size());
                renderData.overlayIndexCount = result.overlayIndexCount;
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
    
    // 1. 限制俯仰角（Botcraft: -89° 到 +89°）
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
    float fov = glm::radians(70.0f);  // 70度转弧度
    float nearP = 0.1f;
    float farP = 1000.0f;

    glm::mat4 projMatrix = glm::perspective(fov, aspect, nearP, farP);

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

void GLRenderer::render(float cx, float cy, float cz, float pitch, float yaw) {
    if (!display || !context) {
        LOGE("EGL not initialized");
        return;
    }

    // 处理工作线程完成的网格结果（每帧优先上传）
    processCompletedWork();

    // 如果需要重建网格，在渲染线程中入队新任务
    if (needRebuildMesh) {
        needRebuildMesh = rebuildMeshFromChunks();
    }

    glClearColor(0.53f, 0.81f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 帧计数器递增
    frameCount++;

    updateCamera(cx, cy, cz, pitch, yaw);

    glUseProgram(shaderProgram);
    
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

    // 加锁保护 chunkRenderCache，防止网络线程的 markChunkForUpdate 并发修改
    std::lock_guard<std::mutex> renderLock(cacheMutex);
    for (auto& [chunkKey, renderData] : chunkRenderCache) {
        if (!renderData.visible || renderData.indexCount == 0) {
            continue;
        }
        
        // 绑定该区块的 VBO/EBO
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, renderData.vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderData.ebo);
        
        // 重新设置顶点属性（因为 VBO 变了）
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)(offsetof(Vertex, color)));
        glEnableVertexAttribArray(3);
        
        // 第一遍：绘制不透明几何体（含草方块侧面基础层）
        uint32_t baseIndexCount = renderData.indexCount - renderData.overlayIndexCount;
        glDrawElements(GL_TRIANGLES, baseIndexCount, GL_UNSIGNED_INT, 0);

        // 第二遍：绘制 overlay 几何体（草方块侧面染色覆盖层，需 alpha blend）
        if (renderData.overlayIndexCount > 0) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);

            glDrawElements(GL_TRIANGLES, renderData.overlayIndexCount, GL_UNSIGNED_INT,
                          (const GLvoid*)(uintptr_t)(baseIndexCount * sizeof(uint32_t)));

            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glDisable(GL_BLEND);
        }
        
        chunksRendered++;
        totalTriangles += renderData.indexCount / 3;
    }
    
    glBindVertexArray(0);
    
    // 每 60 帧打印一次统计信息
    if (frameCount % 60 == 0) {
        LOGI("Frame %u: Rendered %d chunks, %d triangles", 
             frameCount, chunksRendered, totalTriangles);
    }

    eglSwapBuffers(display, surface);
}

void GLRenderer::recreateSurface(int width, int height) {
    screenWidth = width;
    screenHeight = height;
    glViewport(0, 0, width, height);
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

    // 清理纹理数组
    if (textureArrayID != 0) {
        glDeleteTextures(1, &textureArrayID);
        textureArrayID = 0;
    }
    
    // 清理所有区块的渲染数据
    for (auto& [chunkKey, renderData] : chunkRenderCache) {
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

    LOGI("OpenGL ES renderer cleaned up");
}
