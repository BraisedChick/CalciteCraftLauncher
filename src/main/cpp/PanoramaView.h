#pragma once

#include <vector>
#include <cstdint>
#include <chrono>
#include <glm/glm.hpp>

// 主菜单旋转全景背景（panorama）
// 纯逻辑层：面像素加载/翻转、立方体几何、旋转动画 MVP 计算，不接触任何图形 API。
// GLRenderer / VulkanRenderer 拿到像素与矩阵后各自负责纹理上传、着色器与绘制
// （Vulkan 侧需自行叠加 clip 空间 Y 翻转修正）。
class PanoramaView {
public:
    struct FacePixels {
        std::vector<uint8_t> rgba;  // 已水平翻转的 RGBA 像素
        int width = 0;
        int height = 0;
        bool loaded = false;        // false = 加载失败，rgba 为占位色
    };

    // 按 cubemap 面顺序（+X,-X,+Y,-Y,+Z,-Z）加载 6 张全景图并水平翻转，
    // 缺失面填充递变蓝色占位。返回成功加载的面数。
    int loadFacePixels(FacePixels outFaces[6]);

    // 立方体几何（8 顶点 xyz / 36 索引）
    static const float* cubeVertices(size_t& floatCount);
    static const uint16_t* cubeIndices(size_t& indexCount);

    // 旋转动画 MVP（透视 70°，俯仰 15°，~2°/s 绕 Y 旋转）
    // 首次调用时记录动画起始时间
    glm::mat4 computeMVP(int screenWidth, int screenHeight);

private:
    bool startTimeValid = false;
    std::chrono::high_resolution_clock::time_point startTime;
};
