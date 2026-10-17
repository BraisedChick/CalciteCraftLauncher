#include "AssetStore.h"
#include "3rdparty/json.hpp"
#include <android/log.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#define LOG_TAG "AssetStore"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using json = nlohmann::json;

AssetStore& AssetStore::getInstance() {
    static AssetStore instance;
    return instance;
}

bool AssetStore::setRoot(const std::string& rootPath) {
    // 启动期由 JNI 线程在渲染/工作线程启动前调用，运行期只读
    ready.store(false, std::memory_order_release);
    root = rootPath;
    objects.clear();
    if (root.empty() || !loadIndex()) {
        LOGI("Asset store not ready (root=%s)", root.c_str());
        return false;
    }
    ready.store(true, std::memory_order_release);
    LOGI("Asset store ready: root=%s objects=%zu", root.c_str(), objects.size());
    return true;
}

bool AssetStore::loadIndex() {
    // 1) current_index.txt 记录当前使用的 assetId（如 "1.18"）
    std::ifstream idFile(root + "/current_index.txt");
    if (!idFile) return false;
    std::string assetId;
    std::getline(idFile, assetId);
    idFile.close();
    // 去除行尾空白
    while (!assetId.empty() && (assetId.back() == '\r' || assetId.back() == '\n' ||
                                 assetId.back() == ' ' || assetId.back() == '\t')) {
        assetId.pop_back();
    }
    if (assetId.empty()) return false;

    // 2) 解析 indexes/<assetId>.json
    std::string indexPath = root + "/indexes/" + assetId + ".json";
    std::ifstream indexFile(indexPath, std::ios::binary);
    if (!indexFile) {
        LOGE("Index file not found: %s", indexPath.c_str());
        return false;
    }
    json index;
    try {
        index = json::parse(indexFile);
    } catch (const std::exception& e) {
        LOGE("Failed to parse asset index %s: %s", indexPath.c_str(), e.what());
        return false;
    }
    if (!index.is_object() || !index.contains("objects") || !index["objects"].is_object()) {
        LOGE("Asset index malformed (no objects map): %s", indexPath.c_str());
        return false;
    }

    // 3) 建 path → hash 映射（key 去除命名空间前缀：
    //    1.18 等 index 实际用 "minecraft/"（斜杠），新版格式用 "minecraft:"（冒号），两种都剥）
    static const std::string nsSlash = "minecraft/";
    static const std::string nsColon = "minecraft:";
    for (auto it = index["objects"].begin(); it != index["objects"].end(); ++it) {
        const json& obj = it.value();
        if (!obj.is_object() || !obj.contains("hash") || !obj["hash"].is_string()) continue;
        std::string path = it.key();
        if (path.size() > nsSlash.size() && path.compare(0, nsSlash.size(), nsSlash) == 0) {
            path.erase(0, nsSlash.size());
        } else if (path.size() > nsColon.size() && path.compare(0, nsColon.size(), nsColon) == 0) {
            path.erase(0, nsColon.size());
        }
        objects.emplace(std::move(path), obj["hash"].get<std::string>());
    }
    LOGI("Loaded asset index '%s' (%zu objects)", assetId.c_str(), objects.size());
    return !objects.empty();
}

std::string AssetStore::getObjectPath(const std::string& path) const {
    if (!ready.load(std::memory_order_acquire)) return "";
    auto it = objects.find(path);
    if (it == objects.end()) return "";
    const std::string& hash = it->second;
    if (hash.size() < 2) return "";
    // objects/<2hex>/<hash>
    return root + "/objects/" + hash.substr(0, 2) + "/" + hash;
}

bool AssetStore::readFile(const std::string& path, std::vector<uint8_t>& out) const {
    std::string objPath = getObjectPath(path);
    if (objPath.empty()) return false;
    std::ifstream f(objPath, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamsize size = f.tellg();
    if (size <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (!f.read(reinterpret_cast<char*>(out.data()), size)) return false;
    return true;
}

bool AssetStore::readText(const std::string& path, std::string& out) const {
    std::string objPath = getObjectPath(path);
    if (objPath.empty()) return false;
    std::ifstream f(objPath, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return !out.empty();
}

std::vector<std::string> AssetStore::enumeratePrefix(const std::string& prefix) const {
    std::vector<std::string> result;
    if (!ready.load(std::memory_order_acquire)) return result;
    for (const auto& pair : objects) {
        if (pair.first.size() > prefix.size() &&
            pair.first.compare(0, prefix.size(), prefix) == 0) {
            result.push_back(pair.first);
        }
    }
    // 哈希遍历顺序不定，排序保证批量加载结果稳定
    std::sort(result.begin(), result.end());
    return result;
}
