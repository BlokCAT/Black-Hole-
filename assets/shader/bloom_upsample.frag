#version 460 core
// ============================================================
// Bloom 上采样 Pass
// ============================================================
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D srcTexture;   // 当前低分辨率级别的
uniform float srcResolution;    // 当前级别纹理的像素尺寸
uniform float filterRadius;     // 滤波半径（控制模糊扩散程度，通常 0.005~0.02）

void main() {
    // 在低分辨率纹理上采样4个偏移点，利用双线性插值
    vec2 texelSize = vec2(1.0) / srcResolution;

    // 4个偏移采样点（呈菱形分布）
    // filterRadius 控制采样偏移量，越大光晕扩散越远
    vec3 result = vec3(0.0);
    result += texture(srcTexture, vUV + vec2( filterRadius,  0.0)).rgb;
    result += texture(srcTexture, vUV + vec2(-filterRadius,  0.0)).rgb;
    result += texture(srcTexture, vUV + vec2( 0.0,  filterRadius)).rgb;
    result += texture(srcTexture, vUV + vec2( 0.0, -filterRadius)).rgb;

    // 4个采样平均
    result *= 0.25;

    FragColor = vec4(result, 1.0);
}
