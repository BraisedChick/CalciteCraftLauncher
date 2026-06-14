#include "Light.h"
#include <android/log.h>
#include <cmath>
#include <algorithm>

#define LOG_TAG "Light"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

Light& Light::getInstance() {
    static Light instance;
    return instance;
}

// ===== 昼夜计算 =====

float Light::getSkyDarken() const {
    // DayTime: 0=sunrise, 6000=noon, 12000=sunset, 18000=midnight, 24000=sunrise
    long long dt = worldDayTime % 24000;
    if (dt < 0) dt += 24000;

    float darken;
    if (dt < 12000) {
        darken = 0.0f;                        // 白天
    } else if (dt < 13000) {
        darken = (float)(dt - 12000) / 1000.0f; // 黄昏
    } else if (dt < 23000) {
        darken = 1.0f;                         // 夜晚
    } else {
        darken = 1.0f - (float)(dt - 23000) / 1000.0f; // 黎明
    }
    return darken;
}

// ===== 光照贴图创建 =====

void Light::createLightmapTexture() {
    glGenTextures(1, &lightmapTextureID);
    glBindTexture(GL_TEXTURE_2D, lightmapTextureID);

    // 初始生成（全白天亮度，首帧 update() 会覆盖）
    uint8_t pixels[16 * 16 * 4];
    generateLightmapPixels(1.0f, pixels);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    LOGI("Created lightmap texture 16x16 with colored lighting (warm block/cool sky)");
}

// ===== 每帧更新 =====

void Light::update() {
    float skyDarken = getSkyDarken();
    float skyBright = 1.0f - skyDarken;

    // 天空/雾效颜色插值：白天蓝 → 夜晚深蓝黑
    skyR = 0.53f * skyBright + 0.02f * skyDarken;
    skyG = 0.81f * skyBright + 0.02f * skyDarken;
    skyB = 0.92f * skyBright + 0.08f * skyDarken;

    // 更新光照贴图纹理
    if (lightmapTextureID != 0) {
        uint8_t pixels[16 * 16 * 4];
        generateLightmapPixels(skyBright, pixels);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, lightmapTextureID);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
}

// ===== 光照贴图像素生成 =====

void Light::generateLightmapPixels(float skyBright, uint8_t* pixels) {
    for (int sy = 0; sy < 16; sy++) {
        for (int bx = 0; bx < 16; bx++) {
            float blockBright = bx / 15.0f;
            float skyBrightLevel = (sy / 15.0f) * skyBright;

            // 方块光颜色（暖色/橙红——火把/熔岩风格）
            float blockR = blockBright;
            float blockG = blockBright * ((blockBright * 0.6f + 0.4f) * 0.6f + 0.4f);
            float blockB = blockBright * (blockBright * blockBright * 0.6f + 0.4f);

            // 天空光颜色（冷色/蓝白，随昼夜变暗）
            float sR = skyBrightLevel * 0.9f;
            float sG = skyBrightLevel * 1.0f;
            float sB = skyBrightLevel * 1.1f;

            // 合成
            float totalR = fminf(blockR + sR, 1.0f);
            float totalG = fminf(blockG + sG, 1.0f);
            float totalB = fminf(blockB + sB, 1.0f);

            // 混合一点灰色
            totalR = totalR * 0.96f + 0.04f * 0.75f;
            totalG = totalG * 0.96f + 0.04f * 0.75f;
            totalB = totalB * 0.96f + 0.04f * 0.75f;

            // Gamma 校正（原版 notGamma 风格）
            float gamma = 0.5f;
            float ngR = 1.0f - powf(1.0f - totalR, 4.0f);
            float ngG = 1.0f - powf(1.0f - totalG, 4.0f);
            float ngB = 1.0f - powf(1.0f - totalB, 4.0f);
            totalR = totalR * (1.0f - gamma) + ngR * gamma;
            totalG = totalG * (1.0f - gamma) + ngG * gamma;
            totalB = totalB * (1.0f - gamma) + ngB * gamma;

            totalR = fminf(fmaxf(totalR, 0.0f), 1.0f);
            totalG = fminf(fmaxf(totalG, 0.0f), 1.0f);
            totalB = fminf(fmaxf(totalB, 0.0f), 1.0f);

            // 最低环境光保底（避免全黑洞穴完全不可见）
            const float minAmbient = 0.15f;
            totalR = fmaxf(totalR, minAmbient);
            totalG = fmaxf(totalG, minAmbient);
            totalB = fmaxf(totalB, minAmbient);

            int idx = (sy * 16 + bx) * 4;
            pixels[idx + 0] = (uint8_t)(totalR * 255.0f);
            pixels[idx + 1] = (uint8_t)(totalG * 255.0f);
            pixels[idx + 2] = (uint8_t)(totalB * 255.0f);
            pixels[idx + 3] = 255;
        }
    }
}
