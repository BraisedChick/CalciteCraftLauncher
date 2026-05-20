#pragma once

struct Vertex {
    float pos[3];
    float texCoord[2];
    float texIndex;  // 0 = 顶部纹理, 1 = 侧面纹理
};
