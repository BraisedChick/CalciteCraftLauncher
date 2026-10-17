#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>

// 原版风格哈希资产库（Mojang assets 体系）
// 目录布局（root = Java 侧 getExternalFilesDir("assets")）：
//   <root>/current_index.txt      单行 assetId（Java 同步完成后写入）
//   <root>/indexes/<assetId>.json 官方资产索引（objects: "minecraft:<path>" → {hash,size}）
//   <root>/objects/<hash前2位>/<hash> 内容寻址对象文件
// 纯逻辑层：只读文件，不接触任何图形 API。库就绪后 TextureLoader/MusicManager
// 优先从这里按 path→hash 读取；未就绪（未下载/无 index）时 isReady()=false，
// 调用方回退旧 resourcepack ZIP 逻辑。
class AssetStore {
public:
    static AssetStore& getInstance();

    // 设置库根目录并尝试加载 index。任何时候可调用（幂等可重载）；
    // 加载失败（目录不存在/current_index.txt 缺失/JSON 损坏）时库保持未就绪。
    bool setRoot(const std::string& rootPath);

    bool isReady() const { return ready.load(std::memory_order_acquire); }
    const std::string& getRoot() const { return root; }

    // path 为 assets/minecraft 相对路径（如 "textures/block/stone.png"）。
    // 命中返回对象文件绝对路径，未命中返回空串。
    std::string getObjectPath(const std::string& path) const;

    // 读取对象文件全文（二进制安全）。成功返回 true。
    bool readFile(const std::string& path, std::vector<uint8_t>& out) const;
    bool readText(const std::string& path, std::string& out) const;

    // 枚举 index 中所有以 prefix 开头的路径（如 "blockstates/"）
    std::vector<std::string> enumeratePrefix(const std::string& prefix) const;

    size_t getObjectCount() const { return objects.size(); }

private:
    AssetStore() = default;
    bool loadIndex();

    std::string root;
    // path（已去 "minecraft:" 前缀）→ sha1 hex
    std::unordered_map<std::string, std::string> objects;
    std::atomic<bool> ready{false};
};
