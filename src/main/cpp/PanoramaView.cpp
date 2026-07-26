#include "PanoramaView.h"
#include "TextureLoader.h"
#include <android/log.h>
#include <glm/gtc/matrix_transform.hpp>

#define LOG_TAG "PanoramaView"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

int PanoramaView::loadFacePixels(FacePixels outFaces[6]) {
    // panorama_N.png → cubemap 面映射（Minecraft 全景图约定）
    // 0: south (-Z), 1: west (-X), 2: north (+Z), 3: east (+X), 4: up (+Y), 5: down (-Y)
    // cubemap 顺序: +X, -X, +Y, -Y, +Z, -Z
    static const char* faces[6] = {
        "gui/title/background/panorama_3.png",  // +X = east
        "gui/title/background/panorama_1.png",  // -X = west
        "gui/title/background/panorama_4.png",  // +Y = up (sky)
        "gui/title/background/panorama_5.png",  // -Y = down (ground)
        "gui/title/background/panorama_0.png",  // +Z = north
        "gui/title/background/panorama_2.png"   // -Z = south
    };

    int loadedCount = 0;
    for (int i = 0; i < 6; i++) {
        FacePixels& face = outFaces[i];
        TextureData tex = TextureLoader::loadImage(faces[i]);
        if (tex.data && tex.width > 0 && tex.height > 0) {
            // 水平翻转全景图（Minecraft 全景图方向与 cubemap 采样方向相反）
            face.width = tex.width;
            face.height = tex.height;
            face.rgba.resize((size_t)tex.width * tex.height * 4);
            for (int y = 0; y < tex.height; y++) {
                for (int x = 0; x < tex.width; x++) {
                    int srcIdx = (y * tex.width + x) * 4;
                    int dstIdx = (y * tex.width + (tex.width - 1 - x)) * 4;
                    face.rgba[dstIdx + 0] = tex.data[srcIdx + 0];
                    face.rgba[dstIdx + 1] = tex.data[srcIdx + 1];
                    face.rgba[dstIdx + 2] = tex.data[srcIdx + 2];
                    face.rgba[dstIdx + 3] = tex.data[srcIdx + 3];
                }
            }
            face.loaded = true;
            LOGI("Panorama face %d loaded: %s (%dx%d)", i, faces[i], tex.width, tex.height);
            loadedCount++;
        } else {
            LOGE("Failed to load panorama face %d: %s", i, faces[i]);
            // 用递变蓝色填充缺失面
            face.width = 16;
            face.height = 16;
            face.rgba.resize(16 * 16 * 4);
            for (int p = 0; p < 16 * 16; p++) {
                face.rgba[p*4+0] = 30 + i * 20;
                face.rgba[p*4+1] = 30 + i * 20;
                face.rgba[p*4+2] = 80 + i * 20;
                face.rgba[p*4+3] = 255;
            }
            face.loaded = false;
        }
    }
    return loadedCount;
}

const float* PanoramaView::cubeVertices(size_t& floatCount) {
    static const float cubeVerts[] = {
        -1,-1,-1,  1,-1,-1,  1, 1,-1, -1, 1,-1,
        -1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1,
    };
    floatCount = sizeof(cubeVerts) / sizeof(float);
    return cubeVerts;
}

const uint16_t* PanoramaView::cubeIndices(size_t& indexCount) {
    static const uint16_t cubeIdx[] = {
        0,1,2, 2,3,0,  4,6,5, 6,4,7,
        0,4,5, 5,1,0,  2,6,7, 7,3,2,
        0,3,7, 7,4,0,  1,5,6, 6,2,1,
    };
    indexCount = sizeof(cubeIdx) / sizeof(uint16_t);
    return cubeIdx;
}

glm::mat4 PanoramaView::computeMVP(int screenWidth, int screenHeight) {
    if (!startTimeValid) {
        startTime = std::chrono::high_resolution_clock::now();
        startTimeValid = true;
    }

    float aspect = (screenHeight > 0) ? (float)screenWidth / (float)screenHeight : 1.0f;
    glm::mat4 proj = glm::perspective(glm::radians(70.0f), aspect, 0.1f, 10.0f);

    float elapsed = std::chrono::duration<float>(
        std::chrono::high_resolution_clock::now() - startTime).count();
    float angle = elapsed * 0.035f;  // ~2°/s

    glm::mat4 view = glm::mat4(1.0f);
    view = glm::rotate(view, glm::radians(15.0f), glm::vec3(1, 0, 0));
    view = glm::rotate(view, angle, glm::vec3(0, 1, 0));
    return proj * view;
}
