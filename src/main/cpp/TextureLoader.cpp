#include "TextureLoader.h"
#include "AssetStore.h"
#include <android/log.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>

#define LOG_TAG "TextureLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "3rdparty/miniz/miniz.h"

AAssetManager* TextureLoader::g_assetManager = nullptr;
std::string TextureLoader::g_zipPath = "";
std::string TextureLoader::g_vanillaRoot = "";
void* TextureLoader::g_zip = nullptr;
bool TextureLoader::g_zipOpen = false;
std::unordered_map<std::string, TextureData> TextureLoader::s_cache;
std::shared_mutex TextureLoader::s_cacheMutex;

void TextureLoader::setAssetManager(AAssetManager* assetManager) {
    g_assetManager = assetManager;
}

void TextureLoader::setZipPath(const std::string& path) {
    if (g_zipOpen) {
        mz_zip_reader_end(static_cast<mz_zip_archive*>(g_zip));
        delete static_cast<mz_zip_archive*>(g_zip);
        g_zip = nullptr;
        g_zipOpen = false;
    }

    {
        std::unique_lock<std::shared_mutex> lock(s_cacheMutex);
        s_cache.clear();
    }

    g_zipPath = path;
    LOGI("Texture ZIP path set to: %s", path.c_str());

    if (!g_zipPath.empty()) {
        mz_zip_archive* zip = new mz_zip_archive();
        memset(zip, 0, sizeof(mz_zip_archive));
        if (mz_zip_reader_init_file(zip, g_zipPath.c_str(), 0)) {
            g_zip = zip;
            g_zipOpen = true;
            LOGI("ZIP opened and cached: %s", g_zipPath.c_str());
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

namespace {
// 用 >= 而不是 >：path 恰好等于前缀时是按目录的批量读取，同样要归类
bool hasPrefix(const std::string& path, const char* want) {
    std::size_t n = std::strlen(want);
    return path.size() >= n && path.compare(0, n, want) == 0;
}
} // namespace

// 资源来源路由：每类资源只有一个权威来源，不做跨源回退。依据是三个来源的实测内容：
//  - index 只列 jar 之外的资源（1.18 共 3071 条）：sounds/ ogg、lang/、
//    textures/gui/title/background/panorama_* 7 张（jar 里同路径是 1x1 占位）、icons/；
//    不含任何 blockstates/models/items JSON
//  - 服务器 ZIP 只有 blocks.json、items.json、worldgen/**
//  - vanilla 目录（最新正式版 jar 的 assets/minecraft）= 纹理、blockstates/、models/、
//    items/、font/ 等其余一切
// 自研 GLES 着色器不走这里，在 APK 的 assets/shaders/opengl/，由 AAssetManager 读。
TextureLoader::Source TextureLoader::routeFor(const std::string& path) {
    static const char* kIndexPaths[] = {
        "sounds/", "lang/", "textures/gui/title/background/panorama_",
    };
    for (const char* p : kIndexPaths) {
        if (hasPrefix(path, p)) return Source::Index;
    }

    static const char* kZipPaths[] = {"blocks.json", "items.json", "worldgen/"};
    for (const char* p : kZipPaths) {
        if (hasPrefix(path, p)) return Source::Zip;
    }

    return Source::Vanilla;
}

const char* TextureLoader::sourceName(Source source) {
    switch (source) {
        case Source::Index:   return "index";
        case Source::Zip:     return "zip";
        case Source::Vanilla: return "vanilla";
    }
    return "unknown";
}

void TextureLoader::closeZip() {
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
    std::unique_lock<std::shared_mutex> lock(s_cacheMutex);
    s_cache.clear();
    LOGI("Texture cache cleared");
}

// 裸文件名补 block/ 段，统一再加 textures/ 前缀：
// "colormap/grass.png" → "textures/colormap/grass.png"，"stone.png" → "textures/block/stone.png"
std::string TextureLoader::normalizeTexturePath(const std::string& filename) {
    std::string prefix = "textures/";
    if (filename.find('/') == std::string::npos) {
        prefix += "block/";
    }
    return prefix + filename;
}

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
    return result;
}

// 按相对路径直查 vanilla 目录（不存在返回空，不打日志）
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

// relDir 是否可能包含 relPrefix 下的文件（只沿祖先链下钻，兄弟目录整棵跳过）
static bool couldContain(const std::string& relDir, const std::string& relPrefix) {
    if (relDir.empty()) return true;
    if (relPrefix.size() > relDir.size() &&
        relPrefix.compare(0, relDir.size(), relDir) == 0 &&
        relPrefix[relDir.size()] == '/') {
        return true;
    }
    return relDir.size() > relPrefix.size() &&
           relDir.compare(0, relPrefix.size(), relPrefix) == 0;
}

// 递归收集 vanilla 目录下匹配 relPrefix 的文本文件相对路径
static void scanVanillaPrefix(const std::string& absDir, const std::string& relDir,
                               const std::string& relPrefix, std::vector<std::string>& out) {
    if (!couldContain(relDir, relPrefix)) return;
    DIR* d = opendir(absDir.c_str());
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string rel = relDir.empty() ? name : relDir + "/" + name;
        if (!couldContain(rel, relPrefix)) continue;
        std::string abs = absDir + "/" + name;
        // 优先用 d_type，DT_UNKNOWN 时才 stat
        bool isDir;
        if (e->d_type != DT_UNKNOWN) {
            isDir = (e->d_type == DT_DIR);
        } else {
            struct stat st;
            if (stat(abs.c_str(), &st) != 0) continue;
            isDir = S_ISDIR(st.st_mode);
        }
        if (isDir) {
            scanVanillaPrefix(abs, rel, relPrefix, out);
        } else if (rel.size() > relPrefix.size() &&
                   rel.compare(0, relPrefix.size(), relPrefix) == 0) {
            out.push_back(rel);
        }
    }
    closedir(d);
}

// 读 ZIP 单个 entry（不存在返回空，不打日志）
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
    // 名字保留历史叫法，实际按 routeFor 读单个文本文件
    switch (routeFor(filename)) {
        case Source::Index: {
            std::string content;
            if (AssetStore::getInstance().readText(filename, content)) return content;
            LOGE("Not in asset index / object missing on disk: %s", filename.c_str());
            return "";
        }
        case Source::Zip: {
            std::string content = readTextFromZipFile(filename);
            if (!content.empty()) return content;
            LOGE("Not in server resourcepack ZIP: %s", filename.c_str());
            return "";
        }
        case Source::Vanilla: {
            std::string content = readTextFromVanilla(filename);
            if (!content.empty()) return content;
            LOGE("Not in vanilla directory: %s", filename.c_str());
            return "";
        }
    }
    return "";
}

std::vector<std::pair<std::string, std::string>> TextureLoader::readAllTextFromZip(const std::string& prefix) {
    std::vector<std::pair<std::string, std::string>> results;
    // 来源由 routeFor(prefix) 唯一决定，不跨源补漏
    switch (routeFor(prefix)) {
        case Source::Vanilla: {
            if (g_vanillaRoot.empty()) break;
            std::vector<std::string> paths;
            scanVanillaPrefix(g_vanillaRoot, "", prefix, paths);
            std::sort(paths.begin(), paths.end());   // 保证加载顺序稳定
            results.reserve(paths.size());
            for (const std::string& rel : paths) {
                std::string content = readTextFromVanilla(rel);
                if (!content.empty()) {
                    results.emplace_back(rel, std::move(content));
                } else {
                    LOGE("Empty file in vanilla directory: %s", rel.c_str());
                }
            }
            break;
        }

        case Source::Zip: {
            if (!g_zipOpen || !g_zip) break;
            mz_zip_archive* zip = static_cast<mz_zip_archive*>(g_zip);
            int numFiles = mz_zip_reader_get_num_files(zip);
            for (int i = 0; i < numFiles; i++) {
                char nameBuf[256];
                mz_uint nameLen = mz_zip_reader_get_filename(zip, i, nameBuf, sizeof(nameBuf));
                if (nameLen == 0) continue;

                std::string entryName(nameBuf, nameLen);
                // 目录 entry（长度不大于前缀）与不匹配前缀的 entry 都跳过
                if (entryName.size() <= prefix.size() ||
                    entryName.compare(0, prefix.size(), prefix) != 0) {
                    continue;
                }

                size_t uncompSize = 0;
                unsigned char* data = static_cast<unsigned char*>(
                    mz_zip_reader_extract_to_heap(zip, i, &uncompSize, 0));
                if (!data || uncompSize == 0) continue;

                std::string content(reinterpret_cast<char*>(data), uncompSize);
                mz_free(data);
                results.emplace_back(std::move(entryName), std::move(content));
            }
            break;
        }

        case Source::Index: {
            auto& store = AssetStore::getInstance();
            if (!store.isReady()) break;
            for (const std::string& path : store.enumeratePrefix(prefix)) {
                std::string content;
                if (store.readText(path, content)) {
                    results.emplace_back(path, std::move(content));
                } else {
                    LOGE("Asset store object missing on disk: %s", path.c_str());
                }
            }
            break;
        }
    }

    LOGI("Batch read %zu files for prefix %s (source=%s)",
         results.size(), prefix.c_str(), sourceName(routeFor(prefix)));
    return results;
}

TextureData TextureLoader::loadPNG(const std::string& filename) {
    {
        std::shared_lock<std::shared_mutex> readLock(s_cacheMutex);
        auto it = s_cache.find(filename);
        if (it != s_cache.end()) {
            return it->second.clone();
        }
    }

    // 只查 routeFor 指定的那一个来源（纹理只会落到 Vanilla / Index 两支）
    const std::string normalized = normalizeTexturePath(filename);
    const Source source = routeFor(normalized);
    TextureData result;
    switch (source) {
        case Source::Index:   result = loadFromStore(filename); break;
        case Source::Zip:     result = loadFromZip(filename); break;
        case Source::Vanilla: result = loadFromVanilla(filename); break;
    }

    if (!result.data) {
        LOGE("Failed to load texture from %s: %s",
             sourceName(source), normalized.c_str());
        return result;
    }

    {
        std::unique_lock<std::shared_mutex> writeLock(s_cacheMutex);
        s_cache.emplace(filename, std::move(result));
        LOGI("Cached texture: %s", filename.c_str());
        return s_cache[filename].clone();
    }
}

TextureData TextureLoader::loadImage(const std::string& filename) {
    if (filename.size() >= 4) {
        std::string ext = filename.substr(filename.size() - 4);

        if (ext == ".png") {
            return loadPNG(filename);
        }
    }

    LOGE("Unsupported image format or invalid filename: %s", filename.c_str());
    return TextureData();
}
