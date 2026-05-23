#pragma once

#include <cstdint>

struct Vertex {
    float pos[3];
    float texCoord[2];
    float texIndex;
    uint8_t color[4] = {255, 255, 255, 255}; // RGBA 染色（默认白色=不染色）
};
