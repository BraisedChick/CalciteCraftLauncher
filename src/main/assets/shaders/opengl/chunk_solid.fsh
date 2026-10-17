#version 300 es
// ============================================================
// Calcite GL 后端 —— 不透明方块（chunk_solid）
// 图集以纹理数组承载，层号由顶点 TexIndex 经 flat 插值传来
// ============================================================

precision highp float;
precision highp sampler2DArray;

uniform sampler2DArray Sampler0;

uniform vec4 ColorModulator;
uniform float FogStart;
uniform float FogEnd;
uniform vec4 FogColor;

in float vertexDistance;
in vec4 vertexColor;
in vec2 texCoord0;
flat in float fragTexIndex;

out vec4 fragColor;

// 雾浓度：FogStart 内为 0，FogStart..FogEnd 之间平滑过渡到 1
float fog_factor() {
    if (vertexDistance <= FogStart) {
        return 0.0;
    }
    if (vertexDistance >= FogEnd) {
        return 1.0;
    }
    return smoothstep(FogStart, FogEnd, vertexDistance);
}

void main() {
    vec4 color = texture(Sampler0, vec3(texCoord0, fragTexIndex)) * vertexColor * ColorModulator;

    float fog = fog_factor();
    fragColor = vec4(mix(color.rgb, FogColor.rgb, fog * FogColor.a), color.a);
}
