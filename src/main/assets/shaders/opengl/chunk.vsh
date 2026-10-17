#version 300 es
// ============================================================
// Calcite GL 后端 —— 区块顶点着色器（四个程序共用）
//
// 输入布局与 GLRenderer::processCompletedWork 里的 PackedVertex 严格对应：
//   loc 0 Position  vec3  (GL_FLOAT, 区块局部坐标)
//   loc 1 UV0       vec2  (GL_UNSIGNED_SHORT, 归一化到 [0,1])
//   loc 2 TexIndex  float (GL_FLOAT, 纹理数组层号)
//   loc 3 Color     vec4  (GL_UNSIGNED_BYTE, 归一化；含草/叶 tint 与 AO 亮度)
//   loc 4 Normal    vec4  (GL_BYTE, 归一化；地形管线当前不使用，故未声明)
//   loc 5 UV2       vec2  (GL_UNSIGNED_SHORT, 原值；光照贴图坐标 block*16+8, sky*16+8)
// ============================================================

layout(location = 0) in vec3 Position;
layout(location = 1) in vec2 UV0;
layout(location = 2) in float TexIndex;
layout(location = 3) in vec4 Color;
layout(location = 5) in vec2 UV2;

uniform mat4 ModelViewMat;
uniform mat4 ProjMat;
uniform vec3 ChunkOffset;
uniform int FogShape;

// 16x16 光照贴图（块光为 x、天光为 y）
uniform sampler2D Sampler2;

out float vertexDistance;
out vec4 vertexColor;
out vec2 texCoord0;
flat out float fragTexIndex;

// 雾距离：FogShape 0 = 到相机的球面距离，非 0 = 以视线竖直轴为中心的柱面距离
float fog_distance(vec4 viewPos) {
    if (FogShape == 0) {
        return length(viewPos.xyz);
    }
    return max(length(viewPos.xz), abs(viewPos.y));
}

// 顶点存的是纹素中心（8..248），除以 256 落到纹素中心区间后再夹住，
// 防止线性/mipmap 过滤采样到光照贴图边缘的外侧颜色
vec2 lightmap_coord(vec2 raw) {
    return clamp(raw / 256.0, vec2(0.5 / 16.0), vec2(15.5 / 16.0));
}

void main() {
    vec3 pos = Position + ChunkOffset;
    vec4 viewPos = ModelViewMat * vec4(pos, 1.0);

    gl_Position = ProjMat * viewPos;

    vertexDistance = fog_distance(viewPos);
    vertexColor = Color * texture(Sampler2, lightmap_coord(UV2));
    texCoord0 = UV0;
    // 层号来自顶点属性，取整后走 flat 插值，避免跨三角形的浮点漂移
    fragTexIndex = round(TexIndex);
}
