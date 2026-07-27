#include "BlockIconRasterizer.h"
#include "TextureAtlas.h"
#include "TextureLoader.h"
#include <algorithm>
#include <unordered_map>
#include <android/log.h>

#define LOG_TAG "BlockIconRasterizer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ============================================================
// 方块模型 → 3D 图标像素光栅化（CPU生成等距立方体，不依赖FBO）
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

bool BlockIconRasterizer::rasterize(const TextureAtlas* atlas, const std::string& modelName,
                                    int iconSize, std::vector<uint8_t>& outPixels) {
    const auto* modelObj = atlas->getBlockModel(modelName);
    if (!modelObj || modelObj->elements.empty()) {
        return false;
    }

    outPixels.assign(iconSize * iconSize * 4, 0);

    // 加载所有用到的纹理像素（总是检查是否有真正的透明像素）
    struct TexCacheEntry {
        TextureData td;  // 拥有数据所有权
        bool hasAlpha;
    };
    std::unordered_map<int, TexCacheEntry> texCache;
    auto loadTex = [&](int layer) -> bool {
        if (texCache.find(layer) != texCache.end()) return true;
        std::string fname = atlas->getTextureFileName(layer);
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
        rasterQuad(outPixels.data(), iconSize,
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

    LOGI("Rasterized CPU icon for '%s' (%d elements)", modelName.c_str(), (int)modelObj->elements.size());
    return true;
}
