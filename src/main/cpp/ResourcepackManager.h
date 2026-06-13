#pragma once
#include <string>
#include <unordered_map>
#include <GLES3/gl3.h>

// 编译后的着色器程序信息
struct ShaderProgramInfo {
    GLuint program = 0;
    // 标准 Mojang uniform 位置
    GLint uModelViewMat = -1;
    GLint uProjMat = -1;
    GLint uChunkOffset = -1;
    GLint uColorModulator = -1;
    GLint uFogStart = -1;
    GLint uFogEnd = -1;
    GLint uFogColor = -1;
    GLint uFogShape = -1;
    GLint uSampler0 = -1;
    GLint uSampler2 = -1;
    GLint uTextureMatrix = -1;
    GLint uGameTime = -1;
};

class ResourcepackManager {
public:
    static ResourcepackManager& getInstance();

    // 物品纹理
    GLuint getItemTexture(const std::string& itemName);
    GLuint getHudTexture(const std::string& path);
    GLuint getGuiTexture(const std::string& path);
    GLuint getMissingTexture();

    // ===== 着色器 =====
    // 从资源包加载并编译 Mojang 格式着色器程序
    // name: JSON 文件名（如 "rendertype_cutout"），自动从 shaders/core/ 加载
    // 返回 ShaderProgramInfo（program=0 表示失败）
    ShaderProgramInfo loadShaderProgram(const std::string& name);

    // 获取已缓存的着色器程序
    const ShaderProgramInfo* getShaderProgram(const std::string& name) const;

    // 清理
    void clear();

private:
    ResourcepackManager() = default;
    ~ResourcepackManager();

    GLuint missingTex = 0;
    GLuint createMissingTexture();
    GLuint loadAndUploadTexture(const std::string& fullPath);

    // ===== 着色器内部方法 =====
    // 读取着色器源文件，处理 #moj_import，转换 GL→GLES
    std::string loadShaderSource(const std::string& shaderPath);
    // 递归解析 #moj_import
    std::string resolveImports(const std::string& source, int depth = 0);
    // 将 OpenGL #version 150 转换为 GLES #version 300 es，并适配纹理数组
    // isVertex: true=顶点着色器, false=片段着色器
    std::string convertGLtoGLES(const std::string& source, bool isVertex);
    // 编译着色器
    GLuint compileShader(GLenum type, const std::string& source);
    // 链接程序（绑定 Mojang attribute locations）
    GLuint linkProgram(GLuint vert, GLuint frag);

    std::unordered_map<std::string, GLuint> cache;
    std::unordered_map<std::string, ShaderProgramInfo> shaderCache;
};
