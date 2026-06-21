#include "EntityRenderer.h"
#include "TextureLoader.h"
#include <android/log.h>
#include <cmath>

#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>

#include "3rdparty/miniz/miniz.h"

#define LOG_TAG "EntityRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

EntityRenderer& EntityRenderer::getInstance() {
    static EntityRenderer instance;
    return instance;
}

// ===== 着色器 =====
static const char* entityVertSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char* entityFragSrc = R"(#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uTexture;
uniform bool uHasTexture;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    if (uHasTexture) {
        vec4 c = texture(uTexture, vUV);
        if (c.a < 0.1) discard;
        fragColor = c;
    } else {
        fragColor = uColor;
    }
}
)";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("Entity shader compile error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool EntityRenderer::init() {
    if (initialized) return true;

    GLuint vs = compileShader(GL_VERTEX_SHADER, entityVertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, entityFragSrc);
    if (!vs || !fs) return false;

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vs);
    glAttachShader(shaderProgram, fs);
    glLinkProgram(shaderProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("Entity shader link failed");
        return false;
    }

    uMVP = glGetUniformLocation(shaderProgram, "uMVP");
    uTexture = glGetUniformLocation(shaderProgram, "uTexture");
    uHasTexture = glGetUniformLocation(shaderProgram, "uHasTexture");
    uColor = glGetUniformLocation(shaderProgram, "uColor");

    // ===== 构建人形模型几何（MC 经典 64x64 纹理布局）=====
    const float TW = 64.0f, TH = 64.0f;
    humanoidVerts.clear();

    // 各部件相对于脚底（实体位置）的偏移（MC 像素单位，1px = 1/16 block）
    // Head: 8x8x8, center at (0, 24+4, 0) → offset from feet: y = 28 pixels above feet
    buildBox(humanoidVerts, -4, 24, -4, 8, 8, 8, 0, 0, TW, TH);
    // Body: 8x12x4, offset y=12
    buildBox(humanoidVerts, -4, 12, -2, 8, 12, 4, 16, 16, TW, TH);
    // Right Arm: 4x12x4, offset x=-6, y=12
    buildBox(humanoidVerts, -8, 12, -2, 4, 12, 4, 40, 16, TW, TH);
    // Left Arm: 4x12x4, offset x=4, y=12
    buildBox(humanoidVerts, 4, 12, -2, 4, 12, 4, 32, 48, TW, TH);
    // Right Leg: 4x12x4, offset x=-4, y=0
    buildBox(humanoidVerts, -4, 0, -2, 4, 12, 4, 0, 16, TW, TH);
    // Left Leg: 4x12x4, offset x=0, y=0
    buildBox(humanoidVerts, 0, 0, -2, 4, 12, 4, 16, 48, TW, TH);

    // ===== 构建四足动物模型（猪/牛/羊 - 简化版 64x32 纹理）=====
    quadrupedVerts.clear();
    const float QW = 64.0f, QH = 32.0f;
    // Head: 8x8x8, front of body
    buildBox(quadrupedVerts, -4, 8, -14, 8, 8, 8, 0, 0, QW, QH);
    // Body: 12x8x8
    buildBox(quadrupedVerts, -6, 4, -4, 12, 8, 8, 28, 0, QW, QH);
    // 4 Legs: 4x6x4
    buildBox(quadrupedVerts, -6, 0, -2, 4, 6, 4, 0, 16, QW, QH);
    buildBox(quadrupedVerts, 2, 0, -2, 4, 6, 4, 0, 16, QW, QH);
    buildBox(quadrupedVerts, -6, 0, 2, 4, 6, 4, 0, 16, QW, QH);
    buildBox(quadrupedVerts, 2, 0, 2, 4, 6, 4, 0, 16, QW, QH);

    // ===== 构建蜘蛛模型 =====
    spiderVerts.clear();
    // Body: 14x8x8
    buildBox(spiderVerts, -7, 4, -4, 14, 8, 8, 0, 0, QW, QH);
    // Head: 8x8x8
    buildBox(spiderVerts, -4, 4, -12, 8, 8, 8, 32, 0, QW, QH);

    // ===== 构建苦力怕模型（64x32 纹理）=====
    creeperVerts.clear();
    // Head: 8x8x8, Y=18 to 26
    buildBox(creeperVerts, -4, 18, -4, 8, 8, 8, 0, 0, QW, QH);
    // Body: 4x16x4, Y=6 to 22
    buildBox(creeperVerts, -2, 6, -2, 4, 16, 4, 16, 16, QW, QH);
    // 4 Legs: 4x6x4
    buildBox(creeperVerts, -6, 0, -2, 4, 6, 4, 0, 16, QW, QH);   // right hind
    buildBox(creeperVerts, 2, 0, -2, 4, 6, 4, 0, 16, QW, QH);    // left hind
    buildBox(creeperVerts, -6, 0, 2, 4, 6, 4, 0, 16, QW, QH);    // right front
    buildBox(creeperVerts, 2, 0, 2, 4, 6, 4, 0, 16, QW, QH);     // left front

    // ===== 构建史莱姆模型（64x32 纹理）=====
    slimeVerts.clear();
    // 外壳: 16x16x16
    buildBox(slimeVerts, -8, 0, -8, 16, 16, 16, 0, 0, QW, QH);
    // 内核: 8x8x8 (居中)
    buildBox(slimeVerts, -4, 4, -4, 8, 8, 8, 0, 16, QW, QH);

    // ===== 构建恶魂模型（64x32 纹理）=====
    ghastVerts.clear();
    // Body: 16x16x16, Y=8 to 24 (漂浮)
    buildBox(ghastVerts, -8, 8, -8, 16, 16, 16, 0, 0, QW, QH);
    // 9 tentacles: 2x(8-15)x2, 悬挂在body下方
    float tentX[] = {-5.5f, -5.5f, -5.5f, 0, 0, 0, 5.5f, 5.5f, 5.5f};
    float tentZ[] = {-5.5f, 0, 5.5f, -5.5f, 0, 5.5f, -5.5f, 0, 5.5f};
    int tentLen[] = {11, 12, 9, 14, 10, 13, 8, 11, 15};
    for (int i = 0; i < 9; i++) {
        buildBox(ghastVerts, tentX[i] - 1, 8 - tentLen[i], tentZ[i] - 1,
                 2, tentLen[i], 2, 0, 0, QW, QH);
    }

    // VAO/VBO（所有模型共享一个 VBO，渲染时按需切换偏移）
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    // 先分配足够大的空间，渲染时动态上传
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 10000, nullptr, GL_DYNAMIC_DRAW);

    // aPos = location 0, aUV = location 1
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    initialized = true;
    LOGI("EntityRenderer initialized");
    return true;
}

void EntityRenderer::cleanup() {
    if (!initialized) return;
    if (shaderProgram) { glDeleteProgram(shaderProgram); shaderProgram = 0; }
    if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    clearTextureCache();
    initialized = false;
}

void EntityRenderer::clearTextureCache() {
    for (auto& [name, tex] : textureCache) {
        if (tex) glDeleteTextures(1, &tex);
    }
    textureCache.clear();
}

// ===== 纹理加载 =====
GLuint EntityRenderer::getEntityTexture(const Entity& entity) {
    std::string key = entity.getTypeName();
    auto it = textureCache.find(key);
    if (it != textureCache.end()) return it->second;

    // 根据实体类型确定纹理路径
    std::string texPath;
    switch (entity.type) {
        case EntityType::ZOMBIE:
        case EntityType::ZOMBIE_VILLAGER:
        case EntityType::HUSK:
            texPath = "entity/zombie/zombie";
            break;
        case EntityType::SKELETON:
            texPath = "entity/skeleton/skeleton";
            break;
        case EntityType::CREEPER:
            texPath = "entity/creeper/creeper";
            break;
        case EntityType::SPIDER:
            texPath = "entity/spider/spider";
            break;
        case EntityType::PIG:
            texPath = "entity/pig/pig";
            break;
        case EntityType::COW:
            texPath = "entity/cow/cow";
            break;
        case EntityType::SHEEP:
            texPath = "entity/sheep/sheep";
            break;
        case EntityType::CHICKEN:
            texPath = "entity/chicken";
            break;
        case EntityType::PLAYER:
            texPath = "entity/steve";
            break;
        case EntityType::SLIME:
            texPath = "entity/slime/slime";
            break;
        case EntityType::BLAZE:
            texPath = "entity/blaze";
            break;
        case EntityType::GHAST:
            texPath = "entity/ghast/ghast";
            break;
        case EntityType::WOLF:
            texPath = "entity/wolf/wolf";
            break;
        case EntityType::CAT:
            texPath = "entity/cat/black";
            break;
        case EntityType::VILLAGER:
            texPath = "entity/villager/villager";
            break;
        case EntityType::IRON_GOLEM:
            texPath = "entity/iron_golem/iron_golem";
            break;
        case EntityType::WITCH:
            texPath = "entity/witch";
            break;
        case EntityType::PIGLIN:
            texPath = "entity/piglin/piglin";
            break;
        default:
            textureCache[key] = 0;
            return 0;
    }

    // 从 ZIP 加载
    // loadFromZip 自动添加 "textures/" 前缀，所以这里不需要
    std::string filename = texPath + ".png";
    TextureData tex = TextureLoader::loadPNG(filename);

    GLuint glTex = 0;
    if (tex.data && tex.width > 0 && tex.height > 0) {
        glGenTextures(1, &glTex);
        glBindTexture(GL_TEXTURE_2D, glTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        LOGI("Entity texture loaded: %s (%dx%d)", texPath.c_str(), tex.width, tex.height);
    } else {
        LOGI("Entity texture not found: %s (type=%s)", texPath.c_str(), key.c_str());
    }
    textureCache[key] = glTex;
    return glTex;
}

// ===== 渲染 =====
void EntityRenderer::renderAll(const std::vector<Entity>& entities,
                               const glm::mat4& view, const glm::mat4& proj,
                               float partialTick) {
    if (!initialized || entities.empty()) return;

    // 调试日志：每 120 帧打印实体数和类型
    static int frameCounter = 0;
    if (++frameCounter >= 120) {
        frameCounter = 0;
        LOGI("Entity render: %zu entities", entities.size());
        for (const auto& e : entities) {
            LOGI("  entity: type=%s(%d) pos=(%.1f,%.1f,%.1f)",
                 e.getTypeName(), (int)e.type, e.x, e.y, e.z);
        }
    }

    glUseProgram(shaderProgram);
    glBindVertexArray(vao);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE); // 实体需要双面渲染（某些部件）

    glm::mat4 vp = proj * view;

    for (const auto& entity : entities) {
        if (entity.removed) continue;
        // 跳过太远的实体（优化）
        // TODO: 视锥裁剪

        GLuint tex = getEntityTexture(entity);
        // tex == 0 时使用 fallback 纯色渲染（不跳过）

        glActiveTexture(GL_TEXTURE0);
        if (tex != 0) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glBindSampler(0, 0); // NEAREST
        } else {
            glBindTexture(GL_TEXTURE_2D, 0); // 无纹理时用纯色
        }
        glUniform1i(uTexture, 0);
        glUniform1i(uHasTexture, tex != 0 ? 1 : 0);
        if (tex == 0) {
            // fallback 颜色：根据实体类型着色
            switch (entity.type) {
                case EntityType::ZOMBIE: case EntityType::ZOMBIE_VILLAGER:
                    glUniform4f(uColor, 0.2f, 0.5f, 0.2f, 1.0f); break;
                case EntityType::SKELETON:
                    glUniform4f(uColor, 0.8f, 0.8f, 0.8f, 1.0f); break;
                case EntityType::CREEPER:
                    glUniform4f(uColor, 0.1f, 0.7f, 0.1f, 1.0f); break;
                case EntityType::PLAYER:
                    glUniform4f(uColor, 0.2f, 0.4f, 0.8f, 1.0f); break;
                case EntityType::PIG: case EntityType::COW: case EntityType::SHEEP:
                case EntityType::CHICKEN: case EntityType::HORSE:
                    glUniform4f(uColor, 0.6f, 0.4f, 0.2f, 1.0f); break;
                case EntityType::SPIDER:
                    glUniform4f(uColor, 0.3f, 0.2f, 0.1f, 1.0f); break;
                case EntityType::SLIME:
                    glUniform4f(uColor, 0.3f, 0.8f, 0.3f, 0.7f); break;
                case EntityType::GHAST:
                    glUniform4f(uColor, 0.9f, 0.9f, 0.9f, 1.0f); break;
                case EntityType::BLAZE:
                    glUniform4f(uColor, 1.0f, 0.7f, 0.0f, 1.0f); break;
                default:
                    glUniform4f(uColor, 0.5f, 0.5f, 0.5f, 1.0f); break;
            }
        }

        // 插值位置
        float ix = (float)(entity.prevX + (entity.x - entity.prevX) * partialTick);
        float iy = (float)(entity.prevY + (entity.y - entity.prevY) * partialTick);
        float iz = (float)(entity.prevZ + (entity.z - entity.prevZ) * partialTick);
        float iyaw = entity.prevYaw + (entity.yaw - entity.prevYaw) * partialTick;
        float iheadYaw = entity.prevHeadYaw + (entity.headYaw - entity.prevHeadYaw) * partialTick;

        // MVP: 平移到实体位置 + 旋转
        glm::mat4 model = glm::translate(vp, glm::vec3(ix, iy, iz));
        model = glm::rotate(model, glm::radians(-iyaw + 180.0f), glm::vec3(0, 1, 0));

        glm::mat4 mvp = model;
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, &mvp[0][0]);

        // 根据类型选择渲染方法
        switch (entity.type) {
            case EntityType::ZOMBIE:
            case EntityType::ZOMBIE_VILLAGER:
            case EntityType::SKELETON:
            case EntityType::PLAYER:
            case EntityType::WITCH:
            case EntityType::VILLAGER:
            case EntityType::PIGLIN:
            case EntityType::IRON_GOLEM:
            case EntityType::BLAZE:
                renderHumanoid(iyaw, iheadYaw, entity.pitch);
                break;
            case EntityType::PIG:
            case EntityType::COW:
            case EntityType::SHEEP:
            case EntityType::CHICKEN:
            case EntityType::HORSE:
            case EntityType::WOLF:
            case EntityType::CAT:
                renderQuadruped(iyaw, iheadYaw, entity.pitch);
                break;
            case EntityType::SPIDER:
                renderSpider(iyaw, iheadYaw);
                break;
            case EntityType::CREEPER:
                renderCreeper();
                break;
            case EntityType::SLIME:
                renderSlime();
                break;
            case EntityType::GHAST:
                renderGhast();
                break;
            default:
                // 未知类型：渲染为人形（兆底）
                renderHumanoid(iyaw, iheadYaw, entity.pitch);
                break;
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_CULL_FACE);
}

void EntityRenderer::renderHumanoid(float bodyYaw, float headYaw, float headPitch) {
    // 上传人形顶点数据
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, humanoidVerts.size() * sizeof(float), humanoidVerts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(humanoidVerts.size() / 5));
}

void EntityRenderer::renderQuadruped(float bodyYaw, float headYaw, float headPitch) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, quadrupedVerts.size() * sizeof(float), quadrupedVerts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(quadrupedVerts.size() / 5));
}

void EntityRenderer::renderSpider(float bodyYaw, float headYaw) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, spiderVerts.size() * sizeof(float), spiderVerts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(spiderVerts.size() / 5));
}

void EntityRenderer::renderCreeper() {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, creeperVerts.size() * sizeof(float), creeperVerts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(creeperVerts.size() / 5));
}

void EntityRenderer::renderSlime() {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, slimeVerts.size() * sizeof(float), slimeVerts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(slimeVerts.size() / 5));
}

void EntityRenderer::renderGhast() {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, ghastVerts.size() * sizeof(float), ghastVerts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(ghastVerts.size() / 5));
}

// ===== 几何构建工具 =====

void EntityRenderer::addFace(std::vector<float>& verts,
                             const glm::vec3& p0, const glm::vec3& p1,
                             const glm::vec3& p2, const glm::vec3& p3,
                             float u, float v, float faceW, float faceH,
                             float texW, float texH) {
    float u0 = u / texW, v0 = v / texH;
    float u1 = (u + faceW) / texW, v1 = (v + faceH) / texH;

    // 两个三角形: p0-p1-p2, p0-p2-p3
    auto addVert = [&](const glm::vec3& p, float tu, float tv) {
        // 从像素单位转换为 block 单位 (1px = 1/16 block)
        verts.push_back(p.x / 16.0f);
        verts.push_back(p.y / 16.0f);
        verts.push_back(p.z / 16.0f);
        verts.push_back(tu);
        verts.push_back(tv);
    };

    addVert(p0, u0, v0);
    addVert(p1, u1, v0);
    addVert(p2, u1, v1);
    addVert(p0, u0, v0);
    addVert(p2, u1, v1);
    addVert(p3, u0, v1);
}

void EntityRenderer::buildBox(std::vector<float>& verts,
                              float ox, float oy, float oz,
                              float w, float h, float d,
                              float u, float v, float texW, float texH) {
    // MC 64x64 UV 布局:
    // Top:    (u + d, v),         size: (w, d)
    // Bottom: (u + d + w, v),     size: (w, d)
    // Left:   (u, v + d),         size: (d, h)
    // Front:  (u + d, v + d),     size: (w, h)
    // Right:  (u + d + w, v + d), size: (d, h)
    // Back:   (u + d + w + d, v + d), size: (w, h)

    // 8 个顶点
    glm::vec3 p0(ox, oy + h, oz);         // 前上左
    glm::vec3 p1(ox + w, oy + h, oz);     // 前上右
    glm::vec3 p2(ox + w, oy, oz);         // 前下右
    glm::vec3 p3(ox, oy, oz);             // 前下左
    glm::vec3 p4(ox, oy + h, oz + d);     // 后上左
    glm::vec3 p5(ox + w, oy + h, oz + d); // 后上右
    glm::vec3 p6(ox + w, oy, oz + d);     // 后下右
    glm::vec3 p7(ox, oy, oz + d);         // 后下左

    // Top face (朝上看，从上面看顺时针)
    addFace(verts, p4, p5, p1, p0, u + d, v, w, d, texW, texH);

    // Bottom face
    addFace(verts, p3, p2, p6, p7, u + d + w, v, w, d, texW, texH);

    // Front face (朝前看)
    addFace(verts, p0, p1, p2, p3, u + d, v + d, w, h, texW, texH);

    // Back face (朝后看)
    addFace(verts, p5, p4, p7, p6, u + d + w + d, v + d, w, h, texW, texH);

    // Left face (朝左看)
    addFace(verts, p4, p0, p3, p7, u, v + d, d, h, texW, texH);

    // Right face (朝右看)
    addFace(verts, p1, p5, p6, p2, u + d + w, v + d, d, h, texW, texH);
}
