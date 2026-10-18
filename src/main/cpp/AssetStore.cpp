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
    objectPaths.clear();
    if (root.empty() || !loadIndex()) {
        LOGI("Asset store not ready (root=%s)", root.c_str());
        return false;
    }
    ready.store(true, std::memory_order_release);
    LOGI("Asset store ready: root=%s objects=%zu", root.c_str(), objectPaths.size());
    return true;
}

namespace {

// asset index 流式提取器：只取每个条目的 "hash"，边解析边拼出对象绝对路径，全程不建 DOM。
// 事件深度：depth1 根对象，depth2 objects 对象，depth3 单个资产条目（字段键值对都在 depth3 上报）
class IndexObjectPathSax : public nlohmann::json_sax<json> {
public:
    IndexObjectPathSax(const std::string& rootPath,
                       std::unordered_map<std::string, std::string>& out)
        : objectsDir(rootPath + "/objects/"), objectPaths(out) {}

    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool binary(binary_t&) override { return true; }

    bool key(string_t& val) override {
        if (depth == 1) {
            inObjects = (val == "objects");
        } else if (inObjects) {
            if (depth == 2) currentPath = val;
            else if (depth == 3) pendingKey = std::move(val);
        }
        return true;
    }

    bool string(string_t& val) override {
        if (inObjects && depth == 3 && pendingKey == "hash") {
            hash = std::move(val);
            hasHash = true;
        }
        return true;
    }

    bool start_object(std::size_t) override {
        depth++;
        if (inObjects && depth == 3) {
            hash.clear();
            hasHash = false;
        }
        return true;
    }

    bool end_object() override {
        if (inObjects) {
            if (depth == 3) {
                if (hasHash) store(currentPath, hash);
            } else if (depth == 2) {
                inObjects = false;
            }
        }
        if (depth > 0) depth--;
        return true;
    }

    bool start_array(std::size_t) override { depth++; return true; }
    bool end_array() override { if (depth > 0) depth--; return true; }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }

private:
    // 剥掉命名空间前缀（老 index 用 "minecraft/"，新版用 "minecraft:"）
    void store(const std::string& path, const std::string& sha1) {
        static const std::string nsSlash = "minecraft/";
        static const std::string nsColon = "minecraft:";
        std::string key = path;
        if (key.size() > nsSlash.size() && key.compare(0, nsSlash.size(), nsSlash) == 0) {
            key.erase(0, nsSlash.size());
        } else if (key.size() > nsColon.size() && key.compare(0, nsColon.size(), nsColon) == 0) {
            key.erase(0, nsColon.size());
        }
        if (sha1.size() < 2 || key.empty()) return;
        objectPaths.emplace(std::move(key), objectsDir + sha1.substr(0, 2) + "/" + sha1);
    }

    std::string objectsDir;
    std::unordered_map<std::string, std::string>& objectPaths;
    std::size_t depth = 0;
    bool inObjects = false;
    bool hasHash = false;
    std::string pendingKey;
    std::string currentPath;
    std::string hash;
};

} // namespace

bool AssetStore::loadIndex() {
    std::ifstream idFile(root + "/current_index.txt");   // 单行 assetId
    if (!idFile) return false;
    std::string assetId;
    std::getline(idFile, assetId);
    idFile.close();
    while (!assetId.empty() && (assetId.back() == '\r' || assetId.back() == '\n' ||
                                 assetId.back() == ' ' || assetId.back() == '\t')) {
        assetId.pop_back();
    }
    if (assetId.empty()) return false;

    std::string indexPath = root + "/indexes/" + assetId + ".json";
    std::ifstream indexFile(indexPath, std::ios::binary);
    if (!indexFile) {
        LOGE("Index file not found: %s", indexPath.c_str());
        return false;
    }
    IndexObjectPathSax handler(root, objectPaths);
    if (!json::sax_parse(indexFile, &handler)) {
        LOGE("Failed to parse asset index %s", indexPath.c_str());
        return false;
    }
    LOGI("Loaded asset index '%s' (%zu objects)", assetId.c_str(), objectPaths.size());
    return !objectPaths.empty();
}

std::string AssetStore::getObjectPath(const std::string& path) const {
    if (!ready.load(std::memory_order_acquire)) return "";
    auto it = objectPaths.find(path);
    if (it == objectPaths.end()) return "";
    return it->second;
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
    for (const auto& pair : objectPaths) {
        if (pair.first.size() > prefix.size() &&
            pair.first.compare(0, prefix.size(), prefix) == 0) {
            result.push_back(pair.first);
        }
    }
    // 哈希表顺序不定，排序保证批量加载结果稳定
    std::sort(result.begin(), result.end());
    return result;
}
