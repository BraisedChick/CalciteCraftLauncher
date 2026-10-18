#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <unordered_map>
#include <android/asset_manager.h>
#include <shared_mutex>
#include <mutex>

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
    // 资源来源：每类资源只有一个权威来源，不做跨源回退（见 routeFor）
    enum class Source {
        Index,     // 哈希资产库 files/assets/objects
        Zip,       // 服务器资源包 files/resourcepack<协议>.zip
        Vanilla,   // files/assets/vanilla（最新正式版 client.jar 解包）
    };

    static void setAssetManager(AAssetManager* assetManager);
    static AAssetManager* getAssetManager() { return g_assetManager; }
    static void setZipPath(const std::string& path);
    // 纹理/模型/blockstate 的唯一来源（布局与服务器 ZIP entry 一致）
    static void setVanillaRoot(const std::string& path);
    static TextureData loadPNG(const std::string& filename);
    static TextureData loadImage(const std::string& filename);
    static std::string readTextFromZip(const std::string& filename);
    // 批量读取 prefix 下的所有文本文件，来源同样由 routeFor(prefix) 决定
    static std::vector<std::pair<std::string, std::string>> readAllTextFromZip(const std::string& prefix);
    static Source routeFor(const std::string& path);
    static const char* sourceName(Source s);
    static void closeZip();
    static void* getZipHandle() { return g_zip; }

private:
    static AAssetManager* g_assetManager;
    static std::string g_zipPath;
    static std::string g_vanillaRoot;
    static TextureData loadFromStore(const std::string& filename);
    static TextureData loadFromZip(const std::string& filename);
    static TextureData loadFromVanilla(const std::string& filename);
    static TextureData decodeFromBytes(const std::vector<uint8_t>& bytes);
    static std::string readTextFromVanilla(const std::string& filename);
    static std::string readTextFromZipFile(const std::string& filename);
    static std::string normalizeTexturePath(const std::string& filename);

    static void* g_zip;       // mz_zip_archive 缓存
    static bool g_zipOpen;

    // 已解码纹理缓存（文件名 → TextureData）
    static std::unordered_map<std::string, TextureData> s_cache;
    static std::shared_mutex s_cacheMutex;
    static void clearCache();
};
