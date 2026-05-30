#pragma once
#include <string>
#include <unordered_map>
#include <GLES3/gl3.h>

class ItemTextureManager {
public:
    static ItemTextureManager& getInstance() {
        static ItemTextureManager instance;
        return instance;
    }

    // 获取物品纹理（懒加载），返回 GLuint 纹理 ID，缺失时返回默认紫色纹理
    GLuint getTexture(const std::string& itemName);
    // 获取缺失纹理（紫色+黑色棋盘格）
    GLuint getMissingTexture();

    // 清理所有纹理
    void clear();

private:
    ItemTextureManager() = default;
    ~ItemTextureManager();

    GLuint missingTex = 0;    // 缺失纹理
    GLuint createMissingTexture();

    std::unordered_map<std::string, GLuint> cache;
};
