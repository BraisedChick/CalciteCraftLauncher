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
// 纯逻辑层：只读文件，不接触任何图形 API。SAX 流式提取，只保留 path→对象绝对路径 一张表。
// 只服务三类资源（与 TextureLoader::routeFor 对应）：音频 ogg、语言表 lang/、
// 主菜单全景图 panorama_*（jar 里同路径是 1x1 占位，真图只在此库）。
// 未就绪时 isReady()=false，这三类直接不可用（单源，不做跨源回退）。
class AssetStore {
public:
    static AssetStore& getInstance();

    // 设置库根目录并加载 index（幂等可重载）；失败时库保持未就绪
    bool setRoot(const std::string& rootPath);

    bool isReady() const { return ready.load(std::memory_order_acquire); }
    const std::string& getRoot() const { return root; }

    // path 为 assets/minecraft 相对路径（如 "textures/block/stone.png"），未命中返回空串
    std::string getObjectPath(const std::string& path) const;

    // 读取对象文件全文（二进制安全）。成功返回 true。
    bool readFile(const std::string& path, std::vector<uint8_t>& out) const;
    bool readText(const std::string& path, std::string& out) const;

    std::vector<std::string> enumeratePrefix(const std::string& prefix) const;

    size_t getObjectCount() const { return objectPaths.size(); }

private:
    AssetStore() = default;
    bool loadIndex();

    std::string root;
    // path（已去 "minecraft:" 前缀）→ <root>/objects/<hash前2位>/<hash> 绝对路径
    std::unordered_map<std::string, std::string> objectPaths;
    std::atomic<bool> ready{false};
};
