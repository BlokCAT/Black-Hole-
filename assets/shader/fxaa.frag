#version 460 core

// ============================================================
// FXAA 3.11 Quality — 最高质量预设
// 基于 Timothy Lottes, NVIDIA 2011
//
// 之前的效果不明显，原因是：
//   1. 边缘搜索只走了 2 步（原版高质量走 8~12 步）
//   2. 混合公式简化了
//   3. 边缘阈值偏高
//
// 这个版本使用 FXAA_CONSOLE/PC 的 Quality 29 预设，
// 沿边缘方向正负各搜索 4 步，精度大幅提升
// ============================================================

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D srcTexture;
uniform vec3 resolution;

// ---- FXAA 参数（Quality 29 预设）----
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 4.0
#define FXAA_QUALITY__P4 12.0

float fxaaQualitySubpix = 0.75;          // 子像素混合强度
float fxaaQualityEdgeThreshold = 0.063;  // 边缘阈值（降低=更灵敏，0.063适合HDR后）
float fxaaQualityEdgeThresholdMin = 0.0312; // 最小阈值

// ---- 辅助函数 ----
float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 texelSize = 1.0 / resolution.xy;

    // ============================================================
    // 第1步：采样 3x3 邻域
    // ============================================================
    vec3 rgbM = texture(srcTexture, vUV).rgb;
    float lumM = luminance(rgbM);

    float lumN = luminance(texture(srcTexture, vUV + vec2( 0.0,  texelSize.y)).rgb);
    float lumS = luminance(texture(srcTexture, vUV + vec2( 0.0, -texelSize.y)).rgb);
    float lumW = luminance(texture(srcTexture, vUV + vec2(-texelSize.x,  0.0)).rgb);
    float lumE = luminance(texture(srcTexture, vUV + vec2( texelSize.x,  0.0)).rgb);

    float lumNW = luminance(texture(srcTexture, vUV + vec2(-texelSize.x,  texelSize.y)).rgb);
    float lumNE = luminance(texture(srcTexture, vUV + vec2( texelSize.x,  texelSize.y)).rgb);
    float lumSW = luminance(texture(srcTexture, vUV + vec2(-texelSize.x, -texelSize.y)).rgb);
    float lumSE = luminance(texture(srcTexture, vUV + vec2( texelSize.x, -texelSize.y)).rgb);

    // ============================================================
    // 第2步：早期退出 — 不在边缘上的像素直接返回
    // ============================================================
    float lumMax = max(lumM, max(max(lumN, lumS), max(lumW, lumE)));
    float lumMin = min(lumM, min(min(lumN, lumS), min(lumW, lumE)));

    // 亮度对比度不够 → 不在边缘上 → 跳过
    float range = lumMax - lumMin;
    if (range < max(fxaaQualityEdgeThresholdMin, lumMax * fxaaQualityEdgeThreshold)) {
        FragColor = vec4(rgbM, 1.0);
        return;
    }

    // ============================================================
    // 第3步：确定边缘方向（水平 vs 垂直）
    // 梯度大 = 边缘法线方向，沿法线方向做模糊
    // ============================================================
    float edgeHorizontal =
        abs(lumN + lumS - 2.0 * lumM) * 2.0 +
        abs(lumNW + lumSW - 2.0 * lumW) +
        abs(lumNE + lumSE - 2.0 * lumE);

    float edgeVertical =
        abs(lumW + lumE - 2.0 * lumM) * 2.0 +
        abs(lumNW + lumNE - 2.0 * lumN) +
        abs(lumSW + lumSE - 2.0 * lumS);

    bool isHorizontal = edgeHorizontal >= edgeVertical;

    // ============================================================
    // 第4步：选择搜索方向
    // 沿边缘法线方向，选择梯度更大的一侧作为"正方向"
    // ============================================================
    float lum1 = isHorizontal ? lumN : lumW;
    float lum2 = isHorizontal ? lumS : lumE;
    float gradient1 = abs(lum1 - lumM);
    float gradient2 = abs(lum2 - lumM);

    bool isPositive = gradient1 >= gradient2;

    // 搜索步长：沿边缘法线方向，1像素
    vec2 stepDir = isHorizontal ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);
    stepDir = isPositive ? stepDir : -stepDir;

    // 梯度缩放：用于判断是否到达边缘端点
    float gradientScaled = max(gradient1, gradient2) * 0.25;

    // 中点两侧的亮度
    float lumA = isPositive ? lum1 : lum2;
    float lumB = lumM;

    // ============================================================
    // 第5步：沿边缘正负方向搜索端点（Quality 29 预设）
    // 每个方向搜索 4 步，步长逐渐增大（1, 1.5, 2, 4, 12 像素）
    // ============================================================

    // ---- 正方向搜索 ----
    vec2 posA = vUV + stepDir * FXAA_QUALITY__P0;
    float lumAtA = luminance(texture(srcTexture, posA).rgb);
    bool reachedA = abs(lumAtA - lumA) >= gradientScaled;
    if (!reachedA) {
        posA += stepDir * FXAA_QUALITY__P1;
        lumAtA = luminance(texture(srcTexture, posA).rgb);
        reachedA = abs(lumAtA - lumA) >= gradientScaled;
        if (!reachedA) {
            posA += stepDir * FXAA_QUALITY__P2;
            lumAtA = luminance(texture(srcTexture, posA).rgb);
            reachedA = abs(lumAtA - lumA) >= gradientScaled;
            if (!reachedA) {
                posA += stepDir * FXAA_QUALITY__P3;
                lumAtA = luminance(texture(srcTexture, posA).rgb);
                reachedA = abs(lumAtA - lumA) >= gradientScaled;
                if (!reachedA) {
                    posA += stepDir * FXAA_QUALITY__P4;
                    lumAtA = luminance(texture(srcTexture, posA).rgb);
                    reachedA = abs(lumAtA - lumA) >= gradientScaled;
                }
            }
        }
    }

    // ---- 负方向搜索 ----
    vec2 posB = vUV - stepDir * FXAA_QUALITY__P0;
    float lumAtB = luminance(texture(srcTexture, posB).rgb);
    bool reachedB = abs(lumAtB - lumA) >= gradientScaled;
    if (!reachedB) {
        posB -= stepDir * FXAA_QUALITY__P1;
        lumAtB = luminance(texture(srcTexture, posB).rgb);
        reachedB = abs(lumAtB - lumA) >= gradientScaled;
        if (!reachedB) {
            posB -= stepDir * FXAA_QUALITY__P2;
            lumAtB = luminance(texture(srcTexture, posB).rgb);
            reachedB = abs(lumAtB - lumA) >= gradientScaled;
            if (!reachedB) {
                posB -= stepDir * FXAA_QUALITY__P3;
                lumAtB = luminance(texture(srcTexture, posB).rgb);
                reachedB = abs(lumAtB - lumA) >= gradientScaled;
                if (!reachedB) {
                    posB -= stepDir * FXAA_QUALITY__P4;
                    lumAtB = luminance(texture(srcTexture, posB).rgb);
                    reachedB = abs(lumAtB - lumA) >= gradientScaled;
                }
            }
        }
    }

    // ============================================================
    // 第6步：计算混合因子
    // ============================================================

    // 当前像素到两个端点的距离
    float distA = isPositive ? length(posA - vUV) : length(vUV - posB);
    float distB = isPositive ? length(vUV - posB) : length(posA - vUV);

    // 选择更近的端点
    bool isCloserA = distA < distB;
    float dist = min(distA, distB);

    // 边缘混合因子：像素离近端点越近，混合越少（已经在正确的一侧了）
    float blendFactor = 0.5 - dist / (distA + distB);

    // ---- 子像素混合 ----
    // 用 3x3 邻域的亮度方差检测子像素细节
    float lumAvg = (lumN + lumS + lumW + lumE + lumM) / 5.0;
    float lumVar = (abs(lumN - lumAvg) + abs(lumS - lumAvg)
                  + abs(lumW - lumAvg) + abs(lumE - lumAvg)) / 4.0;
    float subpixBlend = max(0.0, lumVar / max(range, 0.001)) * fxaaQualitySubpix;

    // 最终混合 = max(边缘混合, 子像素混合)
    blendFactor = max(blendFactor, subpixBlend);

    // ============================================================
    // 第7步：混合最终颜色
    // 沿边缘法线方向偏移采样，得到抗锯齿后的颜色
    // ============================================================
    vec2 offset = stepDir * blendFactor;
    vec3 rgbF = texture(srcTexture, vUV + offset).rgb;

    FragColor = vec4(rgbF, 1.0);
}
