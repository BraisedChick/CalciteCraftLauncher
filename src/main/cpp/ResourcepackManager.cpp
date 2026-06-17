#include "ResourcepackManager.h"
#include "TextureLoader.h"
#include "TextureAtlas.h"
#include "GLRenderer.h"
#include <android/log.h>
#include <fstream>
#include <sstream>

// 从文件系统读取（开发阶段 fallback）
static std::string readFileFromDisk(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// g_glRenderer 定义在 native-lib.cpp

extern GLRenderer* g_glRenderer;

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
    if (g_glRenderer) {
        const GLuint* cached = g_glRenderer->getBlockIcon(itemName);
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
// 着色器加载（Mojang → GLES）
// ============================================================

// 读取并预处理着色器源文件
std::string ResourcepackManager::loadShaderSource(const std::string& shaderPath) {
    if (shaderPath.empty()) return "";
    std::string content;

    // 1) 尝试从 ZIP 读取（着色器都在 core/ 和 include/ 下）
    content = TextureLoader::readTextFromZip("shaders/core/" + shaderPath);
    if (content.empty()) {
        content = TextureLoader::readTextFromZip("shaders/include/" + shaderPath);
    }

    // 2) ZIP 失败时，尝试从磁盘读取
    if (content.empty()) {
        content = readFileFromDisk("shaders/core/" + shaderPath);
    }
    if (content.empty()) {
        content = readFileFromDisk("shaders/include/" + shaderPath);
    }

    if (content.empty()) {
        LOGE("Failed to read shader: %s", shaderPath.c_str());
        return "";
    }
    // 递归解析 #moj_import
    return resolveImports(content, 0);
}

// 递归解析 #moj_import <...>
std::string ResourcepackManager::resolveImports(const std::string& source, int depth) {
    if (depth > 16) {
        LOGE("Shader #moj_import recursion too deep");
        return source;
    }
    std::string result;
    size_t pos = 0;
    while (pos < source.size()) {
        size_t importPos = source.find("#moj_import", pos);
        if (importPos == std::string::npos) {
            result += source.substr(pos);
            break;
        }
        // 复制 #moj_import 之前的文本
        result += source.substr(pos, importPos - pos);
        // 找到 < 和 >
        size_t openPos = source.find('<', importPos);
        size_t closePos = source.find('>', importPos);
        if (openPos == std::string::npos || closePos == std::string::npos || closePos <= openPos) {
            LOGE("Malformed #moj_import");
            result += source.substr(importPos, (closePos != std::string::npos ? closePos + 1 : source.size() - importPos));
            pos = (closePos != std::string::npos) ? closePos + 1 : source.size();
            continue;
        }
        std::string includeFile = source.substr(openPos + 1, closePos - openPos - 1);
        // 加载包含文件
        std::string includeContent = TextureLoader::readTextFromZip("shaders/include/" + includeFile);
        if (includeContent.empty()) {
            // 文件系统回退
            includeContent = readFileFromDisk("shaders/include/" + includeFile);
        }
        if (includeContent.empty()) {
            LOGE("Failed to load include: %s", includeFile.c_str());
            result += "// MISSING INCLUDE: " + includeFile + "\n";
        } else {
            // 递归处理包含文件中的 #moj_import
            includeContent = resolveImports(includeContent, depth + 1);
            result += includeContent;
        }
        pos = closePos + 1;
    }
    return result;
}

// 转换 #version 150 → #version 300 es，并适配纹理数组 + Mojang attribute layout
std::string ResourcepackManager::convertGLtoGLES(const std::string& source, bool isVertex) {
    // ===== 1. 去除所有 #version 行（包括 include 文件带入的）=====
    std::string cleaned;
    size_t pos = 0;
    while (pos < source.size()) {
        size_t vPos = source.find("#version", pos);
        if (vPos == std::string::npos) {
            cleaned += source.substr(pos);
            break;
        }
        // 复制 #version 之前的内容
        cleaned += source.substr(pos, vPos - pos);
        // 跳过整行
        size_t eol = source.find('\n', vPos);
        if (eol != std::string::npos) {
            pos = eol + 1;
        } else {
            pos = source.size();
        }
    }

    // ===== 2. 构建最终着色器：#version 300 es 头 =====
    std::string result = "#version 300 es\n";
    if (!isVertex) {
        result += "precision mediump float;\n";
    }

    // ===== 3. 所有 ivec2 → vec2 转换（GLSL ES 3.0 不支持 ivec2/float 混合运算）=====
    // 替换所有出现的 "ivec2"，无论后面跟什么（UV2, uv 等）
    {
        std::string src = cleaned;
        cleaned.clear();
        size_t p = 0;
        while (p < src.size()) {
            size_t found = src.find("ivec2", p);
            if (found == std::string::npos) {
                cleaned += src.substr(p);
                break;
            }
            cleaned += src.substr(p, found - p);
            cleaned += "vec2";
            p = found + 5;  // len("ivec2")
        }
    }

    // ===== 4. sampler2D Sampler0 → sampler2DArray Sampler0（仅主纹理采样器）=====
    {
        size_t p = 0;
        std::string src = cleaned;
        cleaned.clear();
        while (p < src.size()) {
            size_t found = src.find("sampler2D Sampler0", p);
            if (found != std::string::npos) {
                cleaned += src.substr(p, found - p);
                cleaned += "sampler2DArray Sampler0";
                p = found + 18;  // len("sampler2D Sampler0")
                continue;
            }
            cleaned += src.substr(p);
            break;
        }
    }

    // ===== 5. 注入纹理数组相关变量和 texture 调用改写 =====
    if (isVertex) {
        // 顶点着色器：注入 layout locations + TexIndex 属性 + fragTexIndex 输出
        // 注入 layout locations：按着色器声明顺序 Position → Color → UV0 → UV2 → Normal
        // 并插入我们的 TexIndex 属性
        {
            std::string src = cleaned;
            cleaned.clear();

            // 辅助 lambda：查找并替换原始声明为带 layout 的版本
            auto replaceAttr = [&](const std::string& decl, const std::string& layoutDecl) -> bool {
                size_t pos = src.find(decl);
                if (pos == std::string::npos) return false;
                cleaned += src.substr(0, pos);
                cleaned += layoutDecl + "\n";
                size_t eol = src.find('\n', pos);
                src = (eol != std::string::npos) ? src.substr(eol + 1) : "";
                return true;
            };

            // Position -> layout 0
            if (!replaceAttr("in vec3 Position", "layout(location = 0) in vec3 Position;")) {
                cleaned = src; goto endLayout;
            }

            // Color -> layout 3（在原声明位置处理，保证顺序）
            replaceAttr("in vec4 Color", "layout(location = 3) in vec4 Color;");

            // UV0 -> layout 1
            replaceAttr("in vec2 UV0", "layout(location = 1) in vec2 UV0;");

            // 注入 TexIndex -> layout 2
            cleaned += "layout(location = 2) in float TexIndex;\n";

            // UV2 -> layout 5
            replaceAttr("in vec2 UV2", "layout(location = 5) in vec2 UV2;");

            // Normal -> layout 4
            replaceAttr("in vec3 Normal", "layout(location = 4) in vec3 Normal;");

            // 剩余内容
            cleaned += src;
endLayout:;
        }

        // 在 "out vec4 normal" 后注入 fragTexIndex 输出（flat 防止插值）
        {
            std::string needle = "out vec4 normal;";
            size_t found = cleaned.find(needle);
            if (found != std::string::npos) {
                cleaned.insert(found + needle.size(), "\nflat out float fragTexIndex;");
            }
        }

        // 在 "texCoord0 = UV0;" 后注入 fragTexIndex = round(TexIndex)
        {
            std::string needle = "texCoord0 = UV0;";
            size_t found = cleaned.find(needle);
            if (found != std::string::npos) {
                cleaned.insert(found + needle.size(), "\n    fragTexIndex = round(TexIndex);");
            }
        }
    } else {
        // 片段着色器：注入 fragTexIndex 输入（flat） + 改写 texture 调用

        // 在 "in vec4 normal" 后注入 flat fragTexIndex
        {
            std::string needle = "in vec4 normal;";
            size_t found = cleaned.find(needle);
            if (found != std::string::npos) {
                cleaned.insert(found + needle.size(), "\nflat in float fragTexIndex;");
            }
        }

        // 替换 texture(Sampler0, texCoord0) → texture(Sampler0, vec3(texCoord0, round(fragTexIndex)))
        {
            std::string needle = "texture(Sampler0, texCoord0)";
            size_t found = cleaned.find(needle);
            while (found != std::string::npos) {
                // 替换 texture 调用
                cleaned.replace(found, needle.size(), "texture(Sampler0, vec3(texCoord0, round(fragTexIndex)))");
                found = cleaned.find(needle, found + 45);
            }
        }
    }

    result += cleaned;
    return result;
}

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
    // 绑定 Mojang attribute locations（与顶点着色器 layout 一致）
    glBindAttribLocation(program, 0, "Position");
    glBindAttribLocation(program, 1, "UV0");
    glBindAttribLocation(program, 2, "TexIndex");
    glBindAttribLocation(program, 3, "Color");
    glBindAttribLocation(program, 4, "Normal");
    glBindAttribLocation(program, 5, "UV2");
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

    // 加载顶点和片段着色器源
    std::string vertPath = name + ".vsh";
    std::string fragPath = name + ".fsh";

    std::string vertSource = loadShaderSource(vertPath);
    std::string fragSource = loadShaderSource(fragPath);

    if (vertSource.empty() || fragSource.empty()) {
        LOGE("Failed to load shader sources for: %s", name.c_str());
        shaderCache[name] = info;
        return info;
    }

    // 转换为 GLES（适配纹理数组）
    vertSource = convertGLtoGLES(vertSource, true);   // isVertex=true
    fragSource = convertGLtoGLES(fragSource, false);   // isVertex=false

    // 调试：首次加载 rendertype_cutout 时打印转换后的顶点着色器
    if (name == "rendertype_cutout" && vertSource.find("layout") != std::string::npos) {
        LOGI("=== Converted vertex shader for %s ===", name.c_str());
        // 只打印前 60 行避免日志过长
        size_t newlineCount = 0;
        size_t pos = 0;
        while (newlineCount < 60 && pos < vertSource.size()) {
            size_t nl = vertSource.find('\n', pos);
            if (nl == std::string::npos) break;
            std::string line = vertSource.substr(pos, nl - pos);
            LOGI("VS[%zu]: %s", newlineCount, line.c_str());
            pos = nl + 1;
            newlineCount++;
        }
        LOGI("=== End vertex shader (showing %zu lines) ===", newlineCount);
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
