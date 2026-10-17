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
    static void setAssetManager(AAssetManager* assetManager);
    static AAssetManager* getAssetManager() { return g_assetManager; }
    static void setZipPath(const std::string& path);
    // vanilla 底层根目录：最新正式版 client.jar 解包 assets/minecraft 剥前缀的本地目录，
    // 官方纹理/模型/语言表的兜底源（路径布局与服务器 ZIP entry 一致）
    static void setVanillaRoot(const std::string& path);
    // 来源优先级总开关（实验用，改一处即可整体回退）：
    // true  = 官方新版优先（blockstates/models/纹理先取 vanilla，缺失再回退服务器 ZIP）
    // false = 服务器 ZIP 优先（按协议版本定制的资源胜出）
    static void setPreferVanilla(bool prefer);
    static bool preferVanilla() { return g_preferVanilla; }
    static TextureData loadPNG(const std::string& filename);
    static TextureData loadImage(const std::string& filename);
    static std::string readTextFromZip(const std::string& filename);
    // 批量读取指定前缀的所有文本文件：哈希资产库按 index 枚举优先；
    // 未收录时按 setPreferVanilla 的顺序合并 vanilla 目录与服务器 ZIP（同一路径不重复）
    static std::vector<std::pair<std::string, std::string>> readAllTextFromZip(const std::string& prefix);
    static void closeZip();
    // 获取 ZIP 句柄供其他模块使用（如 MusicManager）
    static void* getZipHandle() { return g_zip; }

private:
    static AAssetManager* g_assetManager;
    static std::string g_zipPath;
    static std::string g_vanillaRoot;
    static bool g_preferVanilla;
    // 该路径是否走「vanilla 优先」策略：仅官方结构类资源（blockstates/、models/、纹理）；
    // shaders/（自研 GLES 着色器，与原版同名文件语义不同）、worldgen/、lang/、
    // blocks.json/items.json/sounds.json 等项目专有文件始终保持服务器 ZIP 优先
    static bool vanillaFirstFor(const std::string& path);
    // 纹理来源：哈希资产库 / 服务器 ZIP / vanilla 目录，先后顺序由上述策略决定
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
    // 使用 shared_mutex 实现读写锁，多线程安全
    static std::unordered_map<std::string, TextureData> s_cache;
    static std::shared_mutex s_cacheMutex;
    static void clearCache();
};
