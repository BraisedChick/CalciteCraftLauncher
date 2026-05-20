#include "TextureLoader.h"
#include <android/log.h>
#include <cstring>

#define LOG_TAG "TextureLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 包含 stb_image
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

AAssetManager* TextureLoader::g_assetManager = nullptr;

void TextureLoader::setAssetManager(AAssetManager* assetManager) {
    g_assetManager = assetManager;
}

TextureData TextureLoader::loadPNG(const std::string& filename) {
    TextureData result;
    
    if (!g_assetManager) {
        LOGE("Asset manager not set!");
        return result;
    }

    LOGI("Attempting to open texture: %s", filename.c_str());
    AAsset* asset = AAssetManager_open(g_assetManager, filename.c_str(), AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Failed to open texture file: %s", filename.c_str());
        return result;
    }

    off_t length = AAsset_getLength(asset);
    const void* buffer = AAsset_getBuffer(asset);
    
    LOGI("Texture file opened: size=%ld bytes", (long)length);

    if (!buffer || length == 0) {
        LOGE("Empty texture file: %s", filename.c_str());
        AAsset_close(asset);
        return result;
    }

    // 使用 stb_image 解码 PNG
    int width, height, channels;
    unsigned char* imageData = stbi_load_from_memory(
        static_cast<const stbi_uc*>(buffer), 
        static_cast<int>(length), 
        &width, 
        &height, 
        &channels, 
        4  // 强制输出 RGBA
    );

    AAsset_close(asset);

    if (!imageData) {
        LOGE("Failed to decode PNG: %s - %s", filename.c_str(), stbi_failure_reason());
        return result;
    }

    result.width = width;
    result.height = height;
    result.channels = 4;
    result.data = new unsigned char[width * height * 4];
    std::memcpy(result.data, imageData, width * height * 4);
    
    stbi_image_free(imageData);

    LOGI("Loaded texture: %s (%dx%d, %d channels)", filename.c_str(), width, height, channels);
    return result;
}

TextureData TextureLoader::loadImage(const std::string& filename) {
    LOGI("loadImage called with: %s", filename.c_str());

    // 根据文件扩展名选择加载方式
    if (filename.size() >= 4) {
        std::string ext = filename.substr(filename.size() - 4);
        LOGI("File extension: %s", ext.c_str());

        if (ext == ".png") {
            LOGI("Detected PNG format, calling loadPNG");
            return loadPNG(filename);
        }
    }
    
    LOGE("Unsupported image format or invalid filename: %s", filename.c_str());
    return TextureData();
}