#include "GLRenderer.h"
#include <android/log.h>
#include <cmath>
#include <algorithm>
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
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
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
        LOGI("All %d textures uploaded", textureTotalCount);
        textureInitPending = false;
        LOGI("=== finishTextureInit COMPLETE ===");
        preRenderBlockIcons();
    }

    return true;
}

// ============================================================
// 方块模型 → 3D 图标纹理渲染（物品栏用）
// ============================================================

// ============================================================
// 方块模型 → 3D 图标纹理渲染（CPU生成等距立方体，不依赖FBO）
// ============================================================

// CPU 端采样纹理像素
static uint32_t sampleTex(const uint8_t* data, int w, int h, float u, float v) {
    int px = (int)(u * w);
    int py = (int)(v * h);
    if (px < 0) px = 0; if (px >= w) px = w - 1;
    if (py < 0) py = 0; if (py >= h) py = h - 1;
    int idx = (py * w + px) * 4;
    return ((uint32_t)data[idx+3] << 24) | ((uint32_t)data[idx+0] << 16) | ((uint32_t)data[idx+1] << 8) | (uint32_t)data[idx+2];
}

// 设置输出像素（Alpha 预乘）
static void setPixel(uint8_t* out, int x, int y, int iconSize, uint32_t color) {
    if (x < 0 || x >= iconSize || y < 0 || y >= iconSize) return;
    uint8_t a = (color >> 24) & 0xFF;
    if (a == 0) return;
    int idx = (y * iconSize + x) * 4;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    // Alpha 混合（支持半透明覆盖层叠加）
    if (a == 255) {
        out[idx+0] = r;
        out[idx+1] = g;
        out[idx+2] = b;
        out[idx+3] = a;
    } else {
        // 预乘 Alpha 混合：dst = src*alpha + dst*(1-alpha)
        float fa = a / 255.0f;
        out[idx+0] = (uint8_t)(r * fa + out[idx+0] * (1.0f - fa));
        out[idx+1] = (uint8_t)(g * fa + out[idx+1] * (1.0f - fa));
        out[idx+2] = (uint8_t)(b * fa + out[idx+2] * (1.0f - fa));
        out[idx+3] = 255;
    }
}

// 等距投影：将方块局部坐标 (0~16) 映射到屏幕
static void isoProject(float x, float y, float z, int iconSize, float& sx, float& sy) {
    float scale = iconSize * 0.04f;  // 保持适中大小，配合偏移防止顶部裁剪
    sx = (z - x) * 0.7071f * scale;
    sy = ((x + z) * 0.3535f - y * 0.8660f) * scale;
    sx += iconSize * 0.5f;
    sy += iconSize * 0.5f;
    sy += 3.0f;  // 少下移1像素
}

// 光栅化一个四边形面（4个三维顶点→屏幕投影→纹理映射）
// tintColor: 0xFFFFFFFF 表示不染色；其他值按 RGB 相乘
static void rasterQuad(uint8_t* out, int iconSize,
    const float v0[3], const float v1[3], const float v2[3], const float v3[3],
    const uint8_t* texData, int texW, int texH,
    float uv0[2], float uv1[2], float uv2[2], float uv3[2],
    uint32_t tintColor = 0xFFFFFFFF) {

    // 投影到屏幕
    float s[4][2];
    isoProject(v0[0],v0[1],v0[2], iconSize, s[0][0],s[0][1]);
    isoProject(v1[0],v1[1],v1[2], iconSize, s[1][0],s[1][1]);
    isoProject(v2[0],v2[1],v2[2], iconSize, s[2][0],s[2][1]);
    isoProject(v3[0],v3[1],v3[2], iconSize, s[3][0],s[3][1]);

    // 包围盒
    int minX=iconSize,maxX=0,minY=iconSize,maxY=0;
    for(int i=0;i<4;i++){
        int ix=(int)s[i][0], iy=(int)s[i][1];
        if(ix<minX)minX=ix; if(ix>maxX)maxX=ix;
        if(iy<minY)minY=iy; if(iy>maxY)maxY=iy;
    }
    if(minX<0)minX=0; if(maxX>=iconSize)maxX=iconSize-1;
    if(minY<0)minY=0; if(maxY>=iconSize)maxY=iconSize-1;

    // 使用边缘函数判断点是否在四边形内
    auto edge = [](float ax,float ay,float bx,float by,float px,float py)->float{
        return (bx-ax)*(py-ay)-(by-ay)*(px-ax);
    };

    for(int py=minY;py<=maxY;py++){
        for(int px=minX;px<=maxX;px++){
            float fx=(float)px+0.5f, fy=(float)py+0.5f;
            // 分解为两个三角形 (0,1,2) 和 (0,2,3)
            float e0=edge(s[0][0],s[0][1],s[1][0],s[1][1],fx,fy);
            float e1=edge(s[1][0],s[1][1],s[2][0],s[2][1],fx,fy);
            float e2=edge(s[2][0],s[2][1],s[3][0],s[3][1],fx,fy);
            float e3=edge(s[3][0],s[3][1],s[0][0],s[0][1],fx,fy);

            bool inside = (e0>=0&&e1>=0&&e2>=0&&e3>=0)||(e0<=0&&e1<=0&&e2<=0&&e3<=0);
            if(!inside) continue;

            // 双线性插值 UV
            // 简化为四边形中的重心坐标
            float ax = s[0][0], ay = s[0][1];
            float bx = s[1][0], by = s[1][1];
            float cx = s[2][0], cy = s[2][1];
            float dx = s[3][0], dy = s[3][1];

            // 计算重心坐标（在三角形 0-1-2 中）
            float area = (bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
            float w0 = ((bx-fx)*(cy-fy)-(by-fy)*(cx-fx))/area;
            float w1 = ((fx-ax)*(cy-fy)-(fy-ay)*(cx-fx))/area;
            float w2 = ((bx-ax)*(fy-ay)-(by-ay)*(fx-ax))/area;

            float u,v;
            if(w0>=0&&w1>=0&&w2>=0) {
                u = w0*uv0[0] + w1*uv1[0] + w2*uv2[0];
                v = w0*uv0[1] + w1*uv1[1] + w2*uv2[1];
            } else {
                // 在三角形 0-2-3 中
                float area2 = (cx-ax)*(dy-ay)-(cy-ay)*(dx-ax);
                if(area2==0) continue;
                float ww0 = ((cx-fx)*(dy-fy)-(cy-fy)*(dx-fx))/area2;
                float ww1 = ((fx-ax)*(dy-fy)-(fy-ay)*(dx-fx))/area2;
                float ww2 = ((cx-ax)*(fy-ay)-(cy-ay)*(fx-ax))/area2;
                u = ww0*uv0[0] + ww1*uv2[0] + ww2*uv3[0];
                v = ww0*uv0[1] + ww1*uv2[1] + ww2*uv3[1];
            }

            uint32_t color = sampleTex(texData, texW, texH, u, v);
            // 应用染色
            if (tintColor != 0xFFFFFFFF) {
                uint8_t tr = (tintColor >> 16) & 0xFF;
                uint8_t tg = (tintColor >> 8) & 0xFF;
                uint8_t tb = tintColor & 0xFF;
                uint8_t cr = (color >> 16) & 0xFF;
                uint8_t cg = (color >> 8) & 0xFF;
                uint8_t cb = color & 0xFF;
                uint8_t ca = (color >> 24) & 0xFF;
                // 跳过暗像素（灰度覆盖层用深色表示透明，阈值64对应~25%亮度）
                if (cr < 64 && cg < 64 && cb < 64) continue;
                cr = (uint8_t)(((int)cr * tr) / 255);
                cg = (uint8_t)(((int)cg * tg) / 255);
                cb = (uint8_t)(((int)cb * tb) / 255);
                color = ((uint32_t)ca << 24) | ((uint32_t)cr << 16) | ((uint32_t)cg << 8) | cb;
            }
            setPixel(out, px, py, iconSize, color);
        }
    }
}

GLuint GLRenderer::renderBlockIcon(const std::string& modelName, int iconSize) {
    auto& atlas = TextureAtlas::getInstance();
    const auto* modelObj = atlas.getBlockModel(modelName);
    if (!modelObj || modelObj->elements.empty()) {
        return 0;
    }

    std::vector<uint8_t> pixels(iconSize * iconSize * 4, 0);

    // 加载所有用到的纹理像素（总是检查是否有真正的透明像素）
    struct TexCacheEntry {
        TextureData td;  // 拥有数据所有权
        bool hasAlpha;
    };
    std::unordered_map<int, TexCacheEntry> texCache;
    auto loadTex = [&](int layer) -> bool {
        if (texCache.find(layer) != texCache.end()) return true;
        std::string fname = atlas.getTextureFileName(layer);
        TextureData td = TextureLoader::loadImage(fname);
        if (!td.data) return false;
        // 检查纹理是否有透明像素
        bool hasAlpha = false;
        int total = td.width * td.height;
        for (int i = 0; i < total; i++) {
            if (td.data[i * 4 + 3] < 255) { hasAlpha = true; break; }
        }
        texCache[layer] = {std::move(td), hasAlpha};
        return true;
    };

    // 收集全部纹理（统一检查 alpha）
    for (const auto& elem : modelObj->elements) {
        for (int f = 0; f < 6; f++) {
            if (!elem.hasFaces[f]) continue;
            int layer = elem.faces[f].textureLayer;
            if (layer >= 0) loadTex(layer);
        }
    }

    // 渲染辅助 lambda：构建4个顶点并 raster
    auto renderFace = [&](const ModelElementData& elem, int face, const TexCacheEntry& entry,
                          uint32_t tintColor, int pass) {
        const auto& fd = elem.faces[face];
        const auto& texData = entry.td;
        float fx=elem.from[0], fy=elem.from[1], fz=elem.from[2];
        float tx=elem.to[0], ty=elem.to[1], tz=elem.to[2];

        float v[4][3];
        switch(face){
            case 0: v[0][0]=fx;v[0][1]=fy;v[0][2]=fz; v[1][0]=tx;v[1][1]=fy;v[1][2]=fz; v[2][0]=tx;v[2][1]=fy;v[2][2]=tz; v[3][0]=fx;v[3][1]=fy;v[3][2]=tz; break;
            case 1: v[0][0]=fx;v[0][1]=ty;v[0][2]=fz; v[1][0]=tx;v[1][1]=ty;v[1][2]=fz; v[2][0]=tx;v[2][1]=ty;v[2][2]=tz; v[3][0]=fx;v[3][1]=ty;v[3][2]=tz; break;
            case 2: v[0][0]=fx;v[0][1]=fy;v[0][2]=fz; v[1][0]=tx;v[1][1]=fy;v[1][2]=fz; v[2][0]=tx;v[2][1]=ty;v[2][2]=fz; v[3][0]=fx;v[3][1]=ty;v[3][2]=fz; break;
            case 3: v[0][0]=fx;v[0][1]=fy;v[0][2]=tz; v[1][0]=tx;v[1][1]=fy;v[1][2]=tz; v[2][0]=tx;v[2][1]=ty;v[2][2]=tz; v[3][0]=fx;v[3][1]=ty;v[3][2]=tz; break;
            case 4: v[0][0]=fx;v[0][1]=fy;v[0][2]=fz; v[1][0]=fx;v[1][1]=fy;v[1][2]=tz; v[2][0]=fx;v[2][1]=ty;v[2][2]=tz; v[3][0]=fx;v[3][1]=ty;v[3][2]=fz; break;
            case 5: v[0][0]=tx;v[0][1]=fy;v[0][2]=fz; v[1][0]=tx;v[1][1]=fy;v[1][2]=tz; v[2][0]=tx;v[2][1]=ty;v[2][2]=tz; v[3][0]=tx;v[3][1]=ty;v[3][2]=fz; break;
        }

        float u1=fd.uv[0]/16.0f, uv1=fd.uv[3]/16.0f, u2=fd.uv[2]/16.0f, uv2=fd.uv[1]/16.0f;
        float uvs[4][2] = {{u1,uv1},{u2,uv1},{u2,uv2},{u1,uv2}};
        rasterQuad(pixels.data(), iconSize,
            v[0],v[1],v[2],v[3],
            texData.data, texData.width, texData.height,
            uvs[0],uvs[1],uvs[2],uvs[3],
            tintColor);
    };

    // 深度排序（等距视角）
    struct FaceSort {
        int elemIdx; int faceIdx; float depth;
    };
    std::vector<FaceSort> faceOrder;
    for (int ei = 0; ei < (int)modelObj->elements.size(); ei++) {
        const auto& elem = modelObj->elements[ei];
        for (int f = 0; f < 6; f++) {
            if (!elem.hasFaces[f]) continue;
            if (elem.faces[f].textureLayer < 0) continue;
            float fx=elem.from[0], fy=elem.from[1], fz=elem.from[2];
            float tx=elem.to[0], ty=elem.to[1], tz=elem.to[2];
            float cx,cy,cz;
            switch(f){
                case 0: cx=(fx+tx)*0.5f; cy=fy; cz=(fz+tz)*0.5f; break;
                case 1: cx=(fx+tx)*0.5f; cy=ty; cz=(fz+tz)*0.5f; break;
                case 2: cx=(fx+tx)*0.5f; cy=(fy+ty)*0.5f; cz=fz; break;
                case 3: cx=(fx+tx)*0.5f; cy=(fy+ty)*0.5f; cz=tz; break;
                case 4: cx=fx; cy=(fy+ty)*0.5f; cz=(fz+tz)*0.5f; break;
                case 5: cx=tx; cy=(fy+ty)*0.5f; cz=(fz+tz)*0.5f; break;
            }
            float dx = cx - 16.0f, dy = cy - 14.0f, dz = cz - 16.0f;
            float depth = dx*dx + dy*dy + dz*dz;
            // 背面剔除：法线与视线方向(8,6,8)点积 <= 0 的面为背面
            // 视线方向 = 方块中心(8,8,8) → 相机(16,14,16)
            static const float FN[6][3] = {{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
            float viewDot = FN[f][0]*8.0f + FN[f][1]*6.0f + FN[f][2]*8.0f;
            if (viewDot <= 0) continue;
            faceOrder.push_back({ei, f, depth});
        }
    }
    std::sort(faceOrder.begin(), faceOrder.end(), [](const FaceSort& a, const FaceSort& b) {
        return a.depth > b.depth || (a.depth == b.depth && a.elemIdx < b.elemIdx);
    });

    // ===== 两阶段渲染（MC 官方算法）=====
    // 阶段 1：所有无 tintindex 的基层面
    for (const auto& fo : faceOrder) {
        const auto& elem = modelObj->elements[fo.elemIdx];
        int face = fo.faceIdx;
        const auto& fd = elem.faces[face];
        if (fd.tintindex >= 0) continue;
        if (fd.textureLayer < 0) continue;
        auto it = texCache.find(fd.textureLayer);
        if (it == texCache.end()) continue;
        renderFace(elem, face, it->second, 0xFFFFFFFF, 1);
    }

    // 阶段 2：所有有 tintindex 的覆盖层面（半透明/灰度纹理叠加）
    for (const auto& fo : faceOrder) {
        const auto& elem = modelObj->elements[fo.elemIdx];
        int face = fo.faceIdx;
        const auto& fd = elem.faces[face];
        if (fd.tintindex < 0) continue;
        if (fd.textureLayer < 0) continue;
        auto it = texCache.find(fd.textureLayer);
        if (it == texCache.end()) continue;

        uint32_t tintColor = 0xFF7FA752;  // 默认草绿
        if (!it->second.hasAlpha) {
            // 无 alpha 通道的纹理用暗像素跳过（stb_image 将 RGB/灰阶 PNG 转成 alpha=255）
            // 需要特殊处理：不传 tintColor，改用 rasterQuad 默认不染色
            // 并在渲染后用 green 覆盖
            // 方案：仍然用 tint 渲染，rasterQuad 中的暗色跳过逻辑会处理
            renderFace(elem, face, it->second, tintColor, 2);
        } else {
            // 有真 alpha：直接染色渲染，setPixel 处理 α==0
            renderFace(elem, face, it->second, tintColor, 2);
        }
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

    LOGI("Generated CPU icon for '%s' (%d elements)", modelName.c_str(), (int)modelObj->elements.size());
    return tex;
}

void GLRenderer::preRenderBlockIcons() {
    LOGI("Pre-rendering block icons for inventory...");
    auto& atlas = TextureAtlas::getInstance();
    int rendered = 0;
    // 遍历所有已加载的 block 模型，检查是否有对应的 item 引用
    for (const auto& [itemName, parentModel] : atlas.getItemModelCache()) {
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

    // ===== 创建默认光照贴图（白色 16x16 = 全亮）=====
    {
        glGenTextures(1, &lightmapTextureID);
        glBindTexture(GL_TEXTURE_2D, lightmapTextureID);
        uint8_t whitePixels[16 * 16 * 4];
        memset(whitePixels, 255, sizeof(whitePixels));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        LOGI("Created default lightmap texture -> GL%d", lightmapTextureID);
    }

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
                // location 0: Position
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
                glEnableVertexAttribArray(0);
                // location 1: UV0
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
                glEnableVertexAttribArray(1);
                // location 2: TexIndex
                glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(5 * sizeof(float)));
                glEnableVertexAttribArray(2);
                // location 3: Color
                glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)(offsetof(Vertex, color)));
                glEnableVertexAttribArray(3);
                // location 4: Normal
                glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, normal)));
                glEnableVertexAttribArray(4);
                // location 5: UV2
                glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, uv2)));
                glEnableVertexAttribArray(5);
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

    // ===== Phase 1: 使用 rendertype_cutout 渲染基体 + 覆盖层 =====
    // cutout 着色器会丢弃 alpha < 0.1 的像素，对不透明块（alpha=1.0）无害
    if (shaderCutout.program == 0) {
        renderUI();
        eglSwapBuffers(display, surface);
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
        glUniform4f(shaderCutout.uFogColor, 0.53f, 0.81f, 0.92f, 1.0f);

    // 绑定纹理数组到 Sampler0
    glActiveTexture(GL_TEXTURE0);
    if (textureArrayID != 0) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);
        // 一次性记录纹理数组层数供诊断
        if (frameCount <= 5) {
            LOGI("Texture array: %d layers, %dx%d", textureTotalCount, textureWidth, textureHeight);
        }
    } else {
        LOGE("Texture array not initialized! No texture bound - all blocks will be black");
    }
    if (shaderCutout.uSampler0 != -1) glUniform1i(shaderCutout.uSampler0, 0);

    // 绑定光照贴图到 Sampler2（Mojang 光照贴图约定在 sampler2D Sampler2）
    // 即使 uSampler2==-1 也尝试查询和设置，防止编译器优化导致无绑定
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, lightmapTextureID);
    {
        GLint s2loc = shaderCutout.uSampler2;
        if (s2loc == -1) {
            s2loc = glGetUniformLocation(shaderCutout.program, "Sampler2");
        }
        if (s2loc != -1) glUniform1i(s2loc, 2);
        else LOGW("Sampler2 uniform not active (lightmap disabled)");
    }

    // ===== 合批渲染所有可见区块 =====
    int chunksRendered = 0;
    int totalTriangles = 0;

    const auto& dim = VersionManager::getInstance().getDimensionConfig();
    float worldMinY = (float)dim.minY;
    float worldMaxY = (float)dim.maxY;

    // 启用透明混合（覆盖层需要 alpha blend）
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 加锁保护 chunkRenderCache
    std::lock_guard<std::mutex> renderLock(cacheMutex);
    for (auto& [chunkKey, renderData] : chunkRenderCache) {

        if (!renderData.visible || renderData.indexCount == 0) {
            continue;
        }

        // DIAG: 打印附近 chunk 的信息
        static int chunkDiagPrinted = 0;
        if (chunkDiagPrinted < 5) {
            int cx = (int)(renderData.position.x / 16.0f);
            int cz = (int)(renderData.position.z / 16.0f);
            // 从 chunkKey 解码 sectionY
            // key 编码: ((int64_t)chunkX << 24) | ((int64_t)sectionY << 12) | (chunkZ & 0xFFF)
            int64_t k = (long long)chunkKey;
            int sectionY = (int)((k >> 12) & 0xFFF);
            float dist2 = (renderData.position.x - lastCameraX)*(renderData.position.x - lastCameraX)
                         + (renderData.position.z - lastCameraZ)*(renderData.position.z - lastCameraZ);
            LOGI("CHUNK_DIAG: key=%lld pos=(%.0f,%.0f) chunk=(%d,%d) secY=%d dist=%.0f idx=%u overlay=%u water=%u",
                 (long long)chunkKey, renderData.position.x, renderData.position.z,
                 cx, cz, sectionY, sqrtf(dist2),
                 renderData.indexCount, renderData.overlayIndexCount, renderData.waterIndexCount);
            chunkDiagPrinted++;
        }

        // 距离剔除
        float chunkCenterX = renderData.position.x + 8.0f;
        float chunkCenterZ = renderData.position.z + 8.0f;
        float dx = chunkCenterX - lastCameraX;
        float dz = chunkCenterZ - lastCameraZ;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist > farPlane) continue;

        // 视锥体裁剪
        float minX = renderData.position.x;
        float maxX = minX + 16.0f;
        float minZ = renderData.position.z;
        float maxZ = minZ + 16.0f;
        if (!isAABBInFrustum(minX, worldMinY, minZ, maxX, worldMaxY, maxZ)) continue;

        glBindVertexArray(renderData.vao);
        { GLenum e; while((e=glGetError())!=GL_NO_ERROR) LOGE("GL_ERR_VAO 0x%x frame=%u key=%lld", e, frameCount, (long long)chunkKey); }

        uint32_t baseEnd = renderData.indexCount - renderData.overlayIndexCount - renderData.waterIndexCount;

        // 基体几何（LESS 深度测试，写深度）
        glDrawElements(GL_TRIANGLES, baseEnd, GL_UNSIGNED_INT, 0);
        { GLenum e; while((e=glGetError())!=GL_NO_ERROR) LOGE("GL_ERR_BASE 0x%x frame=%u baseEnd=%u", e, frameCount, baseEnd); }

        // 草覆盖层（LEQUAL 深度测试，不写深度，避免 z-fighting）
        if (renderData.overlayIndexCount > 0) {
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            glDrawElements(GL_TRIANGLES, renderData.overlayIndexCount, GL_UNSIGNED_INT,
                           (const GLvoid*)(uintptr_t)(baseEnd * sizeof(uint32_t)));
            { GLenum e; while((e=glGetError())!=GL_NO_ERROR) LOGE("GL_ERR_OVER 0x%x frame=%u", e, frameCount); }
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
        }

        chunksRendered++;
        totalTriangles += renderData.indexCount / 3;
    }

    // ===== Phase 2: 使用 rendertype_translucent 渲染水 =====
    if (shaderTranslucent.program != 0) {
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
                    glUniform4f(shaderTranslucent.uFogColor, 0.53f, 0.81f, 0.92f, 1.0f);
                if (shaderTranslucent.uSampler0 != -1) glUniform1i(shaderTranslucent.uSampler0, 0);
                if (shaderTranslucent.uSampler2 != -1) glUniform1i(shaderTranslucent.uSampler2, 2);

                // 水渲染状态
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_FALSE);
                waterStateSet = true;
            }

            glBindVertexArray(renderData.vao);
            uint32_t baseEnd = renderData.indexCount - renderData.overlayIndexCount - renderData.waterIndexCount;
            uint32_t grassEnd = baseEnd + renderData.overlayIndexCount;
            glDrawElements(GL_TRIANGLES, renderData.waterIndexCount, GL_UNSIGNED_INT,
                           (const GLvoid*)(uintptr_t)(grassEnd * sizeof(uint32_t)));
        }

        if (waterStateSet) {
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glDisable(GL_BLEND);
        }
        { GLenum e; while((e=glGetError())!=GL_NO_ERROR) LOGE("GL_ERR_WATER 0x%x frame=%u", e, frameCount); }
    }

    glBindVertexArray(0);
    { GLenum e; while((e=glGetError())!=GL_NO_ERROR) LOGE("GL_ERR_UNBIND 0x%x frame=%u", e, frameCount); }

    // 每 60 帧检查 OpenGL 错误
    if (frameCount % 60 == 0) {
        GLenum err;
        while ((err = glGetError()) != GL_NO_ERROR) {
            LOGE("GL_ERROR 0x%x after chunk rendering frame %u", err, frameCount);
        }
    }

    // 每 60 帧打印统计信息
    if (frameCount % 60 == 0) {
        LOGI("Frame %u: Rendered %d chunks, %d triangles",
             frameCount, chunksRendered, totalTriangles);
    }

    // 游戏内 UI 叠加
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

    // 清理方块图标缓存
    for (auto& [name, tex] : blockIconCache) {
        glDeleteTextures(1, &tex);
    }
    blockIconCache.clear();

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
