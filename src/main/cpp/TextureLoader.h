#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <unordered_map>
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

    TextureData() = default;

    TextureData(const TextureData&) = delete;
    TextureData& operator=(const TextureData&) = delete;

    TextureData clone() const {
        TextureData result;
        result.width = width;
        result.height = height;
        result.channels = channels;
        if (data) {
            result.data = new unsigned char[width * height * channels];
            std::memcpy(result.data, data, width * height * channels);
        }
        return result;
    }

    TextureData(TextureData&& other) noexcept {
        data = other.data;
        width = other.width;
        height = other.height;
        channels = other.channels;
        other.data = nullptr;
    }

    TextureData& operator=(TextureData&& other) noexcept {
        if (this != &other) {
            if (data) delete[] data;
            data = other.data;
            width = other.width;
            height = other.height;
            channels = other.channels;
            other.data = nullptr;
        }
        return *this;
    }
};

class TextureLoader {
public:
    static void setAssetManager(AAssetManager* assetManager);
    static void setZipPath(const std::string& path);
    static TextureData loadPNG(const std::string& filename);
    static TextureData loadImage(const std::string& filename);
    static std::string readTextFromZip(const std::string& filename);
    // 批量读取 ZIP 中指定前缀的所有文本文件（一次遍历，减少 ZIP 查找开销）
    static std::vector<std::pair<std::string, std::string>> readAllTextFromZip(const std::string& prefix);
    static void closeZip();
    // 获取 ZIP 句柄供其他模块使用（如 MusicManager）
    static void* getZipHandle() { return g_zip; }

private:
    static AAssetManager* g_assetManager;
    static std::string g_zipPath;
    static TextureData loadFromZip(const std::string& filename);

    static void* g_zip;       // mz_zip_archive 缓存
    static bool g_zipOpen;

    // 已解码纹理缓存（文件名 → TextureData）
    static std::unordered_map<std::string, TextureData> s_cache;
    static void clearCache();
};
