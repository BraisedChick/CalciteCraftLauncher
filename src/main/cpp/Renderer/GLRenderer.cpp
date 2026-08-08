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

GLRenderer::GLRenderer()
        : display(EGL_NO_DISPLAY), context(EGL_NO_CONTEXT), surface(EGL_NO_SURFACE),
          textureArrayID(0),
          vertexCount(0), indexCount(0), screenWidth(0), screenHeight(0) {
    memset(cameraMatrix, 0, sizeof(cameraMatrix));
    memset(projectionMatrix, 0, sizeof(projectionMatrix));
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

void GLRenderer::processChunkRemovals() {
    // 渲染线程调用（GL context 已 current）：领取调度器的待卸载 key，安全删除 VAO/VBO/EBO
    auto* scheduler = ClientEngine::getInstance() ? ClientEngine::getInstance()->getMeshScheduler() : nullptr;
    if (!scheduler) return;
    std::vector<uint64_t> removeKeys;
    if (!scheduler->pollRemovals(removeKeys)) return;
    std::lock_guard<std::mutex> lock(cacheMutex);
    for (uint64_t chunkKey : removeKeys) {
        auto it = chunkRenderCache.find(chunkKey);
        if (it == chunkRenderCache.end()) continue;
        for (auto& sec : it->second.sections) {
            if (sec.vao != 0) glDeleteVertexArrays(1, &sec.vao);
            if (sec.vbo != 0) glDeleteBuffers(1, &sec.vbo);
            if (sec.ebo != 0) glDeleteBuffers(1, &sec.ebo);
        }
        chunkRenderCache.erase(it);
    }
    occlusionDirty = true;  // 缓存发生删除，下帧重算可见集
}

void GLRenderer::clearChunks() {
    pendingClear.store(true);
}

void GLRenderer::doClearChunks() {
    LOGI("doClearChunks: Clearing all chunk render data...");

    // 1. 清空调度器全部状态（脏标记/队列/worker 重启，单点清理避免双清吞掉新脏标记）
    if (auto* scheduler = ClientEngine::getInstance() ? ClientEngine::getInstance()->getMeshScheduler() : nullptr) {
        scheduler->clearAll();
    }

    // 2. 删除所有区块的 VAO/VBO/EBO（渲染线程，GL context 已 current）
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
    }

    LOGI("doClearChunks: All chunk render data cleared");
}

void GLRenderer::processCompletedWork() {
    auto* scheduler = ClientEngine::getInstance() ? ClientEngine::getInstance()->getMeshScheduler() : nullptr;
    if (!scheduler) return;

    // 先处理服务端要求卸载的区块（在上传新网格前，避免给已卸载区块白建资源）
    processChunkRemovals();

    // 每帧最多领取 MAX_CHUNKS_PER_FRAME 个 chunk（但每个 chunk 整批上传，避免闪光）
    std::vector<ChunkMeshResult> results;
    if (scheduler->pollResults(results, MAX_CHUNKS_PER_FRAME) == 0) return;

    std::lock_guard<std::mutex> lock(cacheMutex);
    for (auto& result : results) {
        auto it = chunkRenderCache.find(result.chunkKey);
        if (it == chunkRenderCache.end()) {
            // 空结果且无旧条目：无需创建缓存
            if (result.sections.empty()) continue;
            // 首个结果到达时创建缓存条目（position 从 chunkKey 解码）
            int chunkX = (int)(result.chunkKey >> 32);
            int chunkZ = (int)(result.chunkKey & 0xFFFFFFFF);
            it = chunkRenderCache.try_emplace(result.chunkKey).first;
            it->second.position = glm::vec3(chunkX * 16.0f, 0.0f, chunkZ * 16.0f);
        }
        auto& renderData = it->second;

        // 只删除掩码覆盖的旧 section 资源（部分替换，其余 section 不动）
        // 掩码内但新结果里没有的 section = 重建后变空，直接移除
        for (auto secIt = renderData.sections.begin(); secIt != renderData.sections.end();) {
            if (result.sectionMask & (1ULL << ((secIt->sectionY >> 4) & 63))) {
                if (secIt->vao) glDeleteVertexArrays(1, &secIt->vao);
                if (secIt->vbo) glDeleteBuffers(1, &secIt->vbo);
                if (secIt->ebo) glDeleteBuffers(1, &secIt->ebo);
                secIt = renderData.sections.erase(secIt);
            } else {
                ++secIt;
            }
        }
        renderData.sections.reserve(renderData.sections.size() + result.sections.size());

        // 整批上传掩码内重建的 section（同一帧内完成，保证视觉连续性）
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
    }
    occlusionDirty = true;  // 缓存新增/重建 section，下帧重算可见集
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

    // 区块网格调度（公共组件：脏标记消化 + 新区块发现 + worker 派发）
    if (auto* scheduler = ClientEngine::getInstance() ? ClientEngine::getInstance()->getMeshScheduler() : nullptr) {
        scheduler->update(cx, cy, cz, farPlane);
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

    // 初始化天空渲染（首次调用）
    initSky();

    // 渲染天空（天空圆盘 + 太阳 + 月亮 + 星星）
    auto skyParams = SkyRenderer::computeSkyParams(
            gameForLight ? gameForLight->getLight() : nullptr
    );
    renderSky(glm::make_mat4(cameraMatrix), glm::make_mat4(projectionMatrix),
              skyR, skyG, skyB, skyParams.timeOfDay,
              skyParams.starBrightness, skyParams.moonPhase);

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

    // BFS 遮挡剔除：相机跨 section 或缓存变动才重算，否则复用上次可见集
    // 旁观者相机嵌入实心方块穿地时关闭遮挡剔除（对齐原版 LevelRenderer），避免洞穴被误剔除消失
    bool cullEnabled = !(game && game->getGameMode() == 3 && game->isEyeInsideOpaqueBlock(cx, cy + 1.62, cz));
    occlusionCuller.update(cx, cy, cz, (int)worldMinY, (int)worldMaxY, farPlane, occlusionDirty, cullEnabled,
        [this](std::unordered_map<uint64_t, uint64_t>& out) {
            for (auto& [ckey, rd] : chunkRenderCache) {
                int cX = (int)(ckey >> 32);
                int cZ = (int)(ckey & 0xFFFFFFFF);
                for (auto& s : rd.sections)
                    out[ChunkOcclusionCuller::sectionKey(cX, cZ, s.sectionY >> 4)] = s.visibilityData;
            }
        });
    occlusionDirty = false;
    // section 是否被遮挡剔除（无结果时不剔除，保证首帧/异常安全）
    auto occluded = [this](uint64_t ckey, int sectionY) {
        return occlusionCuller.hasResult() &&
            !occlusionCuller.isVisible(ChunkOcclusionCuller::sectionKey(
                (int)(ckey >> 32), (int)(ckey & 0xFFFFFFFF), sectionY >> 4));
    };

    // ---- Phase 1a: 基体几何（纯深度测试，写深度）----
    for (auto& [chunkKey, renderData] : chunkRenderCache) {
        if (renderData.sections.empty()) continue;

        float minX = renderData.position.x;
        float maxX = minX + 16.0f;
        float minZ = renderData.position.z;
        float maxZ = minZ + 16.0f;

        for (auto& sec : renderData.sections) {
            // Section 级视锥裁剪
            float secMinY = (float)sec.sectionY;
            float secMaxY = secMinY + 16.0f;
            if (!isAABBInFrustum(minX, secMinY, minZ, maxX, secMaxY, maxZ)) continue;
            if (occluded(chunkKey, sec.sectionY)) continue;

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
            float minX = renderData.position.x;
            float maxX = minX + 16.0f;
            float minZ = renderData.position.z;
            float maxZ = minZ + 16.0f;

            for (auto& sec : renderData.sections) {
                if (sec.overlayIndexCount == 0) continue;
                float secMinY = (float)sec.sectionY;
                float secMaxY = secMinY + 16.0f;
                if (!isAABBInFrustum(minX, secMinY, minZ, maxX, secMaxY, maxZ)) continue;
                if (occluded(chunkKey, sec.sectionY)) continue;

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
            float minX = renderData.position.x;
            float maxX = minX + 16.0f;
            float minZ = renderData.position.z;
            float maxZ = minZ + 16.0f;

            for (auto& sec : renderData.sections) {
                if (sec.waterIndexCount == 0) continue;
                float secMinY = (float)sec.sectionY;
                float secMaxY = secMinY + 16.0f;
                if (!isAABBInFrustum(minX, secMinY, minZ, maxX, secMaxY, maxZ)) continue;
                if (occluded(chunkKey, sec.sectionY)) continue;

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

// ===== 天空渲染 =====

void GLRenderer::initSky() {
    if (skyInitialized) return;

    // 1. 天空纯色着色器（position + color uniform）
    const char* skyColorVsrc = R"(#version 300 es
precision mediump float;
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";
    const char* skyColorFsrc = R"(#version 300 es
precision mediump float;
out vec4 FragColor;
uniform vec4 uColor;
void main() {
    FragColor = uColor;
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
            LOGE("Sky shader compile error: %s", log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    };

    GLuint vs = compileShader(GL_VERTEX_SHADER, skyColorVsrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, skyColorFsrc);
    if (!vs || !fs) {
        LOGE("Sky shader compilation failed");
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return;
    }

    skyColorProgram = glCreateProgram();
    glAttachShader(skyColorProgram, vs);
    glAttachShader(skyColorProgram, fs);
    glLinkProgram(skyColorProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linkOk;
    glGetProgramiv(skyColorProgram, GL_LINK_STATUS, &linkOk);
    if (!linkOk) {
        char log[512];
        glGetProgramInfoLog(skyColorProgram, 512, nullptr, log);
        LOGE("Sky program link error: %s", log);
        glDeleteProgram(skyColorProgram);
        skyColorProgram = 0;
        return;
    }

    // 编译天空纹理着色器程序（太阳和月亮使用）
    const char* skyTextureVsrc = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoord;
out vec2 vTexCoord;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vTexCoord = aTexCoord;
}
)";

    const char* skyTextureFsrc = R"(#version 300 es
precision mediump float;
in vec2 vTexCoord;
out vec4 FragColor;
uniform vec4 uColor;
uniform sampler2D uTexture;
void main() {
    FragColor = uColor * texture(uTexture, vTexCoord);
}
)";

    GLuint skyVs = compileShader(GL_VERTEX_SHADER, skyTextureVsrc);
    GLuint skyFs = compileShader(GL_FRAGMENT_SHADER, skyTextureFsrc);
    if (skyVs && skyFs) {
        skyCelestialProgram = glCreateProgram();
        glAttachShader(skyCelestialProgram, skyVs);
        glAttachShader(skyCelestialProgram, skyFs);
        glLinkProgram(skyCelestialProgram);
        glDeleteShader(skyVs);
        glDeleteShader(skyFs);

        GLint linkOk;
        glGetProgramiv(skyCelestialProgram, GL_LINK_STATUS, &linkOk);
        if (!linkOk) {
            char log[512];
            glGetProgramInfoLog(skyCelestialProgram, 512, nullptr, log);
            LOGE("Sky celestial program link error: %s", log);
            glDeleteProgram(skyCelestialProgram);
            skyCelestialProgram = 0;
        } else {
            LOGI("Sky celestial program created successfully");
        }
    }

    // 2. 创建天空圆盘 VAO/VBO
    // 上半圆盘
    const auto& topVerts = SkyRenderer::getTopSkyVertices();
    glGenVertexArrays(1, &skyTopVAO);
    glGenBuffers(1, &skyTopVBO);
    glBindVertexArray(skyTopVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyTopVBO);
    glBufferData(GL_ARRAY_BUFFER, topVerts.size() * sizeof(float), topVerts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    // 下半圆盘
    const auto& bottomVerts = SkyRenderer::getBottomSkyVertices();
    glGenVertexArrays(1, &skyBottomVAO);
    glGenBuffers(1, &skyBottomVBO);
    glBindVertexArray(skyBottomVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyBottomVBO);
    glBufferData(GL_ARRAY_BUFFER, bottomVerts.size() * sizeof(float), bottomVerts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

// 3. 太阳 VAO/VBO/EBO
    const auto& sunVerts = SkyRenderer::getSunVertices();
    const auto& sunIdx = SkyRenderer::getSunIndices();
    glGenVertexArrays(1, &skySunVAO);
    glGenBuffers(1, &skySunVBO);
    glGenBuffers(1, &skySunEBO);
    glBindVertexArray(skySunVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skySunVBO);
    std::vector<float> sunFloats;
    for (const auto& vert : sunVerts) {
        sunFloats.push_back(vert.x);
        sunFloats.push_back(vert.y);
        sunFloats.push_back(vert.z);
        sunFloats.push_back(vert.u);
        sunFloats.push_back(vert.v);
    }
    glBufferData(GL_ARRAY_BUFFER, sunFloats.size() * sizeof(float), sunFloats.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skySunEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sunIdx.size() * sizeof(uint16_t), sunIdx.data(), GL_STATIC_DRAW);
// location 0: position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
// location 1: texture coordinate (UV)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    // 4. 月亮 VAO/VBO/EBO
    const auto& moonVerts = SkyRenderer::getMoonVertices();
    const auto& moonIdx = SkyRenderer::getMoonIndices();
    glGenVertexArrays(1, &skyMoonVAO);
    glGenBuffers(1, &skyMoonVBO);
    glGenBuffers(1, &skyMoonEBO);

    glBindVertexArray(skyMoonVAO);  // 绑定 VAO

    glBindBuffer(GL_ARRAY_BUFFER, skyMoonVBO);
    std::vector<float> moonFloats;
    for (const auto& vert : moonVerts) {
        moonFloats.push_back(vert.x);
        moonFloats.push_back(vert.y);
        moonFloats.push_back(vert.z);
        moonFloats.push_back(vert.u);
        moonFloats.push_back(vert.v);
    }
    glBufferData(GL_ARRAY_BUFFER, moonFloats.size() * sizeof(float), moonFloats.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skyMoonEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, moonIdx.size() * sizeof(uint16_t), moonIdx.data(), GL_STATIC_DRAW);

// 设置顶点属性
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);   // UV 坐标
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);  // 最后解绑
    // 5. 星星 VAO/VBO/EBO（POSITION only，与太阳/月亮共用 skyColorProgram）
    int starCount = 0;
    const auto& starVerts = SkyRenderer::getStarVertices(starCount);
    m_starCount = starCount;  // 保存数量供后续绘制使用
    const auto& starIdx = SkyRenderer::getStarIndices(starCount);

    glGenVertexArrays(1, &skyStarsVAO);
    glGenBuffers(1, &skyStarsVBO);
    glGenBuffers(1, &skyStarsEBO);
    glBindVertexArray(skyStarsVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyStarsVBO);
    glBufferData(GL_ARRAY_BUFFER, starVerts.size() * sizeof(float), starVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skyStarsEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, starIdx.size() * sizeof(uint16_t), starIdx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);


    // ===== 重要：在这里加载太阳和月亮纹理（只加载一次） =====
    if (sunTextureID == 0) {
        sunTextureID = loadSunTexture();
        LOGI("Sun texture loaded: %d", sunTextureID);
    }
    for (int i = 0; i < 8; i++) {
        if (moonTextureIDs[i] == 0) {
            moonTextureIDs[i] = loadMoonTexture(i);
            if (moonTextureIDs[i] == 0) {
                LOGW("Failed to load moon phase %d, using phase 0 as fallback", i);
                moonTextureIDs[i] = moonTextureIDs[0];  // 用新月作为备用
            }
        }
    }

    skyInitialized = true;
    LOGI("Sky renderer initialized (program=%d, topVAO=%d, sunVAO=%d, moonVAO=%d, starsVAO=%d)",
         skyColorProgram, skyTopVAO, skySunVAO, skyMoonVAO, skyStarsVAO);
}
GLuint GLRenderer::loadSunTexture() {
    TextureData texData = TextureLoader::loadImage("environment/celestial/sun.png");
    if (!texData.data) {
        LOGE("Failed to load sun texture");
        return 0;
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texData.width, texData.height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, texData.data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

GLuint GLRenderer::loadMoonTexture(int phase) {
    const char* path = SkyRenderer::getMoonPhasePath(phase);
    if (!path) {
        LOGE("Invalid moon phase: %d", phase);
        return 0;
    }

    TextureData texData = TextureLoader::loadImage(path);
    if (!texData.data) {
        LOGE("Failed to load moon texture: %s", path);
        return 0;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texData.width, texData.height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, texData.data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    LOGI("Loaded moon phase %d: %s", phase, path);
    return tex;
}
void GLRenderer::renderSky(const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                            float skyR, float skyG, float skyB, float timeOfDay, float starBrightness, int moonPhase) {
    if (!skyInitialized || skyColorProgram == 0) return;

    // 使用 SkyRenderer 计算天空颜色和透明度
    glm::vec3 skyColor = SkyRenderer::getSkyColor(timeOfDay);
    float sunAlpha = SkyRenderer::getCelestialAlpha(timeOfDay, false);
    float moonAlpha = SkyRenderer::getCelestialAlpha(timeOfDay, true);

    // 保存 GL 状态
    glDisable(GL_DEPTH_TEST);  // 完全禁用深度测试，避免遮挡
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    glUseProgram(skyColorProgram);
    GLint uMVP = glGetUniformLocation(skyColorProgram, "uMVP");
    GLint uColor = glGetUniformLocation(skyColorProgram, "uColor");

    // 天空渲染使用只有旋转的 view matrix（无平移，天体跟随相机）
    glm::mat4 skyView = glm::mat4(glm::mat3(viewMatrix));

    // 天空用独立投影矩阵：远裁剪面固定 1024，不受渲染距离影响
    float aspect = (float)screenWidth / screenHeight;
    glm::mat4 skyProj = Camera::computeProjectionMatrix(fov, aspect, nearPlane, 1024.0f);
/*
    // 1. 渲染天空圆盘
    glm::mat4 skyMVP = skyProj * skyView;
    glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(skyMVP));
    glUniform4f(uColor, skyColor.r, skyColor.g, skyColor.b, 1.0f);
    glBindVertexArray(skyTopVAO);
*/
    // 使用新的接口获取顶点数据
    const auto& topSkyVerts = SkyRenderer::getTopSkyVertices();
    glDrawArrays(GL_TRIANGLE_FAN, 0, (GLsizei)(topSkyVerts.size() / 3));

    // 2. 天体旋转矩阵
    glm::mat4 celestialRot = SkyRenderer::getCelestialRotation(timeOfDay);
    glm::mat4 celestialMVP = skyProj * skyView * celestialRot;
    glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(celestialMVP));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    // 3. 渲染太阳（使用纹理）
    if (sunAlpha > 0.01f && skyCelestialProgram != 0 && sunTextureID != 0) {
        // 切换到纹理着色器
        glUseProgram(skyCelestialProgram);
        GLint uCelestialMVP = glGetUniformLocation(skyCelestialProgram, "uMVP");
        GLint uCelestialColor = glGetUniformLocation(skyCelestialProgram, "uColor");
        GLint uTexture = glGetUniformLocation(skyCelestialProgram, "uTexture");

        // 设置太阳矩阵和颜色
        glUniformMatrix4fv(uCelestialMVP, 1, GL_FALSE, glm::value_ptr(celestialMVP));
        glUniform4f(uCelestialColor, 1.0f, 1.0f, 1.0f, sunAlpha); // 白色 * alpha

        // 绑定太阳纹理（使用缓存的纹理ID）
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sunTextureID);
        glUniform1i(uTexture, 0);

        // 绘制太阳
        glBindVertexArray(skySunVAO);
        const auto& sunIndices = SkyRenderer::getSunIndices();
        glDrawElements(GL_TRIANGLES, (GLsizei)sunIndices.size(), GL_UNSIGNED_SHORT, 0);

        // 切换回纯色着色器
        glUseProgram(skyColorProgram);
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(celestialMVP));
    }

// 4. 渲染月亮（使用纹理）
    if (moonAlpha > 0.01f && skyCelestialProgram != 0) {
        GLuint currentMoonTex = moonTextureIDs[moonPhase];  // 直接使用传入的 moonPhase
        if (currentMoonTex == 0) currentMoonTex = moonTextureIDs[0];

        // 切换到纹理着色器
        glUseProgram(skyCelestialProgram);
        GLint uCelestialMVP = glGetUniformLocation(skyCelestialProgram, "uMVP");
        GLint uCelestialColor = glGetUniformLocation(skyCelestialProgram, "uColor");
        GLint uTexture = glGetUniformLocation(skyCelestialProgram, "uTexture");

        // 设置月亮矩阵和颜色
        glUniformMatrix4fv(uCelestialMVP, 1, GL_FALSE, glm::value_ptr(celestialMVP));
        glUniform4f(uCelestialColor, 1.0f, 1.0f, 1.0f, moonAlpha);

        // 绑定当前月相纹理
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentMoonTex);
        glUniform1i(uTexture, 0);

        // 绘制月亮
        glBindVertexArray(skyMoonVAO);
        const auto& moonIndices = SkyRenderer::getMoonIndices();
        glDrawElements(GL_TRIANGLES, (GLsizei)moonIndices.size(), GL_UNSIGNED_SHORT, 0);

        // 切换回纯色着色器
        glUseProgram(skyColorProgram);
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(celestialMVP));
    }

    // 5. 渲染星星
    if (starBrightness > 0.01f) {
        float actualStarBrightness = starBrightness * skyColor.r;
        glUniform4f(uColor, actualStarBrightness, actualStarBrightness,
                    actualStarBrightness, actualStarBrightness);
        glBindVertexArray(skyStarsVAO);

        // 使用保存的 m_starCount 生成索引（或直接复用已上传的 EBO 数据）
        const auto& starIndices = SkyRenderer::getStarIndices(m_starCount);
        glDrawElements(GL_TRIANGLES, (GLsizei)starIndices.size(), GL_UNSIGNED_SHORT, 0);
    }
    glBindVertexArray(0);
    glUseProgram(0);

    // 恢复 GL 状态
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
}


void GLRenderer::cleanup() {
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

    // 清理天空渲染资源
    if (skyColorProgram != 0) { glDeleteProgram(skyColorProgram); skyColorProgram = 0; }
    if (skyTopVAO != 0) { glDeleteVertexArrays(1, &skyTopVAO); skyTopVAO = 0; }
    if (skyTopVBO != 0) { glDeleteBuffers(1, &skyTopVBO); skyTopVBO = 0; }
    if (skyBottomVAO != 0) { glDeleteVertexArrays(1, &skyBottomVAO); skyBottomVAO = 0; }
    if (skyBottomVBO != 0) { glDeleteBuffers(1, &skyBottomVBO); skyBottomVBO = 0; }
    if (skySunVAO != 0) { glDeleteVertexArrays(1, &skySunVAO); skySunVAO = 0; }
    if (skySunVBO != 0) { glDeleteBuffers(1, &skySunVBO); skySunVBO = 0; }
    if (skySunEBO != 0) { glDeleteBuffers(1, &skySunEBO); skySunEBO = 0; }
    if (skyMoonVAO != 0) { glDeleteVertexArrays(1, &skyMoonVAO); skyMoonVAO = 0; }
    if (skyMoonVBO != 0) { glDeleteBuffers(1, &skyMoonVBO); skyMoonVBO = 0; }
    if (skyMoonEBO != 0) { glDeleteBuffers(1, &skyMoonEBO); skyMoonEBO = 0; }
    if (skyStarsVAO != 0) { glDeleteVertexArrays(1, &skyStarsVAO); skyStarsVAO = 0; }
    if (skyStarsVBO != 0) { glDeleteBuffers(1, &skyStarsVBO); skyStarsVBO = 0; }
    if (skyStarsEBO != 0) { glDeleteBuffers(1, &skyStarsEBO); skyStarsEBO = 0; }

    // 清理太阳月亮纹理
    if (sunTextureID != 0) { glDeleteTextures(1, &sunTextureID); sunTextureID = 0; }
    for (int i = 0; i < 8; i++) {
        if (moonTextureIDs[i] != 0) {
            glDeleteTextures(1, &moonTextureIDs[i]);
            moonTextureIDs[i] = 0;
        }
    }

    skyInitialized = false;

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
