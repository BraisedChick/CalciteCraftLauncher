#pragma once

#include <cstdint>

struct Vertex {
    float pos[3];
    float texCoord[2];   // UV0（主纹理坐标）
    float texIndex;       // 纹理数组层索引
    uint8_t color[4] = {255, 255, 255, 255}; // RGBA 染色（默认白色=不染色）
    float normal[3] = {0.0f, 1.0f, 0.0f};   // 面法线（Mojang Normal 属性）
    float uv2[2] = {240.0f, 240.0f};         // 光照贴图UV（blockLight*16, skyLight*16），默认全亮
};
