#pragma once
#include <string>
#include <unordered_map>
#include <GLES3/gl3.h>

// 编译后的着色器程序信息
struct ShaderProgramInfo {
    GLuint program = 0;
    // 着色器 uniform 位置（未声明的为 -1，调用方需自行判空）
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

    // 3D 方块图标批量生成完成后调用：将窗口期内被 2D/missing 回退
    // 污染的缓存条目刷新为 3D 图标（hotbar 从进服第一帧就在查询）
    void refreshItemIcons();

    // ===== 着色器 =====
    // 编译 assets/shaders/opengl/ 下的自研 GLES 着色器
    // name: 内置表 kBuiltinShaderPrograms 里的程序名（如 "chunk_cutout"），其它名字一律失败
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
    // 编译着色器
    GLuint compileShader(GLenum type, const std::string& source);
    // 链接程序（attribute location 由着色器源码的 layout 指定）
    GLuint linkProgram(GLuint vert, GLuint frag);

    std::unordered_map<std::string, GLuint> cache;
    std::unordered_map<std::string, ShaderProgramInfo> shaderCache;
};
