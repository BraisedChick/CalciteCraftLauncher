#version 300 es
precision mediump float;

in vec2 fragTexCoord;
in float fragTexIndex;  // 纹理层索引（0, 1, 2, ...）
in vec4 fragColor;      // 顶点颜色（用于群系染色，默认白色=不染色）

uniform sampler2DArray textureSampler;  // 纹理数组
uniform sampler2D waterTexture;         // 水纹理（单独加载，16x512 = 32帧动画）
uniform float waterTime;               // 水动画时间 [0, 1)
uniform int useTexture;
uniform int useWaterTexture;           // 是否使用水纹理（水渲染通道设为 1）

out vec4 outColor;

void main() {
    if (useTexture == 1) {
        if (useWaterTexture == 1) {
            // 水纹理：16x512 包含 32 帧，按时间偏移 V 坐标选取帧
            float frame = floor(waterTime * 32.0);
            float v = fragTexCoord.y / 32.0 + frame / 32.0;
            outColor = texture(waterTexture, vec2(fragTexCoord.x, v));
            outColor *= vec4(fragColor.rgb, 1.0);
        } else {
            // 确保 texIndex 是整数（避免浮点插值误差）
            float layer = round(fragTexIndex);

            // 从纹理数组采样
            outColor = texture(textureSampler, vec3(fragTexCoord, layer));

            // 应用顶点颜色进行群系染色（alpha 恒为 1.0，水的透明度由 blend 常数控制）
            outColor *= vec4(fragColor.rgb, 1.0);

            // 丢弃透明像素（支持 grass.png 等带透明通道的纹理）
            if (outColor.a < 0.1) {
                discard;
            }
        }
    } else {
        // 调试模式：纯红色
        outColor = vec4(1.0, 0.0, 0.0, 1.0);
    }
}
