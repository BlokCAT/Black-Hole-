#version 460 core

// ============================================================
// 色调映射 + Bloom 合成 + 高光压缩 Pass
//
// 管线：
//   1. HDR场景 + Bloom结果 合成
//   2. 高光压缩（Highlight Compression）
//   3. ACES 色调映射
//   4. Gamma 校正
// ============================================================

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D hdrTexture;       // 原始 HDR 场景
uniform sampler2D bloomTexture;     // Bloom 模糊结果
uniform float bloomStrength;        // Bloom 强度
uniform float exposure;             // 曝光值
uniform float highlightThreshold;   // 高光压缩阈值
uniform float highlightKnee;        // 高光压缩强度

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

vec3 compressHighlights(vec3 color, float threshold, float knee) {
    float lum = luminance(color);
    if (lum <= threshold) {
        return color;
    }
    float excess = lum - threshold;
    float compressed = threshold + excess / (1.0 + excess / knee);
    return color * (compressed / max(lum, 0.001));
}

void main() {
    vec3 hdrColor = texture(hdrTexture, vUV).rgb;
    vec3 bloomColor = texture(bloomTexture, vUV).rgb;

    // ---- 1. 合成：HDR + Bloom ----
    vec3 combined = hdrColor + bloomColor * bloomStrength;

    // ---- 2. 曝光调整 ----
    combined *= exposure;

    // ---- 3. 高光压缩 ----
    combined = compressHighlights(combined, highlightThreshold, highlightKnee);

    // ---- 4. ACES 色调映射 ----
    vec3 a = combined * (2.51 * combined + 0.03);
    vec3 b = combined * (2.43 * combined + 0.59) + 0.14;
    vec3 mapped = clamp(a / b, 0.0, 1.0);

    // ---- 5. Gamma 校正 ----
    mapped = pow(mapped, vec3(1.0 / 2.2));

    FragColor = vec4(mapped, 1.0);
}
