#pragma once
#include <string>
#include <unordered_map>
#include <GLES3/gl3.h>

class ResourcepackManager {
public:
    static ResourcepackManager& getInstance();

    // 物品纹理：item/<name>.png，回退 blocks/<name>.png
    GLuint getItemTexture(const std::string& itemName);

    // HUD 纹理：gui/sprites/hud/<path>.png
    GLuint getHudTexture(const std::string& path);

    // 通用 GUI 纹理：gui/<path>.png（如 container/inventory.png）
    GLuint getGuiTexture(const std::string& path);

    // 缺失纹理（紫色棋盘格）
    GLuint getMissingTexture();

    // 清理所有纹理（GL 上下文丢失时调用）
    void clear();

private:
    ResourcepackManager() = default;
    ~ResourcepackManager();

    GLuint missingTex = 0;
    GLuint createMissingTexture();
    GLuint loadAndUploadTexture(const std::string& fullPath);

    std::unordered_map<std::string, GLuint> cache;
};
