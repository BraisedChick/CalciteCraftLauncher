# Minecraft 启动器功能说明

## 🎮 功能特性

### 1. **登录方式选择**
- ✅ **离线登录**（已实现）
  - 只需输入用户名即可连接服务器
  - 无需正版账号
  
- ⏳ **正版登录**（暂未实现）
  - 预留接口，未来可添加 Microsoft 账号登录
  - 当前点击会提示"暂未实现"

### 2. **版本选择**
支持从 1.8 到 26.2 的所有主要版本（1.21.11 后使用新命名规则）：

| 版本 | 协议号 | 状态 |
|------|--------|------|
| 1.8  | 47     | ✅ 框架就绪 |
| 1.12 | 340    | ✅ 框架就绪 |
| 1.13 | 404    | ✅ 框架就绪 |
| 1.14 | 498    | ✅ 框架就绪 |
| 1.15 | 578    | ✅ 框架就绪 |
| 1.16 | 735    | ✅ 框架就绪 |
| 1.17 | 755    | ✅ 框架就绪 |
| **1.18** | **757** | **✅ 完全支持** |
| 1.19 | 759    | ✅ 框架就绪 |
| 1.20 | 763    | ✅ 框架就绪 |
| 1.21 | 767    | ✅ 框架就绪 |
| 26.1 | 800    | 🔮 新命名规则 |
| 26.2 | 850    | 🔮 新命名规则 |

**注意：** Minecraft 从 1.21.11 之后改变了版本命名规则，采用新的纪元系统（如 26.1、26.2），不再使用 1.x 的命名方式。

### 3. **渲染器选择**
- ✅ **OpenGL ES**（已实现）
  - 当前默认渲染器
  - 性能良好，兼容性高
  
- ⏳ **Vulkan**（暂未实现）
  - 预留接口，未来可启用
  - 当前点击会提示"暂未实现"

### 4. **配置保存**
自动保存以下配置到 SharedPreferences：
- 用户名
- 服务器地址和端口
- 选择的版本
- 选择的渲染器
- 登录方式

下次启动时会自动加载上次的配置。

## 📱 使用流程

### 启动应用
1. 打开应用，进入启动器界面
2. 选择登录方式（目前只能选离线登录）
3. 输入用户名
4. 输入服务器地址和端口
5. 选择游戏版本
6. 选择渲染器（目前只能选 OpenGL ES）
7. 点击"启动游戏"

### 进入游戏
- 启动器会将配置传递给游戏主界面
- 游戏主界面自动使用这些配置连接服务器
- 连接成功后显示 3D 世界

## 🛠️ 技术实现

### Java 层
- **LauncherActivity.java**: 启动器主界面
- **MainActivity.java**: 游戏主界面（接收启动器参数）

### C++ 层
- **MinecraftVersion.h/cpp**: 版本管理器
- **ClientEngine.cpp**: 使用启动器设置的协议版本
- **native-lib.cpp**: JNI 接口传递协议版本

### 数据流
```
LauncherActivity (Java)
    ↓ Intent extras
MainActivity (Java)
    ↓ JNI call: setProtocolVersion()
VersionManager (C++)
    ↓ getProtocolVersion()
ClientEngine (C++)
    ↓ SetProtocolVersion()
ProtocolCraft Library
```

## 🎨 UI 设计

### 启动器界面（竖屏）
- 深色主题 (#1a1a2e)
- 绿色强调色 (#4CAF50)
- 清晰的分组布局
- Spinner 下拉选择版本
- RadioGroup 单选按钮

### 游戏界面（横屏）
- 全屏 3D 渲染
- 虚拟摇杆控制移动
- 触摸控制视角
- 上升/下降按钮

## 📝 待实现功能

### 短期目标
1. **完善各版本解析器**
   - Legacy Chunk Parser (1.8-1.12)
   - Modern Chunk Parser (1.13-1.17)
   - Latest Chunk Parser (1.20.5+)

2. **方块状态映射**
   - 不同版本的方块 ID 转换
   - 纹理映射

### 中期目标
1. **正版登录**
   - Microsoft OAuth 认证
   - Session 管理
   
2. **Vulkan 渲染器**
   - 完整的 Vulkan 实现
   - 性能优化

3. **更多版本支持**
   - 测试各个版本的兼容性
   - 修复版本特定的 bug

### 长期目标
1. **模组支持**
2. **资源包系统**
3. **多人游戏大厅**
4. **服务器列表管理**

## 🔧 开发建议

### 添加新版本
1. 在 `LauncherActivity.java` 中添加版本号到 `VERSIONS` 数组
2. 在 `PROTOCOL_VERSIONS` 数组中添加对应的协议号
3. 在 `MinecraftVersion.h` 中添加协议版本常量
4. 在 `MinecraftVersion.cpp` 的 `initializeVersionMap()` 中添加版本特性

### 启用新功能
1. 将 RadioButton 的 `enabled` 属性改为 `true`
2. 移除或修改 `OnCheckedChangeListener` 中的提示
3. 实现相应的功能代码

## 🐛 已知问题

1. **正版登录未实现** - 当前只能使用离线模式
2. **Vulkan 渲染器未实现** - 当前只能使用 OpenGL ES
3. **部分版本解析器未完成** - 只有 1.18+ 完全支持

## 📚 相关文档

- [多版本支持架构](MULTI_VERSION_SUPPORT.md)
- [ProtocolCraft 库文档](protocolCraft/README.md)
- [Botcraft 参考实现](https://github.com/TheVoxel/Botcraft)

---

**最后更新**: 2026-05-19  
**版本**: 1.0.0
