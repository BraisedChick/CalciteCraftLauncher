# Minecraft 全版本支持架构 (1.8 - 26.2)

## 📋 概述

本项目实现了灵活的 Minecraft 多版本支持架构，可以处理从 1.8 到未来版本（26.2）的协议差异。

## 🏗️ 架构设计

### 核心组件

#### 1. **MinecraftVersion.h/cpp** - 版本管理器
- **职责**：管理协议版本和对应的特性
- **功能**：
  - 协议版本号常量定义
  - 区块数据格式分类（Legacy/Modern/Extended/Latest）
  - 方块状态编码方式（DirectID/Palette/BitArray/Registry）
  - 维度配置（minY, maxY, sectionHeight）

#### 2. **Chunk.h** - 动态区块结构
- **改进**：使用 `std::vector` 替代固定大小的 `std::array`
- **优势**：可以根据不同版本的维度配置动态调整 section 数量

#### 3. **ChunkParser.h/cpp** - 版本分发解析器
- **策略模式**：根据区块数据格式自动选择对应的解析方法
- **支持的格式**：
  - `parseLegacyChunk()` - 1.8-1.12
  - `parseModernChunk()` - 1.13-1.17
  - `parseExtendedChunk()` - 1.18+
  - `parseLatestChunk()` - 1.20.5+

## 📊 版本对照表

| Minecraft 版本 | 协议号 | 区块格式 | 方块编码 | Y 范围 | Sections |
|---------------|--------|---------|---------|--------|----------|
| 1.8           | 47     | Legacy  | DirectID | 0-255  | 16       |
| 1.9-1.12      | 107-340| Legacy  | DirectID | 0-255  | 16       |
| 1.13-1.17     | 404-756| Modern  | Palette  | 0-255  | 16       |
| 1.18-1.20.2   | 757-764| Extended| BitArray | -64-320| 24       |
| 1.20.5+       | 766+   | Latest  | Registry | -64-320| 24       |
| 1.22+ (预估)  | 800+   | Latest  | Registry | -64-384| 28       |
| 1.26+ (预估)  | 1000+  | Latest  | Registry | -128-512| 40     |

## 🔧 使用方法

### 1. 设置协议版本

在握手阶段设置协议版本：

```cpp
// ClientEngine.cpp
int protocolVersion = 758;  // Minecraft 1.18.2
handshake.SetProtocolVersion(protocolVersion);

// 设置全局协议版本（用于区块解析）
VersionManager::getInstance().setProtocolVersion(protocolVersion);
```

### 2. 查询版本特性

```cpp
const auto& versionMgr = VersionManager::getInstance();

// 获取当前协议版本
int version = versionMgr.getProtocolVersion();

// 检查版本是否大于等于指定版本
if (versionMgr.isAtLeast(ProtocolVersion::V1_18)) {
    // 使用 1.18+ 的特性
}

// 获取维度配置
const auto& dimConfig = versionMgr.getDimensionConfig();
int minY = dimConfig.minY;
int maxY = dimConfig.maxY;
int sectionCount = dimConfig.getSectionCount();

// 获取区块数据格式
ChunkDataFormat format = versionMgr.getChunkFormat();
```

### 3. 区块解析（自动版本适配）

```cpp
ChunkParser parser;

// 通用接口，自动根据协议版本选择解析方法
auto chunk = parser.parseChunkData(
    chunkX, chunkZ,
    data, fullChunk, primaryBitMask,
    heightmaps, blockEntities,
    dimensionMinY
);
```

## 🎯 关键特性

### 1. 动态 Section 管理

```cpp
struct Chunk {
    std::vector<std::unique_ptr<ChunkSection>> sections; // 动态大小
    DimensionConfig dimension;
    
    void initializeSections() {
        const auto& versionMgr = VersionManager::getInstance();
        dimension = versionMgr.getDimensionConfig();
        
        int sectionCount = dimension.getSectionCount();
        sections.resize(sectionCount);
        
        for (int i = 0; i < sectionCount; i++) {
            int sectionY = dimension.minY + (i * SECTION_HEIGHT);
            sections[i] = std::make_unique<ChunkSection>(sectionY);
        }
    }
};
```

### 2. 版本分发逻辑

```cpp
std::unique_ptr<Chunk> ChunkParser::parseChunkData(...) {
    const auto& versionMgr = VersionManager::getInstance();
    ChunkDataFormat format = versionMgr.getChunkFormat();
    
    switch (format) {
        case ChunkDataFormat::Legacy:
            return parseLegacyChunk(...);
        case ChunkDataFormat::Modern:
            return parseModernChunk(...);
        case ChunkDataFormat::Extended:
            return parseExtendedChunk(...);
        case ChunkDataFormat::Latest:
            return parseLatestChunk(...);
    }
}
```

### 3. 未来版本扩展

添加新版本只需：

1. 在 `MinecraftVersion.h` 中添加协议版本号常量
2. 在 `initializeVersionMap()` 中添加版本特性映射
3. 实现对应的解析方法（如果需要）

```cpp
// 示例：添加 1.22 版本支持
constexpr int V1_22 = 800;

versionMap[ProtocolVersion::V1_22] = ProtocolFeatures(
    ProtocolVersion::V1_22,
    ChunkDataFormat::Latest,
    BlockStateEncoding::Registry,
    DimensionConfig(-64, 384, 16)  // 假设未来会扩展高度
);
```

## 📝 TODO 列表

### 已完成 ✅
- [x] 版本管理器框架
- [x] 协议版本常量定义
- [x] 区块数据格式分类
- [x] 维度配置系统
- [x] 版本分发逻辑
- [x] 动态 Section 管理
- [x] ClientEngine 集成

### 待实现 🚧
- [ ] Legacy Chunk Parser (1.8-1.12)
  - [ ] 全局调色板解析
  - [ ] 4-bit 方块 ID 解码
  
- [ ] Modern Chunk Parser (1.13-1.17)
  - [ ] Section 调色板解析
  - [ ] VarInt 位数组解码
  
- [ ] Latest Chunk Parser (1.20.5+)
  - [ ] Registry-based 系统
  - [ ] 新的 NBT 格式
  
- [ ] 方块状态映射
  - [ ] 1.8-1.12: Data Value → Block State
  - [ ] 1.13+: Block State ID → 纹理/属性
  
- [ ] 生物群系解析
  - [ ] 不同版本的生物群系编码

## 🔍 调试技巧

### 查看当前版本信息

```cpp
LOGI("Protocol version: %d (%s)", 
     versionMgr.getProtocolVersion(),
     versionMgr.getVersionName().c_str());
     
LOGI("Chunk format: %d", 
     static_cast<int>(versionMgr.getChunkFormat()));
     
LOGI("Dimension: Y=%d to %d, sections=%d",
     dimConfig.minY, dimConfig.maxY, dimConfig.getSectionCount());
```

### 日志输出示例

```
I/VersionManager: Protocol version set to: 758 (1.18.2)
I/VersionManager:   Chunk format: 2
I/VersionManager:   Block encoding: 2
I/VersionManager:   Dimension: Y=-64 to 320, sections=24
I/ChunkParser: Parsing chunk (0, 0) with protocol version 758 (1.18.2)
I/ChunkParser: Parsing extended chunk (1.18+)
```

## 📚 参考资料

- [Minecraft Wiki - Protocol](https://wiki.vg/Protocol)
- [Botcraft - Cross-platform Minecraft Bot](https://github.com/TheVoxel/Botcraft)
- [ProtocolCraft - C++ Protocol Library](https://github.com/TheVoxel/ProtocolCraft)

## ⚠️ 注意事项

1. **协议版本必须一致**：握手时设置的协议版本必须与服务器期望的版本一致
2. **维度配置很重要**：不同版本的 Y 范围不同，影响 section 计算
3. **向后兼容**：新版本客户端通常可以连接旧版本服务器，但需要正确处理协议差异
4. **测试建议**：每个版本都应该在实际服务器上测试验证

## 🚀 下一步计划

1. 完善各个版本的解析器实现
2. 添加方块状态映射系统
3. 实现生物群系解析
4. 添加更多的版本特性检测
5. 编写单元测试

---

**最后更新**: 2026-05-19  
**维护者**: AI Assistant
