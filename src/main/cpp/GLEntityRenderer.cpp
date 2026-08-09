#include "GLEntityRenderer.h"
#include "TextureLoader.h"          // 你的 PNG 加载工具
#include <android/log.h>
#include <cmath>
#include <chrono>

#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>

#define LOG_TAG "GLEntityRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ===== 着色器源码 =====
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

// ===== 编译着色器辅助 =====
static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("Shader compile error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// ===== 初始化 =====
bool GLEntityRenderer::init() {
    if (m_initialized) return true;

    // 确保模型数据已构建
    EntityModel::initializeAll();

    // 编译着色器
    GLuint vs = compileShader(GL_VERTEX_SHADER, entityVertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, entityFragSrc);
    if (!vs || !fs) {
        LOGE("Shader compilation failed");
        return false;
    }

    m_shaderProgram = glCreateProgram();
    glAttachShader(m_shaderProgram, vs);
    glAttachShader(m_shaderProgram, fs);
    glLinkProgram(m_shaderProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linkOk;
    glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &linkOk);
    if (!linkOk) {
        char log[512];
        glGetProgramInfoLog(m_shaderProgram, sizeof(log), nullptr, log);
        LOGE("Shader link error: %s", log);
        glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
        return false;
    }

    m_uMVP = glGetUniformLocation(m_shaderProgram, "uMVP");
    m_uTexture = glGetUniformLocation(m_shaderProgram, "uTexture");
    m_uHasTexture = glGetUniformLocation(m_shaderProgram, "uHasTexture");
    m_uColor = glGetUniformLocation(m_shaderProgram, "uColor");

    // 创建 dummy VAO（实际渲染时每个模型有自己的 VAO）
    glGenVertexArrays(1, &m_dummyVao);

    m_initialized = true;
    LOGI("GLEntityRenderer initialized");
    return true;
}

void GLEntityRenderer::cleanup() {
    if (!m_initialized) return;
    if (m_shaderProgram) {
        glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
    }
    // 删除所有模型的 GPU 资源
    for (auto& pair : m_modelCache) {
        const auto& res = pair.second;
        if (res.vao) glDeleteVertexArrays(1, &res.vao);
        if (res.vbo) glDeleteBuffers(1, &res.vbo);
        if (res.ibo) glDeleteBuffers(1, &res.ibo);
    }
    m_modelCache.clear();

    if (m_dummyVao) {
        glDeleteVertexArrays(1, &m_dummyVao);
        m_dummyVao = 0;
    }

    clearTextureCache();
    m_initialized = false;
}

void GLEntityRenderer::clearTextureCache() {
    for (auto& pair : m_textureCache) {
        if (pair.second) glDeleteTextures(1, &pair.second);
    }
    m_textureCache.clear();
}

// ===== 纹理加载（同原有逻辑） =====
GLuint GLEntityRenderer::getEntityTexture(const Entity& entity) {
    std::string key = entity.getTypeName();
    auto it = m_textureCache.find(key);
    if (it != m_textureCache.end()) return it->second;

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
            m_textureCache[key] = 0;
            return 0;
    }

    std::string filename = texPath + ".png";
    TextureData tex = TextureLoader::loadPNG(filename); // 假设内部加 "textures/" 前缀

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
        LOGI("Texture loaded: %s (%dx%d)", texPath.c_str(), tex.width, tex.height);
    } else {
        LOGI("Texture not found: %s", texPath.c_str());
    }
    m_textureCache[key] = glTex;
    return glTex;
}

// ===== 模型 GPU 资源缓存 =====
const GLEntityRenderer::GLModelResource& GLEntityRenderer::getModelResource(const EntityModel& model) {
    auto it = m_modelCache.find(&model);
    if (it != m_modelCache.end()) return it->second;

    GLModelResource res;
    const auto& vertices = model.getVertices();
    const auto& indices = model.getIndices();
    const auto& layout = model.getLayout();

    glGenVertexArrays(1, &res.vao);
    glGenBuffers(1, &res.vbo);
    glBindVertexArray(res.vao);
    glBindBuffer(GL_ARRAY_BUFFER, res.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float),
                 vertices.data(), GL_STATIC_DRAW);

    // 设置顶点属性
    for (const auto& attr : layout.attributes) {
        GLenum type;
        switch (attr.type) {
            case VertexComponentType::FLOAT: type = GL_FLOAT; break;
            case VertexComponentType::UINT8: type = GL_UNSIGNED_BYTE; break;
            case VertexComponentType::UINT16: type = GL_UNSIGNED_SHORT; break;
            default: type = GL_FLOAT; break;
        }
        glEnableVertexAttribArray(attr.location);
        glVertexAttribPointer(attr.location, attr.size, type,
                              attr.normalized ? GL_TRUE : GL_FALSE,
                              layout.stride, (void*)(uintptr_t)attr.offset);
    }

    // 索引缓冲（如果有）
    if (!indices.empty()) {
        glGenBuffers(1, &res.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
                     indices.data(), GL_STATIC_DRAW);
        res.hasIndices = true;
        res.indexCount = (int)indices.size();
    } else {
        res.hasIndices = false;
        res.indexCount = 0;
    }

    res.vertexCount = (int)model.getVertexCount();

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_modelCache[&model] = res;
    LOGI("Cached model with %d vertices, %d indices",
         res.vertexCount, res.indexCount);
    return m_modelCache[&model];
}

// ===== 绘制函数（不再上传顶点） =====
void GLEntityRenderer::renderHumanoid(float bodyYaw, float headYaw, float headPitch) {
    (void)bodyYaw; (void)headYaw; (void)headPitch;
    const auto& model = EntityModel::getHumanoid();
    const auto& res = getModelResource(model);
    glBindVertexArray(res.vao);
    if (res.hasIndices) {
        glDrawElements(GL_TRIANGLES, res.indexCount, GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, res.vertexCount);
    }
}

void GLEntityRenderer::renderQuadruped(const EntityModel& model, float bodyYaw, float headYaw, float headPitch) {
    (void)bodyYaw; (void)headYaw; (void)headPitch;
    const auto& res = getModelResource(model);
    glBindVertexArray(res.vao);
    if (res.hasIndices) {
        glDrawElements(GL_TRIANGLES, res.indexCount, GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, res.vertexCount);
    }
}

void GLEntityRenderer::renderSpider(float bodyYaw, float headYaw) {
    (void)bodyYaw; (void)headYaw;
    const auto& model = EntityModel::getSpider();
    const auto& res = getModelResource(model);
    glBindVertexArray(res.vao);
    glDrawArrays(GL_TRIANGLES, 0, res.vertexCount);
}

void GLEntityRenderer::renderCreeper() {
    const auto& model = EntityModel::getCreeper();
    const auto& res = getModelResource(model);
    glBindVertexArray(res.vao);
    glDrawArrays(GL_TRIANGLES, 0, res.vertexCount);
}

void GLEntityRenderer::renderSlime() {
    const auto& model = EntityModel::getSlime();
    const auto& res = getModelResource(model);
    glBindVertexArray(res.vao);
    glDrawArrays(GL_TRIANGLES, 0, res.vertexCount);
}

void GLEntityRenderer::renderGhast() {
    const auto& model = EntityModel::getGhast();
    const auto& res = getModelResource(model);
    glBindVertexArray(res.vao);
    glDrawArrays(GL_TRIANGLES, 0, res.vertexCount);
}

void GLEntityRenderer::renderItem() {
    const auto& model = EntityModel::getItem();
    const auto& res = getModelResource(model);
    glBindVertexArray(res.vao);
    glDrawArrays(GL_TRIANGLES, 0, res.vertexCount);
}

// ===== 主渲染循环 =====
void GLEntityRenderer::renderAll(const std::vector<Entity>& entities,
                                 const glm::mat4& view, const glm::mat4& proj,
                                 float partialTick) {
    if (!m_initialized || entities.empty()) return;

    glUseProgram(m_shaderProgram);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);   // MC 风格双面渲染

    glm::mat4 vp = proj * view;

    // 提取视锥体平面（用于剔除）
    struct FrustumPlane { float a, b, c, d; };
    FrustumPlane planes[6];
    const float* mp = &vp[0][0];
    planes[0] = { mp[3]+mp[0], mp[7]+mp[4], mp[11]+mp[8], mp[15]+mp[12] };
    planes[1] = { mp[3]-mp[0], mp[7]-mp[4], mp[11]-mp[8], mp[15]-mp[12] };
    planes[2] = { mp[3]+mp[1], mp[7]+mp[5], mp[11]+mp[9], mp[15]+mp[13] };
    planes[3] = { mp[3]-mp[1], mp[7]-mp[5], mp[11]-mp[9], mp[15]-mp[13] };
    planes[4] = { mp[3]+mp[2], mp[7]+mp[6], mp[11]+mp[10], mp[15]+mp[14] };
    planes[5] = { mp[3]-mp[2], mp[7]-mp[6], mp[11]-mp[10], mp[15]-mp[14] };
    for (auto& p : planes) {
        float len = sqrtf(p.a*p.a + p.b*p.b + p.c*p.c);
        if (len > 0.0001f) { p.a /= len; p.b /= len; p.c /= len; p.d /= len; }
    }

    m_renderedCount = 0;
    m_totalCount = (int)entities.size();

    for (const auto& entity : entities) {
        if (entity.removed) continue;

        // 插值位置
        float ix = (float)(entity.prevX + (entity.x - entity.prevX) * partialTick);
        float iy = (float)(entity.prevY + (entity.y - entity.prevY) * partialTick);
        float iz = (float)(entity.prevZ + (entity.z - entity.prevZ) * partialTick);

        // 视锥剔除（包围球半径 2.0，可考虑按模型调整）
        bool visible = true;
        for (const auto& p : planes) {
            if (p.a*ix + p.b*iy + p.c*iz + p.d < -2.0f) {
                visible = false;
                break;
            }
        }
        if (!visible) continue;

        // 获取纹理
        GLuint tex = getEntityTexture(entity);
        glActiveTexture(GL_TEXTURE0);
        if (tex) {
            glBindTexture(GL_TEXTURE_2D, tex);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(m_uTexture, 0);
        glUniform1i(m_uHasTexture, tex ? 1 : 0);

        // 无纹理 fallback 颜色
        if (!tex) {
            switch (entity.type) {
                case EntityType::ZOMBIE: case EntityType::ZOMBIE_VILLAGER:
                    glUniform4f(m_uColor, 0.2f, 0.5f, 0.2f, 1.0f); break;
                case EntityType::SKELETON:
                    glUniform4f(m_uColor, 0.8f, 0.8f, 0.8f, 1.0f); break;
                case EntityType::CREEPER:
                    glUniform4f(m_uColor, 0.1f, 0.7f, 0.1f, 1.0f); break;
                case EntityType::PLAYER:
                    glUniform4f(m_uColor, 0.2f, 0.4f, 0.8f, 1.0f); break;
                case EntityType::PIG: case EntityType::COW: case EntityType::SHEEP:
                case EntityType::CHICKEN: case EntityType::HORSE:
                    glUniform4f(m_uColor, 0.6f, 0.4f, 0.2f, 1.0f); break;
                case EntityType::SPIDER:
                    glUniform4f(m_uColor, 0.3f, 0.2f, 0.1f, 1.0f); break;
                case EntityType::SLIME:
                    glUniform4f(m_uColor, 0.3f, 0.8f, 0.3f, 0.7f); break;
                case EntityType::GHAST:
                    glUniform4f(m_uColor, 0.9f, 0.9f, 0.9f, 1.0f); break;
                case EntityType::BLAZE:
                    glUniform4f(m_uColor, 1.0f, 0.7f, 0.0f, 1.0f); break;
                case EntityType::ITEM:
                    glUniform4f(m_uColor, 0.8f, 0.6f, 0.2f, 1.0f); break;
                default:
                    glUniform4f(m_uColor, 0.5f, 0.5f, 0.5f, 1.0f); break;
            }
        }

        // 插值旋转
        float iyaw = entity.prevYaw + (entity.yaw - entity.prevYaw) * partialTick;
        float iheadYaw = entity.prevHeadYaw + (entity.headYaw - entity.prevHeadYaw) * partialTick;

        // 构建模型矩阵
        glm::mat4 modelMatrix;
        if (entity.type == EntityType::ITEM) {
            static auto startTime = std::chrono::steady_clock::now();
            float time = std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime).count();
            float spinAngle = time * 90.0f;
            modelMatrix = glm::translate(vp, glm::vec3(ix, iy + 0.25f, iz));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(spinAngle), glm::vec3(0, 1, 0));
        } else {
            modelMatrix = glm::translate(vp, glm::vec3(ix, iy, iz));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(-iyaw + 180.0f), glm::vec3(0, 1, 0));
        }
        glUniformMatrix4fv(m_uMVP, 1, GL_FALSE, &modelMatrix[0][0]);

        // 分发渲染
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
            case EntityType::CHICKEN:
            case EntityType::HORSE:
            case EntityType::WOLF:
            case EntityType::CAT:
                renderQuadruped(EntityModel::getQuadruped(), iyaw, iheadYaw, entity.pitch);
                break;

            case EntityType::COW:
            case EntityType::SHEEP:
                renderQuadruped(EntityModel::getCow(), iyaw, iheadYaw, entity.pitch);
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

            case EntityType::ITEM:
                renderItem();
                break;

            case EntityType::ARROW:
            case EntityType::EXPERIENCE_ORB:
            case EntityType::FALLING_BLOCK:
            case EntityType::TNT:
            case EntityType::BOAT:
            case EntityType::MINECART:
                // 无模型，跳过
                break;

            default:
                // 未知类型回退人形
                renderHumanoid(iyaw, iheadYaw, entity.pitch);
                break;
        }

        m_renderedCount++;
    }

    // 清理状态
    glUseProgram(0);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}