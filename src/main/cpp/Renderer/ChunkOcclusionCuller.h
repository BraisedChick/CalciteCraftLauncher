#pragma once

// ===== Section 级 BFS 遮挡剔除（cave culling，图形 API 无关，GL/Vulkan 后端共用）=====
// 消费 ChunkMeshScheduler 产出的 section 内部连通性掩码 visibilityData
//   （bit(from*8+to)=1 表示 from 面→to 面在 section 内部可达，
//    方向 0=DOWN 1=UP 2=NORTH 3=SOUTH 4=WEST 5=EAST）。
// 从相机所在 section 起 BFS：
//   - 外向约束（Sodium 风格）：每轴只向远离相机方向扩展，天然无环、必然终止；
//   - 连通查询：经某面进入的 section 只能从连通的面继续外扩，实心 section（vis=0）挡死；
//   - 缓存中不存在的 section（纯空气/未加载）按六向全通处理，避免误挡；
//   - 相机在世界高度之外（高空飞行/地底）时用世界顶/底一整层做种子兜底，
//     这是历史 GL 版 BFS "高空区块消失" 翻车的缺失点，必须保留。
// 低频触发：相机跨 section 或缓存变动才重算，重算结果缓存为可见 section 集合，
// 渲染时按 section key 查表即可（与视锥剔除叠加过滤）。

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>

class ChunkOcclusionCuller {
public:
    // section 网格坐标打包：sx=chunkX, sz=chunkZ, sy=(sectionY>>4)（section 层索引，可负）
    // 各 21 位（低位掩码，两补码）：仅当坐标相差 2^20 区块（远超世界边界）才会碰撞
    static uint64_t sectionKey(int sx, int sz, int sy) {
        return ((uint64_t)(sx & 0x1FFFFF) << 42) |
               ((uint64_t)(sz & 0x1FFFFF) << 21) |
               (uint64_t)(sy & 0x1FFFFF);
    }

    // 重算可见集合（渲染线程调用；GL 侧需持 cacheMutex）。
    // camX/Y/Z：相机世界坐标；worldMinY/worldMaxY：维度高度范围（maxY 为上界不含）；
    // renderDist：渲染距离（方块，= 区块数*16），用于限定 BFS 扩展半径；
    // cacheDirty：本帧渲染缓存是否发生增删（true 时强制重算）；
    // cullEnabled：是否启用遮挡剔除。false 时进入 bypass（不跑 BFS，isVisible 恒 true，
    //   只保留视锥剔除）——对齐原版 LevelRenderer：旁观者相机嵌入实心方块穿地时关闭
    //   连通性剔除，避免地下洞穴被误剔除消失。
    // buildSections：懒构建回调，仅在真正重算时被调用，填入 {sectionKey → visibilityData}。
    // 返回 true 表示本次重算，false 表示复用上次结果。
    bool update(float camX, float camY, float camZ,
                int worldMinY, int worldMaxY, float renderDist, bool cacheDirty, bool cullEnabled,
                const std::function<void(std::unordered_map<uint64_t, uint64_t>&)>& buildSections);

    bool hasResult() const { return computed; }
    bool isVisible(uint64_t key) const {
        if (bypass) return true;  // 相机嵌实心方块：关闭遮挡，仅视锥剔除生效
        return visibleSet.find(key) != visibleSet.end();
    }

private:
    void compute(float camX, float camY, float camZ,
                 int worldMinY, int worldMaxY, float renderDist,
                 const std::unordered_map<uint64_t, uint64_t>& sections);

    std::unordered_set<uint64_t> visibleSet;
    bool computed = false;
    bool bypass = false;  // 遮挡剔除是否被临时关闭（相机嵌实心方块）
    int lastSecX = 0x7fffffff, lastSecY = 0x7fffffff, lastSecZ = 0x7fffffff;
};
