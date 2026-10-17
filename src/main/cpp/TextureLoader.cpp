#include "TextureLoader.h"
#include "AssetStore.h"
#include <android/log.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>
#include <unordered_set>

#define LOG_TAG "TextureLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 包含 stb_image
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// 包含 miniz（ZIP 读取支持，实现代码在 miniz.c 中）
#include "3rdparty/miniz/miniz.h"

AAssetManager* TextureLoader::g_assetManager = nullptr;
std::string TextureLoader::g_zipPath = "";
std::string TextureLoader::g_vanillaRoot = "";
bool TextureLoader::g_preferVanilla = true;
void* TextureLoader::g_zip = nullptr;
bool TextureLoader::g_zipOpen = false;
std::unordered_map<std::string, TextureData> TextureLoader::s_cache;
std::shared_mutex TextureLoader::s_cacheMutex;

void TextureLoader::setAssetManager(AAssetManager* assetManager) {
    g_assetManager = assetManager;
}

void TextureLoader::setZipPath(const std::string& path) {
    // 关闭之前的 ZIP
    if (g_zipOpen) {
        mz_zip_reader_end(static_cast<mz_zip_archive*>(g_zip));
        delete static_cast<mz_zip_archive*>(g_zip);
        g_zip = nullptr;
        g_zipOpen = false;
    }

    // 清理缓存（线程安全）
    {
        std::unique_lock<std::shared_mutex> lock(s_cacheMutex);
        s_cache.clear();
    }

    g_zipPath = path;
    LOGI("Texture ZIP path set to: %s", path.c_str());

    // 直接打开缓存
    if (!g_zipPath.empty()) {
        mz_zip_archive* zip = new mz_zip_archive();
        memset(zip, 0, sizeof(mz_zip_archive));
        if (mz_zip_reader_init_file(zip, g_zipPath.c_str(), 0)) {
            g_zip = zip;
            g_zipOpen = true;
            LOGI("ZIP opened and cached: %s", g_zipPath.c_str());
            // 列出 ZIP 前 20 个文件用于调试
            int numFiles = mz_zip_reader_get_num_files(zip);
            LOGI("ZIP contains %d files", numFiles);
        } else {
            delete zip;
            LOGE("Failed to open ZIP file: %s", g_zipPath.c_str());
        }
    }
}

void TextureLoader::setVanillaRoot(const std::string& path) {
    g_vanillaRoot = path;
    LOGI("Vanilla root set to: %s", path.c_str());
}

void TextureLoader::setPreferVanilla(bool prefer) {
    g_preferVanilla = prefer;
    LOGI("Resource source preference: %s", prefer ? "vanilla first" : "ZIP first");
}

// 仅官方结构类资源走「vanilla 优先」；worldgen/、lang/、blocks.json/items.json/sounds.json
// 等项目专有文件始终 ZIP 优先（着色器已内置在 assets/shaders/opengl/，资源包里的 shaders/ 不再读取）
// items/ = 26.x 的新版 item 定义（3D 物品栏图标的唯一来源），同属结构类
bool TextureLoader::vanillaFirstFor(const std::string& path) {
    static const char* kVanillaFirstPrefixes[] = {"blockstates/", "models/", "items/", "textures/"};
    for (const char* p : kVanillaFirstPrefixes) {
        std::string want(p);
        // 用 >= 而不是 >：path 恰好等于前缀时是按前缀的批量读取（如 "blockstates/"），同样要 vanilla 优先
        if (path.size() >= want.size() && path.compare(0, want.size(), want) == 0) {
            return true;
        }
    }
    return false;
}

void TextureLoader::closeZip() {
    // 清理缓存（线程安全）
    {
        std::unique_lock<std::shared_mutex> lock(s_cacheMutex);
        s_cache.clear();
    }

    if (g_zipOpen) {
        mz_zip_reader_end(static_cast<mz_zip_archive*>(g_zip));
        delete static_cast<mz_zip_archive*>(g_zip);
        g_zip = nullptr;
        g_zipOpen = false;
        LOGI("ZIP closed");
    }
}

void TextureLoader::clearCache() {
    // 线程安全地清理缓存
    std::unique_lock<std::shared_mutex> lock(s_cacheMutex);
    s_cache.clear();
    LOGI("Texture cache cleared");
}

// 纹理路径前缀规则：
// 已带路径如 "colormap/grass.png" → "textures/colormap/grass.png"
// 无路径如 "stone.png"         → "textures/block/stone.png"
std::string TextureLoader::normalizeTexturePath(const std::string& filename) {
    std::string prefix = "textures/";
    if (filename.find('/') == std::string::npos) {
        prefix += "block/";
    }
    return prefix + filename;
}

// PNG 字节解码（store/vanilla 两路来源共用）
TextureData TextureLoader::decodeFromBytes(const std::vector<uint8_t>& bytes) {
    TextureData result;
    if (bytes.empty()) return result;

    int width, height, channels;
    unsigned char* imageData = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                                     &width, &height, &channels, 4);
    if (!imageData) {
        LOGE("Failed to decode PNG: %s", stbi_failure_reason());
        return result;
    }

    result.width = width;
    result.height = height;
    result.channels = 4;
    result.data = new unsigned char[width * height * 4];
    std::memcpy(result.data, imageData, width * height * 4);
    stbi_image_free(imageData);
    return result;
}

// 哈希资产库读取 PNG（纹理第一来源）
TextureData TextureLoader::loadFromStore(const std::string& filename) {
    TextureData result;
    auto& store = AssetStore::getInstance();
    if (!store.isReady()) return result;

    std::vector<uint8_t> bytes;
    if (!store.readFile(normalizeTexturePath(filename), bytes)) {
        return result;
    }
    return decodeFromBytes(bytes);
}

// 服务器 ZIP 读取 PNG（定制 overlay 层，优先于官方资源；用户的资源包即按协议版本设计）
TextureData TextureLoader::loadFromZip(const std::string& filename) {
    TextureData result;
    if (!g_zipOpen || !g_zip) return result;

    mz_zip_archive* zip = static_cast<mz_zip_archive*>(g_zip);
    std::string entry = normalizeTexturePath(filename);
    int foundIndex = mz_zip_reader_locate_file(zip, entry.c_str(), nullptr, 0);
    if (foundIndex < 0) return result;

    size_t uncompSize = 0;
    unsigned char* data = static_cast<unsigned char*>(
        mz_zip_reader_extract_to_heap(zip, foundIndex, &uncompSize, 0));
    if (!data || uncompSize == 0) {
        if (data) mz_free(data);
        return result;
    }

    std::vector<uint8_t> bytes(data, data + uncompSize);
    mz_free(data);
    result = decodeFromBytes(bytes);
    if (!result.data) LOGE("Failed to load PNG from ZIP: %s", entry.c_str());
    return result;
}

// vanilla 目录读取 PNG（纹理兜底来源：最新正式版 jar 解包的原版资源）
TextureData TextureLoader::loadFromVanilla(const std::string& filename) {
    TextureData result;
    if (g_vanillaRoot.empty()) return result;

    std::string path = g_vanillaRoot + "/" + normalizeTexturePath(filename);
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return result;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes;
    if (size > 0) {
        bytes.resize(static_cast<size_t>(size));
        size_t got = fread(bytes.data(), 1, static_cast<size_t>(size), f);
        bytes.resize(got);
    }
    fclose(f);

    result = decodeFromBytes(bytes);
    if (!result.data) {
        LOGE("Failed to load PNG from vanilla: %s", path.c_str());
        return result;
    }

    // vanilla jar 对「真内容只存在于哈希资产库」的资源会塞一张 1x1 占位 PNG，
    // 主菜单全景图就是这种（jar 里 panorama_0..5/overlay 全是 1x1，真图 1024x1024 只在 index 的 objects 里）。
    // 占位不能算命中，否则 vanilla 优先会把哈希池的真图永远挡在后面（表现为全景变成纯色）。
    // 实测 26.3 的 textures 下恰好 1x1 的 PNG 只有这 7 张，palettes/trim 等小图是 8x1 不会被误判。
    if (result.width == 1 && result.height == 1) {
        LOGI("Skip vanilla 1x1 placeholder: %s", path.c_str());
        return TextureData();
    }
    return result;
}

// 文本类 vanilla 兜底：按相对路径直查目录（不存在返回空，不打日志）
std::string TextureLoader::readTextFromVanilla(const std::string& filename) {
    if (g_vanillaRoot.empty()) return "";
    std::string path = g_vanillaRoot + "/" + filename;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    std::string content;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, n);
    fclose(f);
    return content;
}

// 递归收集 vanilla 目录下匹配 relPrefix 的文本文件相对路径
static void scanVanillaPrefix(const std::string& absDir, const std::string& relDir,
                               const std::string& relPrefix, std::vector<std::string>& out) {
    DIR* d = opendir(absDir.c_str());
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string rel = relDir.empty() ? name : relDir + "/" + name;
        std::string abs = absDir + "/" + name;
        struct stat st;
        if (stat(abs.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scanVanillaPrefix(abs, rel, relPrefix, out);
        } else if (rel.size() > relPrefix.size() &&
                   rel.compare(0, relPrefix.size(), relPrefix) == 0) {
            out.push_back(rel);
        }
    }
    closedir(d);
}

// 服务器 ZIP 单文件读取（项目专有文件与定制 overlay）；entry 不存在返回空且不打日志
std::string TextureLoader::readTextFromZipFile(const std::string& filename) {
    if (!g_zipOpen || !g_zip) return "";

    mz_zip_archive* zip = static_cast<mz_zip_archive*>(g_zip);
    int foundIndex = mz_zip_reader_locate_file(zip, filename.c_str(), nullptr, 0);
    if (foundIndex < 0) return "";

    size_t uncompSize = 0;
    unsigned char* data = static_cast<unsigned char*>(
        mz_zip_reader_extract_to_heap(zip, foundIndex, &uncompSize, 0));
    if (!data || uncompSize == 0) {
        if (data) mz_free(data);
        LOGE("Failed to extract from ZIP: %s", filename.c_str());
        return "";
    }
    std::string result(reinterpret_cast<char*>(data), uncompSize);
    mz_free(data);
    return result;
}

std::string TextureLoader::readTextFromZip(const std::string& filename) {
    // 1) 哈希资产库优先（index 收录该路径即以库为准，读失败不回退其他来源）
    auto& store = AssetStore::getInstance();
    if (store.isReady() && !store.getObjectPath(filename).empty()) {
        std::string content;
        if (store.readText(filename, content)) {
            return content;
        }
        LOGE("Asset store object missing on disk: %s", filename.c_str());
        return "";
    }

    // 2) vanilla 目录与服务器 ZIP 按策略先后查找（任一命中即返回，另一来源仍作兜底）
    std::string first;
    std::string second;
    if (g_preferVanilla && vanillaFirstFor(filename)) {
        first = readTextFromVanilla(filename);
        second = readTextFromZipFile(filename);
    } else {
        first = readTextFromZipFile(filename);
        second = readTextFromVanilla(filename);
    }
    if (!first.empty()) return first;
    if (!second.empty()) return second;

    LOGE("File not found (ZIP/vanilla): %s", filename.c_str());
    return "";
}

std::vector<std::pair<std::string, std::string>> TextureLoader::readAllTextFromZip(const std::string& prefix) {
    std::vector<std::pair<std::string, std::string>> results;

    // 1) 哈希资产库优先：按 index 前缀枚举（结果已排序，保证加载顺序稳定）
    auto& store = AssetStore::getInstance();
    if (store.isReady()) {
        std::vector<std::string> paths = store.enumeratePrefix(prefix);
        if (!paths.empty()) {
            results.reserve(paths.size());
            for (const std::string& path : paths) {
                std::string content;
                if (store.readText(path, content)) {
                    results.emplace_back(path, std::move(content));
                } else {
                    LOGE("Asset store object missing on disk: %s", path.c_str());
                }
            }
            LOGI("Batch read %zu files from asset store prefix: %s", results.size(), prefix.c_str());
            return results;
        }
    }

    // 2) 两个来源的收集器：vanilla 目录（目录递归，排序保证加载顺序稳定）与服务器 ZIP
    auto collectVanilla = [&]() {
        if (g_vanillaRoot.empty()) return;
        std::vector<std::string> paths;
        scanVanillaPrefix(g_vanillaRoot, "", prefix, paths);
        std::sort(paths.begin(), paths.end());
        for (const std::string& rel : paths) {
            std::string content = readTextFromVanilla(rel);
            if (!content.empty()) {
                results.emplace_back(rel, std::move(content));
            }
        }
    };

    auto collectZip = [&]() {
        if (!g_zipOpen || !g_zip) return;
        mz_zip_archive* zip = static_cast<mz_zip_archive*>(g_zip);
        int numFiles = mz_zip_reader_get_num_files(zip);

        // 已有同路径内容（另一来源）则跳过，保证一个 entry 只出现一次
        std::unordered_set<std::string> seen;
        seen.reserve(results.size() * 2);
        for (const auto& entry : results) seen.insert(entry.first);

        for (int i = 0; i < numFiles; i++) {
            char nameBuf[256];
            mz_uint nameLen = mz_zip_reader_get_filename(zip, i, nameBuf, sizeof(nameBuf));
            if (nameLen == 0) continue;

            std::string entryName(nameBuf, nameLen);
            // 检查是否匹配前缀
            if (entryName.size() <= prefix.size() ||
                entryName.substr(0, prefix.size()) != prefix) {
                continue;
            }
            if (seen.count(entryName) != 0) continue;

            size_t uncompSize = 0;
            unsigned char* data = static_cast<unsigned char*>(
                mz_zip_reader_extract_to_heap(zip, i, &uncompSize, 0));
            if (!data || uncompSize == 0) continue;

            std::string content(reinterpret_cast<char*>(data), uncompSize);
            mz_free(data);

            results.emplace_back(std::move(entryName), std::move(content));
        }
    };

    // vanilla 优先时合并两源：新版缺失的旧名资源（如 grass 改名遗留）由 ZIP 补齐；
    // 其余前缀保持原语义：ZIP 命中即止，未命中才扫 vanilla
    if (g_preferVanilla && vanillaFirstFor(prefix)) {
        collectVanilla();
        collectZip();
        LOGI("Batch read %zu files for prefix %s (vanilla first, merged)",
             results.size(), prefix.c_str());
    } else {
        collectZip();
        if (results.empty()) collectVanilla();
        LOGI("Batch read %zu files for prefix %s (ZIP first)", results.size(), prefix.c_str());
    }
    return results;
}

TextureData TextureLoader::loadPNG(const std::string& filename) {
    // 先尝试读锁查找缓存
    {
        std::shared_lock<std::shared_mutex> readLock(s_cacheMutex);
        auto it = s_cache.find(filename);
        if (it != s_cache.end()) {
            return it->second.clone();
        }
    }

    // 未命中缓存：按优先级策略在三个来源间查找，全部落空才报错
    // （保留逐级回退是为了新版改名/删除的纹理不会落到占位色上）
    TextureData result;
    if (g_preferVanilla) {
        result = loadFromVanilla(filename);
        if (!result.data) result = loadFromStore(filename);
        if (!result.data) result = loadFromZip(filename);
    } else {
        result = loadFromZip(filename);
        if (!result.data) result = loadFromStore(filename);
        if (!result.data) result = loadFromVanilla(filename);
    }

    if (!result.data) {
        LOGE("Failed to load texture: %s", filename.c_str());
        return result;
    }

    // 加载成功，写入缓存（写锁）
    {
        std::unique_lock<std::shared_mutex> writeLock(s_cacheMutex);
        s_cache.emplace(filename, std::move(result));
        LOGI("Cached texture: %s", filename.c_str());
        return s_cache[filename].clone();
    }
}

TextureData TextureLoader::loadImage(const std::string& filename) {

    // 根据文件扩展名选择加载方式
    if (filename.size() >= 4) {
        std::string ext = filename.substr(filename.size() - 4);

        if (ext == ".png") {
            return loadPNG(filename);
        }
    }

    LOGE("Unsupported image format or invalid filename: %s", filename.c_str());
    return TextureData();
}
