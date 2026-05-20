#pragma once

#include <vector>
#include <string>
#include <android/asset_manager.h>

struct TextureData {
    unsigned char* data = nullptr;
    int width = 0;
    int height = 0;
    int channels = 4;

    ~TextureData() {
        if (data) {
            delete[] data;
            data = nullptr;
        }
    }

    // 添加默认构造函数
    TextureData() = default;

    TextureData(const TextureData&) = delete;
    TextureData& operator=(const TextureData&) = delete;

    TextureData(TextureData&& other) noexcept {
        data = other.data;
        width = other.width;
        height = other.height;
        channels = other.channels;
        other.data = nullptr;
    }
};

class TextureLoader {
public:
    static void setAssetManager(AAssetManager* assetManager);
    static TextureData loadPNG(const std::string& filename);
    static TextureData loadImage(const std::string& filename);

private:
    static AAssetManager* g_assetManager;
};
