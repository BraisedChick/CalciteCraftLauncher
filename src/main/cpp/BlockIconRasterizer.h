#pragma once

#include <string>
#include <vector>
#include <cstdint>

class TextureAtlas;

// ============================================================
// BlockIconRasterizer — 方块模型 → 3D 图标像素光栅化（纯 CPU）
//
// 纯逻辑层：不依赖任何图形 API（GL/Vulkan），供两个后端共用。
// CPU 生成等距投影立方体图标，输出 RGBA 像素缓冲；
// 纹理上传由各渲染器后端自行完成：
//   - GLRenderer：glTexImage2D
//   - VulkanRenderer：staging buffer + vkCmdCopyBufferToImage
// 分层模式与 Light / PanoramaView / CrackOverlayMesh 一致。
// ============================================================
class BlockIconRasterizer {
public:
    // 将方块模型光栅化为 iconSize×iconSize 的 RGBA 像素缓冲
    // atlas: 提供模型几何数据与纹理文件名（须已 initialize）
    // 返回 false 表示无模型数据或无 elements（调用方应回退 2D 贴图）
    static bool rasterize(const TextureAtlas* atlas, const std::string& modelName,
                          int iconSize, std::vector<uint8_t>& outPixels);
};
