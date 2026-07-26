#include "CrackOverlayMesh.h"
#include "gui/GameUI.h"
#include "TextureAtlas.h"
#include "ClientEngine/ClientEngine.h"

bool CrackOverlayMesh::update(bool& rebuilt) {
    rebuilt = false;

    auto& ui = GameUI::getInstance();
    if (!ui.isDigging()) return false;

    int stage = ui.getDestroyStage();
    if (stage < 0 || stage > 9) return false;

    int bx = ui.getDigBlockX();
    int by = ui.getDigBlockY();
    int bz = ui.getDigBlockZ();

    int texLayer = ClientEngine::getInstance()->getTextureAtlas()->getDestroyStageLayer(stage);
    if (texLayer < 0) return false;

    // 脏检查：位置或阶段变化才重建几何体
    if (bx != lastBlockX || by != lastBlockY || bz != lastBlockZ || stage != lastStage) {
        lastBlockX = bx;
        lastBlockY = by;
        lastBlockZ = bz;
        lastStage = stage;
        buildMesh(bx, by, bz, static_cast<float>(texLayer));
        rebuilt = true;
    }

    return !vertices.empty();
}

void CrackOverlayMesh::buildMesh(int bx, int by, int bz, float texLayer) {
    float minX = (float)bx, minY = (float)by, minZ = (float)bz;

    static const float faceNormals[6][3] = {
        {0, -1, 0}, {0, 1, 0}, {0, 0, -1},
        {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}
    };
    static const float faceVerts[6][4][3] = {
        {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
        {{0,1,0},{1,1,0},{1,1,1},{0,1,1}},
        {{0,0,0},{1,0,0},{1,1,0},{0,1,0}},
        {{1,0,1},{0,0,1},{0,1,1},{1,1,1}},
        {{0,0,0},{0,0,1},{0,1,1},{0,1,0}},
        {{1,0,0},{1,0,1},{1,1,1},{1,1,0}}
    };
    static const float faceUVs[6][4][2] = {
        {{0,0},{1,0},{1,1},{0,1}},
        {{0,0},{1,0},{1,1},{0,1}},
        {{0,0},{1,0},{1,1},{0,1}},
        {{1,0},{0,0},{0,1},{1,1}},
        {{0,0},{1,0},{1,1},{0,1}},
        {{0,0},{1,0},{1,1},{0,1}}
    };

    vertices.clear();
    vertices.reserve(24);
    for (int face = 0; face < 6; face++) {
        for (int v = 0; v < 4; v++) {
            Vertex vert;
            vert.pos[0] = minX + faceVerts[face][v][0];
            vert.pos[1] = minY + faceVerts[face][v][1];
            vert.pos[2] = minZ + faceVerts[face][v][2];
            vert.texCoord[0] = faceUVs[face][v][0];
            vert.texCoord[1] = faceUVs[face][v][1];
            vert.texIndex = texLayer;
            vert.color[0] = 255; vert.color[1] = 255; vert.color[2] = 255; vert.color[3] = 255;
            vert.normal[0] = faceNormals[face][0];
            vert.normal[1] = faceNormals[face][1];
            vert.normal[2] = faceNormals[face][2];
            vert.uv2[0] = 8.0f; vert.uv2[1] = 8.0f;
            vertices.push_back(vert);
        }
    }

    indices.clear();
    indices.reserve(36);
    for (int f = 0; f < 6; f++) {
        uint32_t base = f * 4;
        indices.push_back(base); indices.push_back(base+1); indices.push_back(base+2);
        indices.push_back(base); indices.push_back(base+2); indices.push_back(base+3);
    }
}
