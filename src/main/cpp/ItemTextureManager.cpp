#include "ItemTextureManager.h"
#include "TextureLoader.h"
#include <android/log.h>

#define LOG_TAG "ItemTexture"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

ItemTextureManager::~ItemTextureManager() {
    clear();
}

GLuint ItemTextureManager::getMissingTexture() {
    if (missingTex != 0) return missingTex;
    missingTex = createMissingTexture();
    return missingTex;
}

GLuint ItemTextureManager::createMissingTexture() {
    // 16x16 紫色/黑色棋盘格（类似 Minecraft 缺失纹理）
    const int SIZE = 16;
    unsigned char pixels[SIZE][SIZE][4];
    const unsigned char purple[4] = {180, 0, 180, 255};   // #B400B4
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

    LOGI("Created missing texture (16x16 purple/black checkerboard) -> GL%d", glTex);
    return glTex;
}

GLuint ItemTextureManager::getTexture(const std::string& itemName) {
    // 检查缓存
    auto it = cache.find(itemName);
    if (it != cache.end()) return it->second;

    // 从 ZIP 加载：先试 item/，再试 blocks/（方块类物品复用方块纹理）
    std::string path = "item/" + itemName + ".png";
    TextureData tex = TextureLoader::loadPNG(path);
    if (!tex.data) {
        // item/ 没找到，回退到 blocks/（兼容 stone、dirt 等既是方块又是物品的情况）
        std::string fallbackPath = "blocks/" + itemName + ".png";
        tex = TextureLoader::loadPNG(fallbackPath);
        if (!tex.data) {
            LOGE("Failed to load item texture: %s, using missing texture", path.c_str());
            GLuint fallback = getMissingTexture();
            cache[itemName] = fallback;
            return fallback;
        }
        path = fallbackPath; // 记录实际加载的路径用于日志
    }

    // 上传为 OpenGL 纹理
    GLuint glTex = 0;
    glGenTextures(1, &glTex);
    glBindTexture(GL_TEXTURE_2D, glTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    LOGI("Loaded item texture: %s (%dx%d) -> GL%d", itemName.c_str(), tex.width, tex.height, glTex);

    cache[itemName] = glTex;
    return glTex;
}

void ItemTextureManager::clear() {
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
}
