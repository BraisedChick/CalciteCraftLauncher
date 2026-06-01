#include "ResourcepackManager.h"
#include "TextureLoader.h"
#include <android/log.h>

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

    // 先从 item/ 加载
    std::string path = "item/" + itemName + ".png";
    GLuint glTex = loadAndUploadTexture(path);
    if (glTex == 0) {
        // 回退到 blocks/
        std::string fallbackPath = "blocks/" + itemName + ".png";
        glTex = loadAndUploadTexture(fallbackPath);
        if (glTex == 0) {
            LOGE("Failed to load item texture: %s", path.c_str());
            GLuint fallback = getMissingTexture();
            cache[itemName] = fallback;
            return fallback;
        }
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
}
