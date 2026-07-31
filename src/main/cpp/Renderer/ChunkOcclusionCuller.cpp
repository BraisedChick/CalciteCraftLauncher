#include "ChunkOcclusionCuller.h"

#include <cmath>
#include <queue>
#include <vector>

namespace {
// 方向枚举 0=DOWN 1=UP 2=NORTH 3=SOUTH 4=WEST 5=EAST
// 步进向量（section 网格单位）：{dx, dy, dz}
constexpr int STEP[6][3] = {
    {0, -1, 0},  // DOWN
    {0, 1, 0},   // UP
    {0, 0, -1},  // NORTH
    {0, 0, 1},   // SOUTH
    {-1, 0, 0},  // WEST
    {1, 0, 0},   // EAST
};

// 外向约束：方向 d 是否让 section 远离相机（每轴单调，保证 BFS 无环、必然终止）
// 相机所在轴向位置相等时两向都放行（相机平面上可双向扩展）
inline bool outwardAllowed(int d, int sx, int sz, int sy, int cx, int cz, int cy) {
    switch (d) {
        case 0: return sy <= cy;  // DOWN：位于相机层或更低才可继续向下
        case 1: return sy >= cy;  // UP
        case 2: return sz <= cz;  // NORTH
        case 3: return sz >= cz;  // SOUTH
        case 4: return sx <= cx;  // WEST
        case 5: return sx >= cx;  // EAST
    }
    return false;
}
}  // namespace

bool ChunkOcclusionCuller::update(
    float camX, float camY, float camZ,
    int worldMinY, int worldMaxY, float renderDist, bool cacheDirty, bool cullEnabled,
    const std::function<void(std::unordered_map<uint64_t, uint64_t>&)>& buildSections) {
    int secX = (int)std::floor(camX / 16.0f);
    int secZ = (int)std::floor(camZ / 16.0f);
    int secY = (int)std::floor(camY / 16.0f);

    // 遮挡剔除被关闭（旁观者相机嵌实心方块穿地）：进入 bypass，isVisible 恒 true，
    // 不跑 BFS 省开销，仅保留视锥剔除。返回值仅在 bypass 状态切换时算作"变化"。
    if (!cullEnabled) {
        bool wasBypass = bypass;
        bypass = true;
        computed = true;
        lastSecX = secX;
        lastSecY = secY;
        lastSecZ = secZ;
        return !wasBypass;
    }

    // 相机未跨 section 且缓存未变动、且上帧非 bypass：复用上次结果，避免每帧全量 BFS
    if (computed && !bypass && !cacheDirty && secX == lastSecX && secY == lastSecY && secZ == lastSecZ) {
        return false;
    }
    bypass = false;

    std::unordered_map<uint64_t, uint64_t> sections;
    buildSections(sections);
    compute(camX, camY, camZ, worldMinY, worldMaxY, renderDist, sections);

    lastSecX = secX;
    lastSecY = secY;
    lastSecZ = secZ;
    computed = true;
    return true;
}

void ChunkOcclusionCuller::compute(
    float camX, float camY, float camZ,
    int worldMinY, int worldMaxY, float renderDist,
    const std::unordered_map<uint64_t, uint64_t>& sections) {
    visibleSet.clear();

    // section 层索引范围（sy = sectionY >> 4）：底层含 worldMinY，顶层最后一段
    const int syMin = worldMinY >> 4;
    const int syMax = (worldMaxY >> 4) - 1;
    if (syMax < syMin) return;

    const int camChunkX = (int)std::floor(camX / 16.0f);
    const int camChunkZ = (int)std::floor(camZ / 16.0f);
    const int camF = (int)std::floor(camY / 16.0f);
    // 扩展半径（区块）：略大于渲染距离，覆盖全部已加载区块即可
    const int rChunks = (int)(renderDist / 16.0f) + 1;

    struct Node { int sx, sz, sy, entryFace; };  // entryFace=-1 表示种子（无进入面约束）
    std::queue<Node> q;
    std::unordered_map<uint64_t, uint8_t> processed;  // key → 已处理进入面位集（bit6=种子哨兵）

    // 外向约束用的相机层坐标：相机在世界外时置于世界之外一层，使扩展只朝世界内单向
    int camOutY = camF;
    if (camF < syMin) camOutY = syMin - 1;
    else if (camF > syMax) camOutY = syMax + 1;

    auto seed = [&](int sx, int sz, int sy) {
        uint64_t k = sectionKey(sx, sz, sy);
        processed[k] |= 0x40;  // 种子哨兵
        visibleSet.insert(k);
        q.push({sx, sz, sy, -1});
    };

    if (camF >= syMin && camF <= syMax) {
        // 相机在世界高度内：单 section 种子
        seed(camChunkX, camChunkZ, camF);
    } else {
        // 相机在世界外（高空/地底）：世界顶/底一整层做种子兜底
        int layer = (camF > syMax) ? syMax : syMin;
        for (int dx = -rChunks; dx <= rChunks; dx++) {
            for (int dz = -rChunks; dz <= rChunks; dz++) {
                seed(camChunkX + dx, camChunkZ + dz, layer);
            }
        }
    }

    while (!q.empty()) {
        Node n = q.front();
        q.pop();
        uint64_t curKey = sectionKey(n.sx, n.sz, n.sy);
        auto it = sections.find(curKey);
        // 缺失 section（纯空气/未加载）视为六向全通
        uint64_t curVis = (it != sections.end()) ? it->second : ~0ULL;

        for (int d = 0; d < 6; d++) {
            if (!outwardAllowed(d, n.sx, n.sz, n.sy, camChunkX, camChunkZ, camOutY)) continue;
            // 连通查询：种子（entryFace<0）放行任意方向，否则须 entryFace→d 在内部可达
            if (n.entryFace >= 0 && !(curVis & (1ULL << (n.entryFace * 8 + d)))) continue;

            int nsx = n.sx + STEP[d][0];
            int nsy = n.sy + STEP[d][1];
            int nsz = n.sz + STEP[d][2];
            if (nsy < syMin || nsy > syMax) continue;
            if (nsx < camChunkX - rChunks || nsx > camChunkX + rChunks) continue;
            if (nsz < camChunkZ - rChunks || nsz > camChunkZ + rChunks) continue;

            int nEntry = d ^ 1;  // 进入邻居的面 = 扩展方向的反向
            uint64_t nk = sectionKey(nsx, nsz, nsy);
            uint8_t& pf = processed[nk];
            if (pf & (1 << nEntry)) continue;  // 该进入面已处理，避免重复扩展
            pf |= (1 << nEntry);
            visibleSet.insert(nk);
            q.push({nsx, nsz, nsy, nEntry});
        }
    }
}
