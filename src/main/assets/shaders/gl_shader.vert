#version 300 es

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in float inTexIndex;  // 纹理索引
layout(location = 3) in vec4 inColor;       // 顶点颜色（用于群系染色）

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;

out vec2 fragTexCoord;
out float fragTexIndex;
out vec4 fragColor;  // 传递顶点颜色到片段着色器

void main() {
    gl_Position = proj * view * model * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragTexIndex = inTexIndex;
    fragColor = inColor;
}
