#include "GLRenderer.h"
#include <android/log.h>
#include <cmath>
#include <algorithm>
#include <android/asset_manager.h>
#include <cstring>
#include <cstddef>  // for offsetof
#include <time.h>   // clock_nanosleep
#include "TextureLoader.h"
#include "MeshGenerator.h"
#include "TextureAtlas.h"
#include "BlockRegistry.h"
#include "BiomeColorManager.h"
#include "Light.h"
#include "BlockIconRasterizer.h"
#include "Camera.h"
#include "gui/GameUI.h"
#include "ClientEngine/ClientEngine.h"
#include "ClientEngine/GameEngine.h"
#include "EntityRenderer.h"
#include "EntityManager.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"

#define LOG_TAG "GLRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

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
          textureArrayID(0),
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
    // 对齐原版 RenderSystem 全局 LEQUAL：共面几何（如 grass_block 模型自带的
    // #overlay 元素）顶点逐位一致时后画者确定性覆盖，不产生 Z-fighting
    glDepthFunc(GL_LEQUAL);

    // 启用背面剔除，减少渲染的面数
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);  // 逆时针为正面

    // 同步加载 TextureAtlas（仅模型解析，快速 ~1 秒，在 UI 线程执行）
    LOGI("Initializing TextureAtlas...");
    ClientEngine::getInstance()->getTextureAtlas()->initialize();
    LOGI("TextureAtlas initialized: %d layers", ClientEngine::getInstance()->getTextureAtlas()->getLayerCount());

    // 预计算全部方块元数据（之后 getBlockMetadata 无锁访问）
    ClientEngine::getInstance()->getBlockRegistry()->precomputeAll();

    // 初始化 BiomeColorManager
    LOGI("Initializing BiomeColorManager...");
    BiomeColorManager::getInstance().initialize();

    // GL 纹理数组创建和上传延迟到渲染线程（finishTextureInit），避免 ANR
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    const char* vendor = (const char*)glGetString(GL_VENDOR);

    LOGI("=== OpenGL Info ===");
    LOGI("GL_RENDERER: %s", renderer ? renderer : "unknown");
    LOGI("GL_VERSION: %s", version ? version : "unknown");
    LOGI("GL_VENDOR: %s", vendor ? vendor : "unknown");
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    LOGI("=== GLRenderer::initialize SUCCESS ===");
    return true;
}

bool GLRenderer::finishTextureInit() {
    if (!textureInitPending) return true;


    if (textureArrayID == 0) {
        // 第一帧：创建纹理数组
        textureTotalCount = ClientEngine::getInstance()->getTextureAtlas()->getLayerCount();
        if (textureTotalCount <= 0) {
            LOGE("No texture layers to initialize");
            textureInitPending = false;
            return false;
        }

        // 获取第一张纹理的尺寸
        {
            TextureData firstTex = TextureLoader::loadImage(
                ClientEngine::getInstance()->getTextureAtlas()->getTextureFileName(0));
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
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        // 根据 mipmap 设置选择正确的 min filter，避免无 mipmap 数据时采样全黑
        int initMipLevel = GameUI::getInstance().getMipmapLevel();
        if (initMipLevel > 0) {
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, initMipLevel);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        } else {
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        }
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        textureNextBatch = 0;
    } else {
        // 后续批次：重新绑定纹理数组到 unit 0（渲染代码可能切换了活跃单元）
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);
    }

    // 逐帧分批上传纹理
    int end = std::min(textureNextBatch + TEXTURES_PER_FRAME, textureTotalCount);
    for (int i = textureNextBatch; i < end; i++) {
        std::string filename = ClientEngine::getInstance()->getTextureAtlas()->getTextureFileName(i);
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
            ClientEngine::getInstance()->getTextureAtlas()->getPlaceholderColor(i, r, g, b);
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
        LOGI("All %d textures uploaded", textureTotalCount);
        // 根据设置生成 mipmap
        int mipLevel = GameUI::getInstance().getMipmapLevel();
        if (mipLevel > 0) {
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, mipLevel);
            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
            LOGI("Mipmaps generated, max level=%d", mipLevel);
        }
        textureInitPending = false;
        LOGI("=== finishTextureInit COMPLETE ===");
        preRenderBlockIcons();
    }

    return true;
}

// ============================================================
// 方块模型 → 3D 图标纹理渲染（物品栏用）
// CPU 光栅化已抽取至 BlockIconRasterizer（GL/Vulkan 共用纯逻辑层），
// 此处仅负责 GL 纹理上传
// ============================================================

GLuint GLRenderer::renderBlockIcon(const std::string& modelName, int iconSize) {
    auto* atlas = ClientEngine::getInstance()->getTextureAtlas();
    std::vector<uint8_t> pixels;
    if (!BlockIconRasterizer::rasterize(atlas, modelName, iconSize, pixels)) {
        return 0;
    }

    // 上传为 GL 纹理
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, iconSize, iconSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    LOGI("Uploaded block icon texture for '%s'", modelName.c_str());
    return tex;
}

void GLRenderer::preRenderBlockIcons() {
    LOGI("Pre-rendering block icons for inventory...");
    auto* atlas = ClientEngine::getInstance()->getTextureAtlas();
    int rendered = 0;
    // 遍历所有已加载的 block 模型，检查是否有对应的 item 引用
    for (const auto& [itemName, parentModel] : atlas->getItemModelCache()) {
        // 跳过已有 2D 纹理的物品
        // 直接渲染 3D 图标（getItemTexture 会优先检查 2D 纹理）
        if (blockIconCache.find(itemName) != blockIconCache.end()) continue;
        GLuint tex = renderBlockIcon(parentModel, 64);
        if (tex != 0) {
            blockIconCache[itemName] = tex;
            rendered++;
        }
    }
    LOGI("Pre-rendered %d block icons", rendered);

    // 纹理分批上传期间 hotbar 已在每帧查询图标，此时 3D 图标尚未生成，
    // 回退结果被 ResourcepackManager 永久缓存；这里生成完成后统一刷新
    ResourcepackManager::getInstance().refreshItemIcons();
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
    eglConfig = config;  // 保存，用于后续 Surface 重建

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

bool GLRenderer::createShaders() {
    auto& rpm = ResourcepackManager::getInstance();
    shaderSolid = rpm.loadShaderProgram("rendertype_solid");
    shaderCutout = rpm.loadShaderProgram("rendertype_cutout");
    shaderCutoutMipped = rpm.loadShaderProgram("rendertype_cutout_mipped");
    shaderTranslucent = rpm.loadShaderProgram("rendertype_translucent");

    LOGI("Mojang shaders: solid=%d, cutout=%d, cutout_mipped=%d, translucent=%d",
         shaderSolid.program, shaderCutout.program, shaderCutoutMipped.program, shaderTranslucent.program);

    // 输出 cutout 着色器的 uniform 位置，便于诊断
    LOGI("Cutout uniforms: ModelViewMat=%d ProjMat=%d ChunkOffset=%d ColorMod=%d FogStart=%d FogEnd=%d FogColor=%d FogShape=%d Sampler0=%d Sampler2=%d TextureMat=%d",
         shaderCutout.uModelViewMat, shaderCutout.uProjMat, shaderCutout.uChunkOffset,
         shaderCutout.uColorModulator, shaderCutout.uFogStart, shaderCutout.uFogEnd,
         shaderCutout.uFogColor, shaderCutout.uFogShape,
         shaderCutout.uSampler0, shaderCutout.uSampler2, shaderCutout.uTextureMatrix);

    // 至少需要 cutout 着色器才能渲染
    if (shaderCutout.program == 0) {
        LOGE("Failed to load Mojang shaders (rendertype_cutout is required)");
        return false;
    }

    // 光照贴图纹理延迟到渲染循环中创建（需要 GameEngine 存在）

    LOGI("Mojang shaders loaded successfully");
    return true;
}

bool GLRenderer::createBuffers() {
    // 使用 per-chunk VAO（在 processCompletedWork 中创建）
    LOGI("Buffers will be created per-chunk in processCompletedWork");
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

void GLRenderer::removeChunk(int chunkX, int chunkZ) {
    uint64_t chunkKey = ((uint64_t)(chunkX & 0xFFFFFFFF) << 32) | (chunkZ & 0xFFFFFFFF);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        chunksToRemove.insert(chunkKey);
        dirtyChunks.erase(chunkKey);   // 取消排队中的更新
    }
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingChunks.erase(chunkKey);
    }
}

void GLRenderer::processChunkRemovals() {
    // 渲染线程调用（GL context 已 current）：安全删除 VAO/VBO/EBO
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (chunksToRemove.empty()) return;
    for (uint64_t chunkKey : chunksToRemove) {
        auto it = chunkRenderCache.find(chunkKey);
        if (it == chunkRenderCache.end()) continue;
        for (auto& sec : it->second.sections) {
            if (sec.vao != 0) glDeleteVertexArrays(1, &sec.vao);
            if (sec.vbo != 0) glDeleteBuffers(1, &sec.vbo);
            if (sec.ebo != 0) glDeleteBuffers(1, &sec.ebo);
        }
        chunkRenderCache.erase(it);
    }
    chunksToRemove.clear();
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
            for (auto& sec : renderData.sections) {
                if (sec.vao != 0) glDeleteVertexArrays(1, &sec.vao);
                if (sec.vbo != 0) glDeleteBuffers(1, &sec.vbo);
                if (sec.ebo != 0) glDeleteBuffers(1, &sec.ebo);
            }
            renderData.sections.clear();
        }
        chunkRenderCache.clear();
        dirtyChunks.clear();
        chunksToRemove.clear();
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

// ===== Sodium 风格遮挡剔除：预计算 section 内部连通性 =====
// 方向: 0=DOWN 1=UP 2=NORTH 3=SOUTH 4=WEST 5=EAST
// 结果: bit(from*8+to)=1 表示 from 面→to 面在 section 内部可达
static const int FACE_AXIS[6] = {1,1,2,2,0,0};
static const int FACE_VAL[6]  = {0,15,0,15,0,15};

static uint64_t computeSectionVisibility(const ChunkSection& section) {
    auto toIdx = [](int x,int y,int z){ return (y<<8)|(z<<4)|x; };
    bool isSolid[16*16*16]={false};
    int solidCount=0;
    for(int y=0;y<16;y++) for(int z=0;z<16;z++) for(int x=0;x<16;x++){
        int idx=toIdx(x,y,z);
        int32_t st=section.blockStates[idx];
        if(!st) continue;
        auto& meta=ClientEngine::getInstance()->getBlockRegistry()->getBlockMetadata(st);
        if(meta.isFullBlock&&meta.isOpaque) { isSolid[idx]=true; solidCount++; }
    }
    if(solidCount==4096) return 0;
    if(solidCount<256) return ~0ULL;
    static const int NB[6][3]={{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
    uint64_t vis=0;
    int queue[4096];
    for(int of=0;of<6;of++){
        bool visited[4096]={false};
        int qH=0,qT=0;
        int fix=FACE_AXIS[of],val=FACE_VAL[of],a1=(fix+1)%3,a2=(fix+2)%3;
        for(int d1=0;d1<16;d1++) for(int d2=0;d2<16;d2++){
            int c[3]; c[fix]=val; c[a1]=d1; c[a2]=d2;
            int idx=toIdx(c[0],c[1],c[2]);
            if(!isSolid[idx]&&!visited[idx]){ visited[idx]=true; queue[qT++]=idx; }
        }
        while(qH<qT){
            int cur=queue[qH++],cx=cur&0xF,cy=(cur>>8)&0xF,cz=(cur>>4)&0xF;
            for(int d=0;d<6;d++){
                int nx=cx+NB[d][0],ny=cy+NB[d][1],nz=cz+NB[d][2];
                if((unsigned)nx>=16||(unsigned)ny>=16||(unsigned)nz>=16) continue;
                int ni=toIdx(nx,ny,nz);
                if(!isSolid[ni]&&!visited[ni]){ visited[ni]=true; queue[qT++]=ni; }
            }
        }
        vis|=1ULL<<(of*8+of);
        for(int tf=0;tf<6;tf++){
            if(tf==of) continue;
            int tfx=FACE_AXIS[tf],tfv=FACE_VAL[tf],ta1=(tfx+1)%3,ta2=(tfx+2)%3;
            bool ok=false;
            for(int d1=0;d1<16&&!ok;d1++) for(int d2=0;d2<16&&!ok;d2++){
                int c[3]; c[tfx]=tfv; c[ta1]=d1; c[ta2]=d2;
                if(visited[toIdx(c[0],c[1],c[2])]) ok=true;
            }
            if(ok) vis|=1ULL<<(of*8+tf);
        }
    }
    return vis;
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

        ChunkMeshResult result;
        result.chunkKey = item.chunkKey;

        for (size_t sectionIdx = 0; sectionIdx < chunk->sections.size(); ++sectionIdx) {
            const auto& section = chunk->sections[sectionIdx];
            if (!section || section->isEmpty) continue;

            auto meshOut = MeshGenerator::generateSectionMesh(
                *section, item.chunkX, section->y, item.chunkZ, chunkManager,
                wl_baseVertices, wl_baseIndices,
                wl_overlayVertices, wl_overlayIndices,
                wl_waterVertices, wl_waterIndices);

            if (meshOut.vertices.empty()) continue;

            ChunkMeshResult::SectionData secData;
            secData.sectionY = section->y;
            secData.visibilityData = computeSectionVisibility(*section);

            // 在工作线程压缩 Vertex（48B）→ PackedVertex（32B），减轻渲染线程负担
            auto& srcVerts = meshOut.vertices;
            secData.packedVertices.resize(srcVerts.size());
            for (size_t vi = 0; vi < srcVerts.size(); vi++) {
                const auto& src = srcVerts[vi];
                auto& dst = secData.packedVertices[vi];
                // pos: 世界坐标，float 不压缩，无精度损失
                memcpy(dst.pos, src.pos, sizeof(float) * 3);
                dst.texIndex = src.texIndex;
                memcpy(dst.color, src.color, 4);
                // uv: [0,1] → [0,65535]
                dst.uv[0] = (uint16_t)(src.texCoord[0] * 65535.0f + 0.5f);
                dst.uv[1] = (uint16_t)(src.texCoord[1] * 65535.0f + 0.5f);
                // normal: [-1,1] → [-127,127]
                dst.normal[0] = (int8_t)(src.normal[0] * 127.0f);
                dst.normal[1] = (int8_t)(src.normal[1] * 127.0f);
                dst.normal[2] = (int8_t)(src.normal[2] * 127.0f);
                dst.normal[3] = 0;
                // uv2: lightmap [0,240] → [0,65535]
                dst.uv2[0] = (uint16_t)(src.uv2[0] + 0.5f);
                dst.uv2[1] = (uint16_t)(src.uv2[1] + 0.5f);
            }

            size_t regularCount = meshOut.indices.size()
                - meshOut.overlayIndexCount - meshOut.waterIndexCount;

            secData.baseIndices.assign(
                meshOut.indices.begin(),
                meshOut.indices.begin() + regularCount);

            size_t ovStart = regularCount;
            if (meshOut.overlayIndexCount > 0) {
                secData.overlayIndices.assign(
                    meshOut.indices.begin() + ovStart,
                    meshOut.indices.begin() + ovStart + meshOut.overlayIndexCount);
            }

            size_t watStart = ovStart + meshOut.overlayIndexCount;
            if (meshOut.waterIndexCount > 0) {
                secData.waterIndices.assign(
                    meshOut.indices.begin() + watStart,
                    meshOut.indices.end());
            }

            result.sections.push_back(std::move(secData));
        }

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            resultQueue.push(std::move(result));
        }
    }

    LOGI("Mesh worker thread stopped");
}

void GLRenderer::processCompletedWork() {
    // 先处理服务端要求卸载的区块（在上传新网格前，避免给已卸载区块白建资源）
    processChunkRemovals();

    // 将新完成的结果追加到待处理队列
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        while (!resultQueue.empty()) {
            pendingResults.push(std::move(resultQueue.front()));
            resultQueue.pop();
        }
    }

    // 每帧最多处理 MAX_CHUNKS_PER_FRAME 个 chunk（但每个 chunk 整批上传，避免闪光）
    int chunksProcessed = 0;

    while (!pendingResults.empty() && chunksProcessed < MAX_CHUNKS_PER_FRAME) {
        auto& result = pendingResults.front();
        bool processed = false;

        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = chunkRenderCache.find(result.chunkKey);
            if (it != chunkRenderCache.end()) {
                processed = true;
                auto& renderData = it->second;

                // 删除旧的 section 资源（替换为新几何体）
                for (auto& sec : renderData.sections) {
                    if (sec.vao) glDeleteVertexArrays(1, &sec.vao);
                    if (sec.vbo) glDeleteBuffers(1, &sec.vbo);
                    if (sec.ebo) glDeleteBuffers(1, &sec.ebo);
                }
                renderData.sections.clear();
                renderData.sections.reserve(result.sections.size());

                // 整批上传所有 section（同一帧内完成，保证视觉连续性）
                for (auto& secData : result.sections) {
                    SectionRenderData sec;
                    sec.sectionY = secData.sectionY;
                    sec.visibilityData = secData.visibilityData;

                    // 上传工作线程已压缩好的 VBO
                    glGenBuffers(1, &sec.vbo);
                    glBindBuffer(GL_ARRAY_BUFFER, sec.vbo);
                    glBufferData(GL_ARRAY_BUFFER,
                                secData.packedVertices.size() * sizeof(PackedVertex),
                                secData.packedVertices.data(), GL_STATIC_DRAW);

                    // 合并索引
                    std::vector<uint32_t> merged;
                    merged.reserve(secData.baseIndices.size() + secData.overlayIndices.size() + secData.waterIndices.size());
                    merged.insert(merged.end(), secData.baseIndices.begin(), secData.baseIndices.end());
                    merged.insert(merged.end(), secData.overlayIndices.begin(), secData.overlayIndices.end());
                    merged.insert(merged.end(), secData.waterIndices.begin(), secData.waterIndices.end());

                    sec.overlayIndexCount = static_cast<uint32_t>(secData.overlayIndices.size());
                    sec.waterIndexCount = static_cast<uint32_t>(secData.waterIndices.size());
                    sec.indexCount = static_cast<uint32_t>(merged.size());

                    // 创建 EBO
                    glGenBuffers(1, &sec.ebo);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sec.ebo);
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                merged.size() * sizeof(uint32_t),
                                merged.data(), GL_STATIC_DRAW);

                    // 创建并配置 VAO
                    glGenVertexArrays(1, &sec.vao);
                    glBindVertexArray(sec.vao);
                    glBindBuffer(GL_ARRAY_BUFFER, sec.vbo);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sec.ebo);
                    // location 0: Position (GL_FLOAT, 3分量)
                    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PackedVertex),
                        (void*)offsetof(PackedVertex, pos));
                    glEnableVertexAttribArray(0);
                    // location 1: UV0 (GL_UNSIGNED_SHORT, 归一化)
                    glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(PackedVertex),
                        (void*)offsetof(PackedVertex, uv));
                    glEnableVertexAttribArray(1);
                    // location 2: TexIndex (GL_FLOAT)
                    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(PackedVertex),
                        (void*)offsetof(PackedVertex, texIndex));
                    glEnableVertexAttribArray(2);
                    // location 3: Color (GL_UNSIGNED_BYTE, 归一化)
                    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PackedVertex),
                        (void*)offsetof(PackedVertex, color));
                    glEnableVertexAttribArray(3);
                    // location 4: Normal (GL_BYTE, 归一化, 4分量→第4个未用)
                    glVertexAttribPointer(4, 4, GL_BYTE, GL_TRUE, sizeof(PackedVertex),
                        (void*)offsetof(PackedVertex, normal));
                    glEnableVertexAttribArray(4);
                    // location 5: UV2 (GL_UNSIGNED_SHORT, 原始值，shader 内除以 256)
                    glVertexAttribPointer(5, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(PackedVertex),
                        (void*)offsetof(PackedVertex, uv2));
                    glEnableVertexAttribArray(5);
                    glBindVertexArray(0);

                    renderData.sections.push_back(std::move(sec));
                }

                renderData.needsUpdate = false;
                renderData.pending = false;
            }
        }

        // chunk 成功处理 → 从 pending 清除
        if (processed) {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pendingChunks.erase(result.chunkKey);
        }
        pendingResults.pop();
        chunksProcessed++;
    }
}

void GLRenderer::updateCamera(float cx, float cy, float cz, float pitch, float yaw) {
    // 矩阵数学在 Camera（图形 API 无关），这里只存入渲染状态
    glm::mat4 viewMatrix = Camera::computeViewMatrix(cx, cy, cz, pitch, yaw);
    memcpy(cameraMatrix, glm::value_ptr(viewMatrix), sizeof(float) * 16);

    float aspect = (float)screenWidth / screenHeight;
    glm::mat4 projMatrix = Camera::computeProjectionMatrix(fov, aspect, nearPlane, farPlane);
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
    // 优化版：p-vertex 测试，每个平面只检查1个顶点（最可能在平面外侧的角点）
    // 6 个平面 × 1 个顶点 = 6 次测试（原版 8 顶点 × 6 平面 = 48 次）
    for (int p = 0; p < 6; p++) {
        const auto& pl = frustumPlanes[p];
        // p-vertex: 在平面法线方向上投影最大的角点
        float px = (pl.x > 0) ? maxX : minX;
        float py = (pl.y > 0) ? maxY : minY;
        float pz = (pl.z > 0) ? maxZ : minZ;
        if (pl.x * px + pl.y * py + pl.z * pz + pl.w < 0) {
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

    // 处理挂起的 Surface 释放/重建请求（必须在持有 context 的渲染线程执行）
    processSurfaceRequests();

    // Surface 无效时跳过渲染（切屏中，等待重建）
    if (surface == EGL_NO_SURFACE) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        return;
    }

    // 保存当前帧的相机位置（供 rebuildMeshFromChunks 等使用）
    lastCameraX = cx;
    lastCameraY = cy;
    lastCameraZ = cz;

    auto& ui = GameUI::getInstance();

    // 菜单状态：渲染全景背景 + ImGui
    if (ui.getState() != UIState::IN_GAME) {
        glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 渲染旋转全景到默认帧缓冲（在 ImGui 之前）
        initPanorama();
        renderPanoramaToFBO();

        renderUI();
        eglSwapBuffers(display, surface);
        limitFramerate();
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

    // ===== 昼夜循环：更新天空颜色 + 光照贴图上传（GL 纹理归渲染器所有，Light 只产出像素）=====
    auto* gameForLight = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (gameForLight && gameForLight->getLight()) {
        auto* light = gameForLight->getLight();
        light->update();

        // 延迟创建光照贴图纹理（EGL context 重建后也走这里，失效缓存保证首帧必上传）
        if (lightmapTextureID == 0) {
            glGenTextures(1, &lightmapTextureID);
            glBindTexture(GL_TEXTURE_2D, lightmapTextureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            light->invalidateLightmapCache();
            LOGI("Created lightmap texture 16x16 (GL upload layer)");
        }

        uint8_t lightmapPixels[16 * 16 * 4];
        if (light->getLightmapPixelsIfChanged(lightmapPixels)) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, lightmapTextureID);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, lightmapPixels);
        }
    }
    float skyR = (gameForLight && gameForLight->getLight()) ? gameForLight->getLight()->getSkyColorR() : 0.53f;
    float skyG = (gameForLight && gameForLight->getLight()) ? gameForLight->getLight()->getSkyColorG() : 0.81f;
    float skyB = (gameForLight && gameForLight->getLight()) ? gameForLight->getLight()->getSkyColorB() : 0.92f;

    glClearColor(skyR, skyG, skyB, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 帧计数器递增
    frameCount++;

    updateCamera(cx, cy, cz, pitch, yaw);
    glm::mat4 viewMatrix = glm::make_mat4(cameraMatrix);
    glm::mat4 projMatrix = glm::make_mat4(projectionMatrix);
    computeFrustumPlanes(projMatrix * viewMatrix);

    // ===== Phase 1: 使用 rendertype_cutout 渲染基体 + 覆盖层 =====
    if (shaderCutout.program == 0) {
        renderUI();
        eglSwapBuffers(display, surface);
        limitFramerate();
        return;
    }

    glUseProgram(shaderCutout.program);

    // 设置公共 uniform
    glm::mat4 modelMat(1.0f);
    glm::mat4 modelViewMat = viewMatrix * modelMat;
    if (shaderCutout.uModelViewMat != -1)
        glUniformMatrix4fv(shaderCutout.uModelViewMat, 1, GL_FALSE, glm::value_ptr(modelViewMat));
    if (shaderCutout.uProjMat != -1)
        glUniformMatrix4fv(shaderCutout.uProjMat, 1, GL_FALSE, glm::value_ptr(projMatrix));
    if (shaderCutout.uChunkOffset != -1)
        glUniform3f(shaderCutout.uChunkOffset, 0.0f, 0.0f, 0.0f);
    if (shaderCutout.uColorModulator != -1)
        glUniform4f(shaderCutout.uColorModulator, 1.0f, 1.0f, 1.0f, 1.0f);
    if (shaderCutout.uFogShape != -1)
        glUniform1i(shaderCutout.uFogShape, 0);
    if (shaderCutout.uTextureMatrix != -1)
        glUniformMatrix4fv(shaderCutout.uTextureMatrix, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));

    // 雾效
    float fogEnd = farPlane;
    float fogStart = farPlane * 0.7f;
    if (shaderCutout.uFogStart != -1) glUniform1f(shaderCutout.uFogStart, fogStart);
    if (shaderCutout.uFogEnd != -1) glUniform1f(shaderCutout.uFogEnd, fogEnd);
    if (shaderCutout.uFogColor != -1)
        glUniform4f(shaderCutout.uFogColor, skyR, skyG, skyB, 1.0f);

    // 绑定纹理数组到 Sampler0
    glActiveTexture(GL_TEXTURE0);
    if (textureArrayID != 0) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);
    } else {
        LOGE("Texture array not initialized! No texture bound - all blocks will be black");
    }
    if (shaderCutout.uSampler0 != -1) glUniform1i(shaderCutout.uSampler0, 0);

    // 绑定光照贴图到 Sampler2
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, lightmapTextureID);
    {
        GLint s2loc = shaderCutout.uSampler2;
        if (s2loc == -1) {
            s2loc = glGetUniformLocation(shaderCutout.program, "Sampler2");
        }
        if (s2loc != -1) glUniform1i(s2loc, 2);
    }

    // ===== 合批渲染所有可见区块 =====
    int chunksRendered = 0;
    int totalTriangles = 0;

    const auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    float worldMinY = (float)(game ? game->getDimensionMinY() : -64);
    float worldMaxY = (float)(game ? (game->getDimensionMinY() + game->getDimensionHeight()) : 320);

    // 加锁保护 chunkRenderCache
    std::lock_guard<std::mutex> renderLock(cacheMutex);

    // 启用透明混合（玻璃、树叶等需要 alpha blend）
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ---- Phase 1a: 基体几何（纯深度测试，写深度）----
    for (auto& [chunkKey, renderData] : chunkRenderCache) {
        if (!renderData.visible || renderData.sections.empty()) continue;

        float minX = renderData.position.x;
        float maxX = minX + 16.0f;
        float minZ = renderData.position.z;
        float maxZ = minZ + 16.0f;

        for (auto& sec : renderData.sections) {
            // Section 级视锥裁剪
            float secMinY = (float)sec.sectionY;
            float secMaxY = secMinY + 16.0f;
            if (!isAABBInFrustum(minX, secMinY, minZ, maxX, secMaxY, maxZ)) continue;

            uint32_t baseEnd = sec.indexCount - sec.overlayIndexCount - sec.waterIndexCount;
            if (baseEnd == 0) continue;

            glBindVertexArray(sec.vao);
            glDrawElements(GL_TRIANGLES, baseEnd, GL_UNSIGNED_INT, 0);

            totalTriangles += baseEnd / 3;
        }
        if (!renderData.sections.empty()) chunksRendered++;
    }

    // ---- Phase 1b: 覆盖层（LEQUAL depth，不写深度）----
    {
        bool blendEnabled = false;
        for (auto& [chunkKey, renderData] : chunkRenderCache) {
            if (!renderData.visible) continue;
            float minX = renderData.position.x;
            float maxX = minX + 16.0f;
            float minZ = renderData.position.z;
            float maxZ = minZ + 16.0f;

            for (auto& sec : renderData.sections) {
                if (sec.overlayIndexCount == 0) continue;
                float secMinY = (float)sec.sectionY;
                float secMaxY = secMinY + 16.0f;
                if (!isAABBInFrustum(minX, secMinY, minZ, maxX, secMaxY, maxZ)) continue;

                if (!blendEnabled) {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    blendEnabled = true;
                }

                glBindVertexArray(sec.vao);
                uint32_t baseEnd = sec.indexCount - sec.overlayIndexCount - sec.waterIndexCount;

                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_FALSE);
                glDrawElements(GL_TRIANGLES, sec.overlayIndexCount, GL_UNSIGNED_INT,
                               (const GLvoid*)(uintptr_t)(baseEnd * sizeof(uint32_t)));
                glDepthMask(GL_TRUE);

                totalTriangles += sec.overlayIndexCount / 3;
            }
        }
        if (blendEnabled) glDisable(GL_BLEND);
    }

    // ===== Phase 2: 使用 rendertype_translucent 渲染水 =====
    if (shaderTranslucent.program != 0) {
        bool waterStateSet = false;
        for (auto& [chunkKey, renderData] : chunkRenderCache) {
            if (!renderData.visible) continue;
            float minX = renderData.position.x;
            float maxX = minX + 16.0f;
            float minZ = renderData.position.z;
            float maxZ = minZ + 16.0f;

            for (auto& sec : renderData.sections) {
                if (sec.waterIndexCount == 0) continue;
                float secMinY = (float)sec.sectionY;
                float secMaxY = secMinY + 16.0f;
                if (!isAABBInFrustum(minX, secMinY, minZ, maxX, secMaxY, maxZ)) continue;

                if (!waterStateSet) {
                    glUseProgram(shaderTranslucent.program);

                    if (shaderTranslucent.uModelViewMat != -1)
                        glUniformMatrix4fv(shaderTranslucent.uModelViewMat, 1, GL_FALSE, glm::value_ptr(modelViewMat));
                    if (shaderTranslucent.uProjMat != -1)
                        glUniformMatrix4fv(shaderTranslucent.uProjMat, 1, GL_FALSE, glm::value_ptr(projMatrix));
                    if (shaderTranslucent.uChunkOffset != -1)
                        glUniform3f(shaderTranslucent.uChunkOffset, 0.0f, 0.0f, 0.0f);
                    if (shaderTranslucent.uColorModulator != -1)
                        glUniform4f(shaderTranslucent.uColorModulator, 1.0f, 1.0f, 1.0f, 1.0f);
                    if (shaderTranslucent.uFogShape != -1)
                        glUniform1i(shaderTranslucent.uFogShape, 0);
                    if (shaderTranslucent.uTextureMatrix != -1)
                        glUniformMatrix4fv(shaderTranslucent.uTextureMatrix, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
                    if (shaderTranslucent.uFogStart != -1) glUniform1f(shaderTranslucent.uFogStart, fogStart);
                    if (shaderTranslucent.uFogEnd != -1) glUniform1f(shaderTranslucent.uFogEnd, fogEnd);
                    if (shaderTranslucent.uFogColor != -1)
                        glUniform4f(shaderTranslucent.uFogColor, skyR, skyG, skyB, 1.0f);
                    if (shaderTranslucent.uSampler0 != -1) glUniform1i(shaderTranslucent.uSampler0, 0);
                    if (shaderTranslucent.uSampler2 != -1) glUniform1i(shaderTranslucent.uSampler2, 2);

                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glDepthFunc(GL_LEQUAL);
                    glDepthMask(GL_FALSE);
                    waterStateSet = true;
                }

                glBindVertexArray(sec.vao);
                uint32_t baseEnd = sec.indexCount - sec.overlayIndexCount - sec.waterIndexCount;
                uint32_t overlayEnd = baseEnd + sec.overlayIndexCount;
                glDrawElements(GL_TRIANGLES, sec.waterIndexCount, GL_UNSIGNED_INT,
                               (const GLvoid*)(uintptr_t)(overlayEnd * sizeof(uint32_t)));
            }
        }

        if (waterStateSet) {
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }

    glBindVertexArray(0);

    // ===== Phase 3: 实体渲染 =====
    {
        auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
        auto* er = ClientEngine::getInstance() ? ClientEngine::getInstance()->getEntityRenderer() : nullptr;
        if (game && er) {
            if (!er->isInitialized()) er->init();
            auto entities = game->getEntityManager()->getAllEntities();
            if (!entities.empty()) {
                er->renderAll(entities, viewMatrix, projMatrix, 1.0f);
            }
        }
    }

    // ===== Phase 4: 方块破坏覆盖层 =====
    renderCrackOverlay(viewMatrix, projMatrix, shaderCutout);

    // 每 60 帧打印统计信息
    if (frameCount % 60 == 0) {
        LOGI("Frame %u: Rendered %d chunks, %d triangles",
             frameCount, chunksRendered, totalTriangles);
    }

    // 游戏内 UI 叠加
    renderUI();

    eglSwapBuffers(display, surface);

    // 帧率限制（统一使用绝对时间）
    limitFramerate();
}

bool GLRenderer::initImGui() {
    // 同进程内可能先跑过 Vulkan（GameUI 是单例，vulkanBackend 会残留为 true），
    // 必须显式设回 false，否则 init() 跳过 ImGui_ImplOpenGL3_Init、render() 走 Vulkan NewFrame 导致崩溃
    GameUI::getInstance().setVulkanBackend(false);
    return GameUI::getInstance().init();
}

void GLRenderer::renderUI() {
    GameUI::getInstance().render();
}

// ===== 方块破坏覆盖层渲染 =====
void GLRenderer::renderCrackOverlay(const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                     const ShaderProgramInfo& shader) {
    // 几何生成与脏检查在 CrackOverlayMesh（图形 API 无关），这里只做上传与绘制
    bool rebuilt = false;
    if (!crackMesh.update(rebuilt)) return;

    // 重建或 VAO 尚未创建（含 EGL context 重建后）时重新上传
    if (rebuilt || crackVAO == 0) {
        const auto& verts = crackMesh.getVertices();
        const auto& idx = crackMesh.getIndices();

        // 仅 24 顶点，不值得走 PackedVertex 压缩，直接上传 Vertex 原始布局
        if (crackVAO == 0) {
            glGenVertexArrays(1, &crackVAO);
            glGenBuffers(1, &crackVBO);
            glGenBuffers(1, &crackEBO);
        }

        glBindVertexArray(crackVAO);
        glBindBuffer(GL_ARRAY_BUFFER, crackVBO);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, crackEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_DYNAMIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texIndex));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, color));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv2));
        glEnableVertexAttribArray(5);
    }

    // ===== 绘制 =====
    glUseProgram(shader.program);

    glm::mat4 modelMat(1.0f);
    glm::mat4 modelViewMat = viewMatrix * modelMat;
    if (shader.uModelViewMat != -1)
        glUniformMatrix4fv(shader.uModelViewMat, 1, GL_FALSE, glm::value_ptr(modelViewMat));
    if (shader.uProjMat != -1)
        glUniformMatrix4fv(shader.uProjMat, 1, GL_FALSE, glm::value_ptr(projMatrix));
    if (shader.uChunkOffset != -1)
        glUniform3f(shader.uChunkOffset, 0.0f, 0.0f, 0.0f);
    if (shader.uColorModulator != -1)
        glUniform4f(shader.uColorModulator, 1.0f, 1.0f, 1.0f, 1.0f);
    if (shader.uFogShape != -1) glUniform1i(shader.uFogShape, 0);
    if (shader.uTextureMatrix != -1)
        glUniformMatrix4fv(shader.uTextureMatrix, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));

    float fogEnd = farPlane;
    float fogStart = farPlane * 0.7f;
    float skyR = (ClientEngine::getInstance() && ClientEngine::getInstance()->getGame() && ClientEngine::getInstance()->getGame()->getLight()) ? ClientEngine::getInstance()->getGame()->getLight()->getSkyColorR() : 0.53f;
    float skyG = (ClientEngine::getInstance() && ClientEngine::getInstance()->getGame() && ClientEngine::getInstance()->getGame()->getLight()) ? ClientEngine::getInstance()->getGame()->getLight()->getSkyColorG() : 0.81f;
    float skyB = (ClientEngine::getInstance() && ClientEngine::getInstance()->getGame() && ClientEngine::getInstance()->getGame()->getLight()) ? ClientEngine::getInstance()->getGame()->getLight()->getSkyColorB() : 0.92f;
    if (shader.uFogStart != -1) glUniform1f(shader.uFogStart, fogStart);
    if (shader.uFogEnd != -1) glUniform1f(shader.uFogEnd, fogEnd);
    if (shader.uFogColor != -1) glUniform4f(shader.uFogColor, skyR, skyG, skyB, 1.0f);

    // 确保纹理数组绑定到 unit 0（实体渲染可能改变了活跃纹理）
    glActiveTexture(GL_TEXTURE0);
    if (textureArrayID != 0)
        glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);
    if (shader.uSampler0 != -1) glUniform1i(shader.uSampler0, 0);
    if (shader.uSampler2 != -1) glUniform1i(shader.uSampler2, 2);

    // 禁用面剔除，确保覆盖层从任意角度可见
    glDisable(GL_CULL_FACE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    // 用 PolygonOffset 将覆盖层向相机拉近，避免与方块面 z-fighting
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);

    glBindVertexArray(crackVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    // 恢复 OpenGL 状态
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_CULL_FACE);
}

void GLRenderer::setFov(float degrees) {
    fov = degrees;
}

void GLRenderer::setRenderDistance(int chunks) {
    farPlane = chunks * 16.0f;
}

void GLRenderer::setMipmapLevel(int level) {
    if (textureArrayID == 0) return;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);

    if (level > 0) {
        // 限制 mipmap 层级数（level=1 只用 2 层: 0+1，level=4 用全部 5 层: 0+1+2+3+4）
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, level);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        LOGI("Mipmap level set to %d", level);
    } else {
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        LOGI("Mipmap disabled");
    }
}

void GLRenderer::setMaxFps(int fps) {
    maxFps = fps;
    if (fps > 0 && fps < 256) {
        frameIntervalNs = NANOSECONDS_PER_SECOND / fps;
    } else {
        frameIntervalNs = 0;
    }
    // 重置基准，下次限帧时重新以当前时间开始
    frameTimeBaseValid = false;

    // 根据设置调整 eglSwapInterval
    int newInterval;
    if (fps == 0) {
        newInterval = 1;  // VSync
    } else {
        newInterval = 0;  // 关闭 VSync，由 CPU 控制帧率
    }
    if (newInterval != currentSwapInterval) {
        if (display != EGL_NO_DISPLAY) {
            eglSwapInterval(display, newInterval);
        }
        currentSwapInterval = newInterval;
    }
    LOGI("MaxFps set to %d, interval=%lld ns, swapInterval=%d", fps, frameIntervalNs, newInterval);
}

void GLRenderer::limitFramerate() {
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

void GLRenderer::requestSurfaceRelease() {
    // JNI/UI 线程调用：将释放请求交给渲染线程（context 持有者）执行，
    // 并阻塞等待，确保返回 Android 前 EGL Surface 已真正销毁（避免悬空的 ANativeWindow）。
    std::unique_lock<std::mutex> lk(surfaceReqMutex);
    surfaceReleaseReq = true;
    surfaceReqHandled = false;
    // 带超时等待，防止渲染线程已停止时死锁
    surfaceReqCV.wait_for(lk, std::chrono::milliseconds(500), [this]{ return surfaceReqHandled; });
}

void GLRenderer::requestSurfaceRecreate(ANativeWindow* window) {
    // JNI/UI 线程调用：转移 window 所有权给渲染线程（后者处理完后负责 release）。
    // 重建无需阻塞：surfaceChanged 返回后新 Surface 仍然有效。
    std::lock_guard<std::mutex> lk(surfaceReqMutex);
    // 若已有未消费的重建请求，先释放旧 window ref
    if (surfaceRecreateReqWindow) {
        ANativeWindow_release(surfaceRecreateReqWindow);
    }
    surfaceRecreateReqWindow = window;  // 所有权转移
    surfaceReleaseReq = false;          // 重建优先，取消待处理的释放
}

void GLRenderer::processSurfaceRequests() {
    // 渲染线程调用（持有 EGL context）：执行挂起的 Surface 释放/重建。
    ANativeWindow* recreateWindow = nullptr;
    bool doRelease = false;
    {
        std::lock_guard<std::mutex> lk(surfaceReqMutex);
        recreateWindow = surfaceRecreateReqWindow;
        surfaceRecreateReqWindow = nullptr;
        doRelease = surfaceReleaseReq;
    }

    if (recreateWindow) {
        // 切回：在渲染线程上用新 window 重建 Surface（内部会 eglMakeCurrent 重新绑定）
        recreateSurface(recreateWindow);
        ANativeWindow_release(recreateWindow);
        std::lock_guard<std::mutex> lk(surfaceReqMutex);
        surfaceReleaseReq = false;
        surfaceReqHandled = true;
        surfaceReqCV.notify_all();
    } else if (doRelease) {
        // 切出：在渲染线程上解绑并销毁 Surface
        releaseSurface();
        std::lock_guard<std::mutex> lk(surfaceReqMutex);
        surfaceReleaseReq = false;
        surfaceReqHandled = true;
        surfaceReqCV.notify_all();
    }
}

void GLRenderer::releaseSurface() {
    if (display == EGL_NO_DISPLAY) return;

    // 解绑当前上下文
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if (surface != EGL_NO_SURFACE) {
        eglDestroySurface(display, surface);
        surface = EGL_NO_SURFACE;
        LOGI("EGL Surface released (context preserved)");
    }
}

bool GLRenderer::recreateSurface(ANativeWindow* window) {
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        LOGE("Cannot recreate surface: display or context invalid");
        return false;
    }

    // 如果旧 Surface 还在，先释放
    if (surface != EGL_NO_SURFACE) {
        releaseSurface();
    }

    // 重新创建 Surface
    surface = eglCreateWindowSurface(display, eglConfig, window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        LOGE("Failed to recreate EGL surface: 0x%x", eglGetError());
        return false;
    }

    // 重新绑定上下文
    if (!eglMakeCurrent(display, surface, surface, context)) {
        LOGE("Failed to make current after surface recreation: 0x%x", eglGetError());
        eglDestroySurface(display, surface);
        surface = EGL_NO_SURFACE;
        return false;
    }

    // 更新窗口尺寸
    screenWidth = ANativeWindow_getWidth(window);
    screenHeight = ANativeWindow_getHeight(window);
    glViewport(0, 0, screenWidth, screenHeight);

    // 更新 ImGui 尺寸
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)screenWidth, (float)screenHeight);

    // 重置限帧基准（Surface 重建后时间线不连续）
    frameTimeBaseValid = false;

    LOGI("EGL Surface recreated: %dx%d", screenWidth, screenHeight);
    return true;
}

void GLRenderer::recreateSurface(int width, int height) {
    screenWidth = width;
    screenHeight = height;
    glViewport(0, 0, width, height);

    // 更新 ImGui 显示尺寸
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
}

// ===== 全景背景（主菜单旋转 cubemap）=====

void GLRenderer::initPanorama() {
    if (panoramaLoaded) return;
    panoramaLoaded = true;  // 防止重复尝试

    // 检查 GL 状态
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOGW("GL error before panorama init: 0x%x", err);
    }

    // 1. 创建 cubemap 纹理（面像素加载与翻转在 PanoramaView，图形 API 无关）
    glGenTextures(1, &panoramaCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, panoramaCubemap);

    PanoramaView::FacePixels faces[6];
    int loadedCount = panoramaView.loadFacePixels(faces);
    for (int i = 0; i < 6; i++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA,
                     faces[i].width, faces[i].height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     faces[i].rgba.data());
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    err = glGetError();
    if (err != GL_NO_ERROR) {
        LOGE("GL error after cubemap upload: 0x%x", err);
    }

    LOGI("Panorama cubemap: loaded %d/6 faces, texture ID=%d", loadedCount, panoramaCubemap);

    // 2. 编译着色器
    const char* vsrc = R"(#version 300 es
layout(location=0) in vec3 aPos;
out vec3 vDir;
uniform mat4 uMVP;
void main() {
    vDir = aPos;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

    const char* fsrc = R"(#version 300 es
precision mediump float;
in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uCubemap;
void main() {
    FragColor = texture(uCubemap, vDir);
}
)";

    auto compileShader = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(s, 512, nullptr, log);
            LOGE("Panorama shader compile error: %s", log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    };

    GLuint vs = compileShader(GL_VERTEX_SHADER, vsrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsrc);
    if (!vs || !fs) {
        LOGE("Panorama shader compilation failed (vs=%d, fs=%d)", vs, fs);
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        glDeleteTextures(1, &panoramaCubemap);
        panoramaCubemap = 0;
        return;
    }

    panoramaProgram = glCreateProgram();
    glAttachShader(panoramaProgram, vs);
    glAttachShader(panoramaProgram, fs);
    glLinkProgram(panoramaProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linkOk;
    glGetProgramiv(panoramaProgram, GL_LINK_STATUS, &linkOk);
    if (!linkOk) {
        char log[512];
        glGetProgramInfoLog(panoramaProgram, 512, nullptr, log);
        LOGE("Panorama program link error: %s", log);
        glDeleteProgram(panoramaProgram);
        glDeleteTextures(1, &panoramaCubemap);
        panoramaProgram = 0;
        panoramaCubemap = 0;
        return;
    }

    LOGI("Panorama shaders compiled, program ID=%d", panoramaProgram);

    // 3. 创建立方体 VAO/VBO/EBO（几何数据来自 PanoramaView）
    size_t cubeVertFloats = 0, cubeIdxCount = 0;
    const float* cubeVerts = PanoramaView::cubeVertices(cubeVertFloats);
    const uint16_t* cubeIdx = PanoramaView::cubeIndices(cubeIdxCount);

    glGenVertexArrays(1, &panoramaVAO);
    glGenBuffers(1, &panoramaVBO);
    glGenBuffers(1, &panoramaEBO);

    glBindVertexArray(panoramaVAO);
    glBindBuffer(GL_ARRAY_BUFFER, panoramaVBO);
    glBufferData(GL_ARRAY_BUFFER, cubeVertFloats * sizeof(float), cubeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, panoramaEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, cubeIdxCount * sizeof(uint16_t), cubeIdx, GL_STATIC_DRAW);

    glBindVertexArray(0);

    err = glGetError();
    if (err != GL_NO_ERROR) {
        LOGE("GL error after VAO setup: 0x%x", err);
    }

    // 4. 创建 FBO + 2D 纹理（将 cubemap 渲染到 2D 纹理，供 ImGui 显示）
    panoramaFBOWidth = screenWidth > 0 ? screenWidth : 1280;
    panoramaFBOHeight = screenHeight > 0 ? screenHeight : 720;

    glGenTextures(1, &panoramaFBOTexture);
    glBindTexture(GL_TEXTURE_2D, panoramaFBOTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, panoramaFBOWidth, panoramaFBOHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &panoramaFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, panoramaFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           panoramaFBOTexture, 0);

    GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("Panorama FBO incomplete: 0x%x", fboStatus);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    LOGI("Panorama background initialized OK (cubemap=%d, program=%d, vao=%d, fbo=%d, fboTex=%d, %dx%d)",
         panoramaCubemap, panoramaProgram, panoramaVAO, panoramaFBO, panoramaFBOTexture,
         panoramaFBOWidth, panoramaFBOHeight);
}

void GLRenderer::renderPanoramaToFBO() {
    if (!panoramaLoaded || panoramaProgram == 0 || panoramaCubemap == 0) {
        return;
    }

    // 直接渲染到默认帧缓冲（不用 FBO）
    glUseProgram(panoramaProgram);

    // 旋转动画 MVP 由 PanoramaView 计算（图形 API 无关）
    glm::mat4 mvp = panoramaView.computeMVP(screenWidth, screenHeight);

    GLint uMVP = glGetUniformLocation(panoramaProgram, "uMVP");
    GLint uCubemap = glGetUniformLocation(panoramaProgram, "uCubemap");
    glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1i(uCubemap, 0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, panoramaCubemap);
    glBindVertexArray(panoramaVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, 0);

    // 恢复 GL 状态
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
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

    // 清理光照贴图纹理
    if (lightmapTextureID != 0) {
        glDeleteTextures(1, &lightmapTextureID);
        lightmapTextureID = 0;
    }

    // 清理 Mojang 官方着色器程序
    if (shaderSolid.program != 0) {
        glDeleteProgram(shaderSolid.program);
        shaderSolid.program = 0;
    }
    if (shaderCutout.program != 0) {
        glDeleteProgram(shaderCutout.program);
        shaderCutout.program = 0;
    }
    if (shaderCutoutMipped.program != 0) {
        glDeleteProgram(shaderCutoutMipped.program);
        shaderCutoutMipped.program = 0;
    }
    if (shaderTranslucent.program != 0) {
        glDeleteProgram(shaderTranslucent.program);
        shaderTranslucent.program = 0;
    }
    
    // 清理所有区块的渲染数据
    for (auto& [chunkKey, renderData] : chunkRenderCache) {
        for (auto& sec : renderData.sections) {
            if (sec.vao != 0) {
                glDeleteVertexArrays(1, &sec.vao);
            }
            if (sec.vbo != 0) {
                glDeleteBuffers(1, &sec.vbo);
            }
            if (sec.ebo != 0) {
                glDeleteBuffers(1, &sec.ebo);
            }
        }
    }
    chunkRenderCache.clear();

    // 清理方块图标缓存
    for (auto& [name, tex] : blockIconCache) {
        glDeleteTextures(1, &tex);
    }
    blockIconCache.clear();

    // 清理全景背景资源
    if (panoramaFBO != 0)       { glDeleteFramebuffers(1, &panoramaFBO); panoramaFBO = 0; }
    if (panoramaFBOTexture != 0){ glDeleteTextures(1, &panoramaFBOTexture); panoramaFBOTexture = 0; }
    if (panoramaCubemap != 0)   { glDeleteTextures(1, &panoramaCubemap); panoramaCubemap = 0; }
    if (panoramaProgram != 0)   { glDeleteProgram(panoramaProgram); panoramaProgram = 0; }
    if (panoramaVAO != 0)       { glDeleteVertexArrays(1, &panoramaVAO); panoramaVAO = 0; }
    if (panoramaVBO != 0)       { glDeleteBuffers(1, &panoramaVBO); panoramaVBO = 0; }
    if (panoramaEBO != 0)       { glDeleteBuffers(1, &panoramaEBO); panoramaEBO = 0; }
    panoramaLoaded = false;

    // 重置 GameUI 的 GL 纹理 ID（context 销毁后旧 ID 失效，下次渲染时重新加载）
    GameUI::getInstance().resetGLResources();

    // 清理破坏覆盖层资源
    if (crackVAO != 0) { glDeleteVertexArrays(1, &crackVAO); crackVAO = 0; }
    if (crackVBO != 0) { glDeleteBuffers(1, &crackVBO); crackVBO = 0; }
    if (crackEBO != 0) { glDeleteBuffers(1, &crackEBO); crackEBO = 0; }

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
        eglConfig = nullptr;
    }

    // 关闭 ImGui
    GameUI::getInstance().shutdown();

    LOGI("OpenGL ES renderer cleaned up");
}
