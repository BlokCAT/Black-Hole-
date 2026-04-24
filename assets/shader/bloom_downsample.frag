#version 460 core

// ============================================================
// Bloom 下采样 Pass
// 每次将分辨率减半，同时做模糊
// 基于 Jorge Jimenez SIGGRAPH 2016 的 13-tap 采样模式
// ============================================================

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D srcTexture;   // 上一级的输出纹理
uniform float srcResolution;    // 上一级纹理的像素尺寸

void main() {
    // 当前像素在源纹理中的 UV 坐标
    vec2 texelSize = vec2(1.0) / srcResolution;
    vec3 result = vec3(0.0);
    // === 中心采样 ===
    result += texture(srcTexture, vUV).rgb * 0.5;

    float x = texelSize.x;
    float y = texelSize.y;

    result += texture(srcTexture, vUV + vec2( x,  0.0)).rgb * 0.125;
    result += texture(srcTexture, vUV + vec2(-x,  0.0)).rgb * 0.125;
    result += texture(srcTexture, vUV + vec2(0.0,  y )).rgb * 0.125;
    result += texture(srcTexture, vUV + vec2(0.0, -y )).rgb * 0.125;

    // === 对角采样
    vec2 d1 = vec2( x,  y) * 1.5;
    vec2 d2 = vec2(-x,  y) * 1.5;
    vec2 d3 = vec2( x, -y) * 1.5;
    vec2 d4 = vec2(-x, -y) * 1.5;

    result += texture(srcTexture, vUV + d1).rgb * 0.03125;
    result += texture(srcTexture, vUV + d2).rgb * 0.03125;
    result += texture(srcTexture, vUV + d3).rgb * 0.03125;
    result += texture(srcTexture, vUV + d4).rgb * 0.03125;

    FragColor = vec4(result, 1.0);
}
