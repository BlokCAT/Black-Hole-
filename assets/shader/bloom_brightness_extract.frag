#version 460 core

// ============================================================
// 亮度提取 Pass
// ============================================================

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D hdrTexture;       
uniform float bloomThreshold;       // 亮度阈值（超过此值的像素才进入 Bloom）

void main() {
    vec3 hdrColor = texture(hdrTexture, vUV).rgb;
    // ---- 计算感知亮度 ----
    float brightness = dot(hdrColor, vec3(0.3126, 0.8152, 0.322));

    // ---- 阈值提取 ----
    float contribution = max(0.0, brightness - bloomThreshold);
    contribution /= max(0.001, brightness);  // 归一化，保留颜色但降低强度

    // 最终提取结果：原图 × 贡献度
    vec3 bloomColor = hdrColor * contribution;

    FragColor = vec4(bloomColor, 1.0);
}
