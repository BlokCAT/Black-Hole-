# Bloom（泛光）实现详解

> 本文档讲解项目中 Progressive Bloom 的完整实现原理，适合了解基本图形学概念（渲染管线、shader、纹理）但没写过后处理的读者。

---

## 目录

1. [Bloom 是什么？为什么需要它？](#1-bloom-是什么为什么需要它)
2. [为什么必须先有 HDR？](#2-为什么必须先有-hdr)
3. [整体管线一览](#3-整体管线一览)
4. [Pass 0：渲染 HDR 场景](#4-pass-0渲染-hdr-场景)
5. [Pass 1：亮度提取（Brightness Extract）](#5-pass-1亮度提取brightness-extract)
6. [Pass 2：逐级下采样（Downsample）](#6-pass-2逐级下采样downsample)
7. [Pass 3：逐级上采样（Upsample）](#7-pass-3逐级上采样upsample)
8. [Pass 4：色调映射 + 合成（Tone Mapping）](#8-pass-4色调映射--合成tone-mapping)
9. [Pass 5：FXAA 抗锯齿](#9-pass-5fxaa-抗锯齿)
10. [参数调优指南](#10-参数调优指南)
11. [常见问题](#11-常见问题)

---

## 1. Bloom 是什么？为什么需要它？

### 现实世界中的 Bloom

你有没有这样的经验：正午看太阳，太阳周围会有一圈"光晕"扩散开来？或者夜晚看路灯，灯泡周围有一团朦胧的发光？

这不是眼睛的bug，而是**真实的光学现象**：
- 相机镜头的玻璃表面会反射和散射光线
- 人眼的角膜和晶状体也会让强光"溢出"到周围

### 渲染中的问题

在显示器上，一个像素最大亮度就是 1.0（纯白）。如果场景里有一个亮度 10.0 的光源，显示出来和亮度 1.0 的白色像素**看起来一模一样**——都是纯白。

这就丢失了"这个光源比那个亮很多"的信息。

### Bloom 的作用

Bloom 让**亮的东西"溢出"光芒**：

```
没有 Bloom：  亮处 = 纯白方块，和周围暗处有硬边界
有 Bloom：    亮处 = 周围有柔和光晕，越亮光晕越大
```

它本质上回答了这个问题：**"这个像素有多亮？"**——而不仅仅是"它是白色吗？"

---

## 2. 为什么必须先有 HDR？

### SDR vs HDR

| | SDR（Standard Dynamic Range） | HDR（High Dynamic Range） |
|---|---|---|
| 像素值范围 | [0, 1] | [0, +∞) |
| 纹理格式 | `GL_RGBA8`（每通道8位） | `GL_RGBA16F`（每通道16位浮点） |
| 能存多亮？ | 最亮就是 1.0 | 可以存 10.0、100.0 甚至更大 |
| Bloom 效果 | 差（无法区分"白"和"非常白"） | 好（知道哪些像素真正亮） |

### 关键理解

Bloom 的核心是**区分亮和非常亮**。

在 SDR 里，一个像素值 = 0.9（接近白）和 1.0（纯白），差别只有 0.1。Bloom 阈值设 0.8 的话，两者都会被提取，但无法区分谁更亮。

在 HDR 里，一个像素值 = 0.9 和 5.0（非常亮），差别巨大。Bloom 能精确地只让 5.0 的像素发光，0.9 的不发光。

### 我们的解决方案

```
原来的流程（错误）：
  shader 计算（HDR值）→ Reinhard 色调映射 → Gamma → 写屏幕
  问题：HDR 数据在 shader 内部就被压成 [0,1]，Bloom 拿不到

正确的流程：
  shader 计算（HDR值）→ 写入浮点 FBO → Bloom 处理 → 色调映射 → 写屏幕
  关键：先存 HDR，后处理，最后才映射
```

---

## 3. 整体管线一览

```
┌─────────────────────────────────────────────────────────────┐
│                    完整渲染管线                               │
│                                                              │
│  Pass 0: 渲染黑洞场景                                        │
│    blackhole.frag → hdrFBO (RGBA16F, 1200×800)              │
│    ↑ 输出 HDR 颜色，不做色调映射                              │
│                                                              │
│  Pass 1: 亮度提取                                            │
│    bloom_brightness_extract.frag → brightnessFBO             │
│    ↑ 只保留亮度 > 阈值的像素                                  │
│                                                              │
│  Pass 2: 逐级下采样 ×N                                       │
│    bloom_downsample.frag → bloomFBOs[0] → [1] → ... → [N-1] │
│    ↑ 每级分辨率减半 + 模糊                                    │
│                                                              │
│  Pass 3: 逐级上采样                                          │
│    bloom_upsample.frag: [N-1] → [N-2] → ... → [0]           │
│    ↑ 每级上采样并累加到上一级（加法混合）                      │
│                                                              │
│  Pass 4: 色调映射 + 合成                                     │
│    tonemap_composite.frag → tonemapFBO (RGBA8)               │
│    ↑ HDR + Bloom ×强度 → ACES色调映射 → Gamma               │
│                                                              │
│  Pass 5: FXAA 抗锯齿                                        │
│    fxaa.frag → 屏幕                                          │
│    ↑ 在 LDR 空间检测边缘并柔化                                │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### FBO 一览

| FBO | 格式 | 分辨率 | 用途 |
|---|---|---|---|
| `hdrFBO` | RGBA16F | 1200×800 | 存原始 HDR 场景 |
| `brightnessFBO` | RGBA16F | 1200×800 | 存亮度提取结果 |
| `bloomFBOs[0]` | RGBA16F | 600×400 | 第1级下采样 |
| `bloomFBOs[1]` | RGBA16F | 300×200 | 第2级下采样 |
| ... | ... | ... | ... |
| `bloomFBOs[7]` | RGBA16F | 4×3 | 第8级下采样 |
| `tonemapFBO` | RGBA8 | 1200×800 | 色调映射后的 LDR 结果 |

---

## 4. Pass 0：渲染 HDR 场景

### 做什么

把黑洞的光线追踪结果渲染到 `hdrFBO`（浮点纹理），**不做任何色调映射**。

### 关键代码

```glsl
// blackhole.frag 末尾
vec3 finalColor = color * (1.0 - diskColor.a) + diskColor.rgb;

// 直接输出 HDR，不做色调映射！
// 色调映射在单独的后处理 pass 中完成
FragColor = vec4(finalColor, 1.0);
```

### 为什么不能在这里做色调映射？

```glsl
// ❌ 之前的错误做法
finalColor = finalColor / (finalColor + vec3(1.0));  // Reinhard
finalColor = pow(finalColor, vec3(1.0 / 2.2));       // Gamma
FragColor = vec4(finalColor, 1.0);
```

如果在这里做了色调映射，写进 FBO 的值已经被压到 [0,1]，后面的 Bloom 就分不清"白"和"非常白"了。

### FBO 为什么用 RGBA16F？

`RGBA16F` = 每个通道 16 位浮点数。可以存负数、可以存超过 1.0 的值。

比如吸积盘内侧蓝移区域，`intensity` 可能达到 50.0 甚至更高，`RGBA8`（0~255 整数）存不下这种值，会被截断成 1.0。

---

## 5. Pass 1：亮度提取（Brightness Extract）

### 做什么

从 HDR 场景中，**只保留亮度超过阈值（threshold）的像素**，其余归零。

### 类比

想象你有一张照片，你只想要"特别亮"的部分（太阳、灯泡、高光反射），其余全涂黑。这就是亮度提取。

### shader 原理

```glsl
// bloom_brightness_extract.frag

vec3 hdrColor = texture(hdrTexture, vUV).rgb;

// 计算感知亮度（人眼对绿色最敏感）
float brightness = dot(hdrColor, vec3(0.2126, 0.7152, 0.0722));

// 只保留亮度 > threshold 的部分
float contribution = max(0.0, brightness - bloomThreshold);
contribution /= max(0.001, brightness);  // 归一化：保留颜色，降低强度

vec3 bloomColor = hdrColor * contribution;
```

### 关键理解：contribution 的含义

```
假设 threshold = 0.8

像素A: brightness = 0.5 → contribution = max(0, 0.5-0.8)/0.5 = 0     → 不提取
像素B: brightness = 1.0 → contribution = max(0, 1.0-0.8)/1.0 = 0.2   → 提取20%亮度
像素C: brightness = 3.0 → contribution = max(0, 3.0-0.8)/3.0 = 0.733 → 提取73%亮度
像素D: brightness = 0.8 → contribution = 0                              → 刚好在阈值上，不提取
```

越亮的像素被提取的比例越高，暗像素直接为 0。

### 为什么不直接用 `if (brightness > threshold)`？

硬截断会在阈值边界产生**锯齿**——一边完全黑，一边突然亮。用 `contribution` 做软过渡更平滑。

---

## 6. Pass 2：逐级下采样（Downsample）

### 做什么

把亮度提取的结果反复缩小到一半分辨率，每次缩小同时做模糊。

### 为什么需要多级？

如果只做一次模糊，光晕半径受限于一个像素能采样到的范围。分辨率 600×400 时，5×5 的采样核只能覆盖几个像素的光晕。

**多级的魔法**：在低分辨率下，1个像素覆盖的"世界面积"更大。比如 1/8 分辨率时，1个像素 = 原来 8×8=64 个像素的区域。一次简单的模糊在这里等效于高分辨率下的大半径模糊。

```
原始 (1200×800):  光晕半径 ≈ 1像素
1/2  (600×400):   光晕半径 ≈ 2像素（等效）
1/4  (300×200):   光晕半径 ≈ 4像素（等效）
1/8  (150×100):   光晕半径 ≈ 8像素（等效）
...
```

### 采样模式（13-tap 滤波）

```glsl
// bloom_downsample.frag

// 中心采样（权重0.5）
result += texture(srcTexture, vUV).rgb * 0.5;

// 十字采样（上下左右，各权重0.125）
result += texture(srcTexture, vUV + vec2( x,  0.0)).rgb * 0.125;
result += texture(srcTexture, vUV + vec2(-x,  0.0)).rgb * 0.125;
result += texture(srcTexture, vUV + vec2(0.0,  y )).rgb * 0.125;
result += texture(srcTexture, vUV + vec2(0.0, -y )).rgb * 0.125;

// 对角采样（各权重0.03125）
result += texture(srcTexture, vUV + d1).rgb * 0.03125;
...（4个对角方向）
```

### 为什么这样分配权重？

```
权重总和 = 0.5 + 4×0.125 + 4×0.03125 = 0.5 + 0.5 + 0.125 = 1.125

这看起来不对？——实际上是利用了双线性插值！
当你在一个 texel 的偏移位置采样时，GPU 自动平均4个相邻像素。
所以13个采样点实际覆盖了更多信息。
```

核心思想：**中心权重最大（0.5），近邻次之（0.125），对角最弱（0.03125）**。这模拟了高斯模糊的"中间亮、边缘暗"特性。

---

## 7. Pass 3：逐级上采样（Upsample）

### 做什么

从最粗（分辨率最低）的一级开始，逐级放大回原分辨率，并累加到上一级。

### 核心概念：累加而非替换

下采样产生了不同半径的模糊版本。上采样要把它们**全部叠加**在一起。

```
bloomFBOs[7]（最粗，最大模糊）  ─→ 上采样加到 bloomFBOs[6]
bloomFBOs[6]（= 原有 + [7]的）  ─→ 上采样加到 bloomFBOs[5]
bloomFBOs[5]（= 原有 + [6]的）  ─→ 上采样加到 bloomFBOs[4]
...
bloomFBOs[1]（= 原有 + [2]的）  ─→ 上采样加到 bloomFBOs[0]
bloomFBOs[0] 最终 = 所有级的累加！
```

### 加法混合（Additive Blending）

```cpp
// main.cpp 中关键代码
glEnable(GL_BLEND);
glBlendFunc(GL_ONE, GL_ONE);  // dst = src + dst（加法混合）
```

- `GL_ONE, GL_ONE` 意味着：**新像素 = 源颜色 × 1 + 目标颜色 × 1**
- 即源和目标直接相加，不做任何衰减
- 这样上采样的结果就**叠加**到了下采样已有的结果上

### Tent Filter

```glsl
// bloom_upsample.frag

// 在4个偏移位置采样（菱形分布）
result += texture(srcTexture, vUV + vec2( filterRadius,  0.0)).rgb;
result += texture(srcTexture, vUV + vec2(-filterRadius,  0.0)).rgb;
result += texture(srcTexture, vUV + vec2( 0.0,  filterRadius)).rgb;
result += texture(srcTexture, vUV + vec2( 0.0, -filterRadius)).rgb;
result *= 0.25;  // 4个采样取平均
```

`filterRadius` 控制偏移量，越大 → 采样点越远 → 模糊扩散越大。

---

## 8. Pass 4：色调映射 + 合成（Tone Mapping）

### 做什么

1. 把 HDR 场景 + Bloom 结果合成
2. 用 ACES 曲线把 [0, +∞) 压到 [0, 1]
3. 做 Gamma 校正

### 为什么需要色调映射？

显示器只能显示 [0, 1] 的值。HDR 像素值可能是 5.0、50.0 甚至更大。如果直接显示，超过 1.0 的部分全是纯白，细节全丢。

色调映射就是一个**优雅的压缩函数**，让高值被压缩但不丢失相对关系。

### ACES 色调映射

```glsl
// tonemap_composite.frag

vec3 a = combined * (2.51 * combined + 0.03);
vec3 b = combined * (2.43 * combined + 0.59) + 0.14;
vec3 mapped = clamp(a / b, 0.0, 1.0);
```

这是 Stephen Hill 对 ACES（Academy Color Encoding System）的近似拟合。曲线特性：

```
输入    输出
0.0  →  0.00   （黑色保持黑色）
0.5  →  0.47   （中间调几乎线性）
1.0  →  0.80   （白色被压缩）
2.0  →  0.94   （高光被大幅压缩但仍有区分）
5.0  →  0.99   （极亮值接近1但不到1）
10.0 →  ~1.0   （几乎饱和）
```

与简单的 Reinhard（`x/(1+x)`）相比，ACES 的优势：
- 暗部更通透（不会灰蒙蒙）
- 高光保留更多色偏（蓝白不是直接变白）
- 整体对比度更高

### Gamma 校正

```glsl
mapped = pow(mapped, vec3(1.0 / 2.2));
```

显示器输入的是 sRGB 信号（有 Gamma 曲线），而我们的计算是线性的。`1/2.2 ≈ 0.45` 的幂运算把线性值转换到 sRGB 空间。

### 为什么用 RGBA8 的 tonemapFBO？

色调映射之后值已经在 [0, 1]，不再需要浮点精度。用 `RGBA8` 节省显存，而且 FXAA 在 LDR 空间工作效果更好。

---

## 9. Pass 5：FXAA 抗锯齿

### 做什么

检测画面中的边缘像素，沿边缘方向做子像素混合，消除锯齿。

### 为什么不在 HDR 空间做 FXAA？

FXAA 通过亮度差检测边缘。HDR 空间里一个像素亮度 0.5、另一个 50.0，差了 100 倍，FXAA 会认为这里有一条"超级边缘"，导致过度的模糊。

色调映射后，亮度被压到 [0, 1]，边缘检测更合理。

### FXAA 的三步走

1. **检测边缘**：采样 3×3 邻域，算最大亮度差。差太小 → 不在边缘上 → 跳过
2. **确定方向**：水平梯度 vs 垂直梯度，哪个大就是哪个方向的边缘
3. **沿边缘混合**：找到边缘的两个端点，按距离混合颜色

详见 `fxaa.frag` 中的详细注释。

---

## 10. 参数调优指南

### Bloom 相关

| 参数 | 位置 | 默认值 | 效果 |
|---|---|---|---|
| `bloomThreshold` | main.cpp | 0.6 | 阈值越高，只有越亮的像素发光；越低，更多像素参与发光 |
| `bloomStrength` | main.cpp | 0.18 | 最终光晕强度，0 = 关闭，1 = 极强 |
| `bloomFilterRadius` | main.cpp | 0.01 | 上采样模糊半径，越大光晕越扩散 |
| `BLOOM_MIP_LEVELS` | main.cpp | 8 | 下采样级数，越多 → 最粗级分辨率越低 → 光晕半径越大 |

### 调参口诀

- **光晕太小看不到** → 降低 `bloomThreshold`（让更多像素发光）或增大 `bloomStrength`
- **光晕太大太糊** → 增大 `bloomThreshold` 或减小 `bloomStrength`
- **光晕不够"扩散"** → 增大 `BLOOM_MIP_LEVELS` 或 `bloomFilterRadius`
- **画面整体太亮** → 减小 `exposure` 或 `bloomStrength`

### 快捷键

| 按键 | 功能 |
|---|---|
| B | 开关 Bloom |
| 上/下方向键 | 调整 Bloom 强度 |
| F | 开关 FXAA |

---

## 11. 常见问题

### Q: 为什么 Bloom 效果很弱，几乎看不到？

A: 最可能的原因是 `bloomThreshold` 太高。场景中亮度值如果普遍在 0.5~1.0 之间，阈值 0.8 会过滤掉大部分像素。试试降低到 0.3~0.5。

### Q: 为什么 Bloom 让整个画面都变亮了？

A: `bloomStrength` 太大，或者 `bloomThreshold` 太低导致几乎所有像素都被提取。Bloom 应该只影响高光区域，不是全局提亮。

### Q: 为什么光晕有"方块感"？

A: 下采样级数不够（`BLOOM_MIP_LEVELS` 太小），导致大半径模糊不足。最粗级分辨率至少要低于 10×10 才能有足够大的模糊半径。

### Q: 为什么色调映射后画面灰蒙蒙的？

A: 可能是 `exposure` 太低。ACES 曲线在低曝光时会压缩对比度。试着增大 `exposure` 到 1.5~2.0。

### Q: FXAA 让画面变模糊了怎么办？

A: FXAA 本质就是"智能模糊"，在边缘检测不准时会过度模糊。可以：
1. 关掉 FXAA（按 F 键）
2. 降低 `fxaaQualitySubpix`（在 fxaa.frag 中，从 0.75 降到 0.25）

---

## 参考资料

- [John Chapman - Real-Time Volumetric Rendering](https://john-chapman-graphics.blogspot.com/)
- [Jorge Jimenez - Next Generation Post Processing in Call of Duty: Advanced Warfare (SIGGRAPH 2016)](https://www.youtube.com/watch?v=VI8l4z_Dj3c)
- [Stephen Hill - ACES Filmic Tone Mapping](https://twitter.com/self_shadow/status/1156140445289375744)
- [Timothy Lottes - FXAA 3.11](https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf)
