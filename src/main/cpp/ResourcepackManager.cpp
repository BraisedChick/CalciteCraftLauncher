#include "ResourcepackManager.h"
#include "TextureLoader.h"
#include "TextureAtlas.h"
#include "Renderer/GLRenderer.h"
#include "ClientEngine/ClientEngine.h"
#include <android/log.h>
#include <android/asset_manager.h>

#define LOG_TAG "Resourcepack"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

ResourcepackManager& ResourcepackManager::getInstance() {
    static ResourcepackManager instance;
    return instance;
}

ResourcepackManager::~ResourcepackManager() {
    clear();
}

GLuint ResourcepackManager::getMissingTexture() {
    if (missingTex != 0) return missingTex;
    missingTex = createMissingTexture();
    return missingTex;
}

GLuint ResourcepackManager::createMissingTexture() {
    const int SIZE = 16;
    unsigned char pixels[SIZE][SIZE][4];
    const unsigned char purple[4] = {180, 0, 180, 255};
    const unsigned char black[4] = {0, 0, 0, 255};

    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            bool isPurple = ((x / 4) + (y / 4)) % 2 == 0;
            const unsigned char* c = isPurple ? purple : black;
            pixels[y][x][0] = c[0];
            pixels[y][x][1] = c[1];
            pixels[y][x][2] = c[2];
            pixels[y][x][3] = c[3];
        }
    }

    GLuint glTex = 0;
    glGenTextures(1, &glTex);
    glBindTexture(GL_TEXTURE_2D, glTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SIZE, SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    LOGI("Created missing texture -> GL%d", glTex);
    return glTex;
}

GLuint ResourcepackManager::loadAndUploadTexture(const std::string& fullPath) {
    TextureData tex = TextureLoader::loadPNG(fullPath);
    if (!tex.data) return 0;

    GLuint glTex = 0;
    glGenTextures(1, &glTex);
    glBindTexture(GL_TEXTURE_2D, glTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data);
    glBindTexture(GL_TEXTURE_2D, 0);

    LOGI("Loaded texture: %s (%dx%d) -> GL%d", fullPath.c_str(), tex.width, tex.height, glTex);
    return glTex;
}

GLuint ResourcepackManager::getItemTexture(const std::string& itemName) {
    auto it = cache.find(itemName);
    if (it != cache.end()) return it->second;

    GLuint glTex = 0;

    // 1) 优先使用 3D 方块模型渲染的图标（所有可放置方块显示为立体）
    if (ClientEngine::getInstance() && ClientEngine::getInstance()->getRenderer()) {
        const GLuint* cached = ClientEngine::getInstance()->getRenderer()->getBlockIcon(itemName);
        if (cached) {
            glTex = *cached;
        }
    }

    // 2) 无 3D 图标时，从 item/ 加载 2D 纹理（工具、物品等非方块）
    if (glTex == 0) {
        std::string path = "item/" + itemName + ".png";
        glTex = loadAndUploadTexture(path);
    }

    // 3) 回退到 block/ 目录的 2D 纹理
    if (glTex == 0) {
        std::string fallbackPath = "block/" + itemName + ".png";
        glTex = loadAndUploadTexture(fallbackPath);
    }

    // 4) 全部失败，使用紫色棋盘格
    if (glTex == 0) {
        LOGE("Failed to load item texture: %s", itemName.c_str());
        glTex = getMissingTexture();
    }

    cache[itemName] = glTex;
    return glTex;
}

void ResourcepackManager::refreshItemIcons() {
    // 3D 图标批量生成前（纹理分批上传窗口期）hotbar 已在每帧查询图标，
    // 回退的 2D/missing 结果被永久缓存；这里把能查到 3D 图标的条目统一刷新
    if (!ClientEngine::getInstance() || !ClientEngine::getInstance()->getRenderer()) return;
    auto* renderer = ClientEngine::getInstance()->getRenderer();

    int count = 0;
    for (auto& pair : cache) {
        // HUD/GUI 纹理的 key 含 '/'，不是物品名，跳过
        if (pair.first.find('/') != std::string::npos) continue;

        const GLuint* icon = renderer->getBlockIcon(pair.first);
        if (!icon || *icon == 0 || *icon == pair.second) continue;

        // 删掉旧的 2D 回退纹理（missingTex 共享，不能删）
        if (pair.second != 0 && pair.second != missingTex) {
            glDeleteTextures(1, &pair.second);
        }
        pair.second = *icon;
        count++;
    }
    if (count > 0) {
        LOGI("Refreshed %d item icons to 3D block icons", count);
    }
}

GLuint ResourcepackManager::getHudTexture(const std::string& path) {
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;

    std::string fullPath = "gui/sprites/hud/" + path + ".png";
    GLuint glTex = loadAndUploadTexture(fullPath);
    if (glTex == 0) {
        LOGE("Failed to load HUD texture: %s", fullPath.c_str());
        cache[path] = 0;
        return 0;
    }

    cache[path] = glTex;
    return glTex;
}

GLuint ResourcepackManager::getGuiTexture(const std::string& path) {
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;

    std::string fullPath = "gui/" + path + ".png";
    GLuint glTex = loadAndUploadTexture(fullPath);
    if (glTex == 0) {
        LOGE("Failed to load GUI texture: %s", fullPath.c_str());
        cache[path] = 0;
        return 0;
    }

    cache[path] = glTex;
    return glTex;
}

void ResourcepackManager::clear() {
    for (auto& pair : cache) {
        if (pair.second != 0 && pair.second != missingTex) {
            glDeleteTextures(1, &pair.second);
        }
    }
    cache.clear();
    if (missingTex != 0) {
        glDeleteTextures(1, &missingTex);
        missingTex = 0;
    }
    // 清理着色器
    for (auto& pair : shaderCache) {
        if (pair.second.program != 0) {
            glDeleteProgram(pair.second.program);
        }
    }
    shaderCache.clear();
}

// ============================================================
// 着色器加载
//
// 改这些文件时需与两处契约对齐：
//   1) 顶点输入布局 = GLRenderer::processCompletedWork 里的 PackedVertex
//      loc 0 Position vec3 / loc 1 UV0 vec2(归一化) / loc 2 TexIndex float
//      loc 3 Color vec4(归一化) / loc 4 Normal vec4(地形管线未使用) /
//      loc 5 UV2 vec2(原值，光照贴图坐标 block*16+8, sky*16+8)
//      location 只由着色器里的 layout(location = N) 单点定义，C++ 侧不再 glBindAttribLocation
//   2) uniform 名称 = loadShaderProgram() 末尾按名取位置的那批
//      ModelViewMat / ProjMat / ChunkOffset / ColorModulator / FogStart /
//      FogEnd / FogColor / FogShape / Sampler0 / Sampler2
// ============================================================
namespace {

// assets 内的目录前缀
constexpr const char* kShaderAssetDir = "shaders/opengl/";

struct BuiltinShaderProgram {
    const char* name;     // loadShaderProgram() 传入的程序名
    const char* vertex;   // kShaderAssetDir 下的文件名
    const char* fragment;
};

// 四个程序共用一份顶点着色器，片段着色器只差 alpha 丢弃阈值
constexpr BuiltinShaderProgram kBuiltinShaderPrograms[] = {
    {"chunk_solid",         "chunk.vsh", "chunk_solid.fsh"},          // 不透明
    {"chunk_cutout",        "chunk.vsh", "chunk_cutout.fsh"},         // 镂空（草、花、栅栏）
    {"chunk_cutout_mipped", "chunk.vsh", "chunk_cutout_mipped.fsh"},  // 镂空 + mipmap
    {"chunk_translucent",   "chunk.vsh", "chunk_translucent.fsh"},    // 半透明（水）
};

const BuiltinShaderProgram* findBuiltinShader(const std::string& name) {
    for (const auto& p : kBuiltinShaderPrograms) {
        if (name == p.name) return &p;
    }
    return nullptr;
}

// 从 APK assets 读取着色器源码，失败返回空串
std::string readShaderAsset(const char* fileName) {
    if (!fileName) return "";

    AAssetManager* am = TextureLoader::getAssetManager();
    if (!am) return "";

    std::string path = std::string(kShaderAssetDir) + fileName;
    AAsset* asset = AAssetManager_open(am, path.c_str(), AASSET_MODE_BUFFER);
    if (!asset) return "";

    std::string content;
    const void* buffer = AAsset_getBuffer(asset);
    off_t length = AAsset_getLength(asset);
    if (buffer && length > 0) {
        // AAsset_close 后 buffer 失效，必须先复制出来
        content.assign(static_cast<const char*>(buffer), static_cast<size_t>(length));
    }
    AAsset_close(asset);

    return content;
}

}  // namespace

GLuint ResourcepackManager::compileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        LOGE("Shader compile error (type=%d): %s", type, infoLog);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint ResourcepackManager::linkProgram(GLuint vert, GLuint frag) {
    GLuint program = glCreateProgram();
    // attribute location 由着色器源码里的 layout(location = N) 显式指定，
    // 这里不再 glBindAttribLocation，避免与 PackedVertex 布局产生两处定义
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        LOGE("Program link error: %s", infoLog);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

ShaderProgramInfo ResourcepackManager::loadShaderProgram(const std::string& name) {
    // 查缓存
    auto it = shaderCache.find(name);
    if (it != shaderCache.end()) return it->second;

    ShaderProgramInfo info;

    // 只认内置着色器表：源码位于 assets/shaders/opengl/，已是 #version 300 es
    const BuiltinShaderProgram* builtin = findBuiltinShader(name);
    if (!builtin) {
        LOGE("Unknown shader program: %s", name.c_str());
        shaderCache[name] = info;
        return info;
    }

    std::string vertSource = readShaderAsset(builtin->vertex);
    std::string fragSource = readShaderAsset(builtin->fragment);
    if (vertSource.empty() || fragSource.empty()) {
        LOGE("Missing built-in shader asset: %s (shaders/opengl/%s or %s)",
             name.c_str(), builtin->vertex, builtin->fragment);
        shaderCache[name] = info;
        return info;
    }

    // 编译
    GLuint vert = compileShader(GL_VERTEX_SHADER, vertSource);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSource);
    if (vert == 0 || frag == 0) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        shaderCache[name] = info;
        return info;
    }

    // 链接
    info.program = linkProgram(vert, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);

    if (info.program == 0) {
        LOGE("Failed to link shader program: %s", name.c_str());
        shaderCache[name] = info;
        return info;
    }

    // 获取 uniform 位置
    info.uModelViewMat = glGetUniformLocation(info.program, "ModelViewMat");
    info.uProjMat = glGetUniformLocation(info.program, "ProjMat");
    info.uChunkOffset = glGetUniformLocation(info.program, "ChunkOffset");
    info.uColorModulator = glGetUniformLocation(info.program, "ColorModulator");
    info.uFogStart = glGetUniformLocation(info.program, "FogStart");
    info.uFogEnd = glGetUniformLocation(info.program, "FogEnd");
    info.uFogColor = glGetUniformLocation(info.program, "FogColor");
    info.uFogShape = glGetUniformLocation(info.program, "FogShape");
    info.uSampler0 = glGetUniformLocation(info.program, "Sampler0");
    info.uSampler2 = glGetUniformLocation(info.program, "Sampler2");
    info.uTextureMatrix = glGetUniformLocation(info.program, "TextureMat");
    info.uGameTime = glGetUniformLocation(info.program, "GameTime");

    LOGI("Loaded shader program: %s (program=%d)", name.c_str(), info.program);
    shaderCache[name] = info;
    return info;
}

const ShaderProgramInfo* ResourcepackManager::getShaderProgram(const std::string& name) const {
    auto it = shaderCache.find(name);
    if (it != shaderCache.end() && it->second.program != 0) {
        return &it->second;
    }
    return nullptr;
}
