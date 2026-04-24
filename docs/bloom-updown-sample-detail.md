# Bloom 上下采样逐行代码详解

> 本文逐行讲解上下采样的每一句代码，重点说清**各级之间如何关联**、**数据如何流动**、**为什么这样写**。

---

## 前置理解：什么是 Mip 链？

Bloom 的核心是一个 **Mip 链**（Mipmap Chain）——一组分辨率逐级减半的纹理：

```
bloomFBOs[0]  600×400    ← 第0级，最精细
bloomFBOs[1]  300×200    ← 第1级
bloomFBOs[2]  150×100    ← 第2级
bloomFBOs[3]  75×50      ← 第3级
bloomFBOs[4]  37×25      ← 第4级
bloomFBOs[5]  18×12      ← 第5级
bloomFBOs[6]  9×6        ← 第6级
bloomFBOs[7]  4×3        ← 第7级，最粗糙
```

**关键规则**：每一级的 1 个像素 = 上一级 2×2 = 4 个像素的平均。所以分辨率越低，1 个像素"看到"的世界面积越大。

---

## 下采样（Downsample）

### 目标

把亮度提取的结果逐级缩小，每级同时做模糊。

### 数据流

```
brightnessFBO (1200×800)
       │
       ▼ 下采样+模糊
bloomFBOs[0] (600×400)     ← 只有一点点模糊
       │
       ▼ 下采样+模糊
bloomFBOs[1] (300×200)     ← 更多模糊
       │
       ▼ 下采样+模糊
bloomFBOs[2] (150×100)     ← 更更多模糊
       │
      ...
       │
       ▼ 下采样+模糊
bloomFBOs[7] (4×3)         ← 极度模糊（整个画面缩成4×3）
```

**每一级都更模糊**，因为分辨率低了，采样核覆盖的"世界面积"更大。

### C++ 控制代码逐行解读

```cpp
// ========== Pass 2: 逐级下采样 ==========
bloomDownsampleShader->begin();  // 只需 begin 一次，所有级共用同一个 shader
```

#### 第一级：从 brightnessFBO → bloomFBOs[0]

```cpp
// 第一步：绑定输出目标——把渲染结果写到 bloomFBOs[0]
bloomFBOs[0]->bind();
// bloomFBOs[0] 是 600×400，所以 GPU 自动用 600×400 的 viewport

// 第二步：清空目标，防止上一帧的残留
CK(glClear(GL_COLOR_BUFFER_BIT));

// 第三步：绑定输入纹理——从 brightnessFBO 读取数据
brightnessFBO->bindColorTexture(0);
// bindColorTexture(0) 的意思是：把这个 FBO 的颜色纹理绑定到纹理单元 0
// 后面 shader 里的 sampler2D srcTexture 就会从这个纹理单元读取

// 第四步：告诉 shader 输入纹理的分辨率（用于计算像素偏移量）
bloomDownsampleShader->setUniformFloat("srcResolution", (float)screenWidth);
// 这里 srcResolution = 1200（屏幕宽度）
// shader 里会用它算 texelSize = 1/1200

// 第五步：告诉 shader 从哪个纹理单元读取
bloomDownsampleShader->setUniformTexture2D("srcTexture", 0);
// "srcTexture" 是 shader 里的 sampler2D 变量名
// 0 是纹理单元编号，和上面 bindColorTexture(0) 对应

// 第六步：画全屏四边形——触发 shader 执行
drawFullscreenQuad();
// 每个像素执行一次 bloom_downsample.frag
// 输出写入 bloomFBOs[0]

// 第七步：解绑输出目标
bloomFBOs[0]->unbind();
```

**这一步做了什么？**

```
输入：brightnessFBO (1200×800) 的亮度提取结果
输出：bloomFBOs[0] (600×400) 的模糊缩小版

shader 里每个输出像素，从输入纹理的 3×3 邻域采样 13 个点加权平均
因为输出分辨率是输入的一半，所以自动完成了"缩小"
同时 13-tap 采样做了模糊
```

#### 后续级：从 bloomFBOs[i-1] → bloomFBOs[i]

```cpp
for (int i = 1; i < BLOOM_MIP_LEVELS; i++) {
    // 绑定当前级的 FBO 作为输出目标
    bloomFBOs[i]->bind();
    CK(glClear(GL_COLOR_BUFFER_BIT));

    // 绑定上一级 FBO 的纹理作为输入
    bloomFBOs[i-1]->bindColorTexture(0);
    //  ↑ 关键！读取的是 i-1 级（上一级）的结果

    bloomDownsampleShader->setUniformTexture2D("srcTexture", 0);

    // srcResolution 用上一级的宽度（不是屏幕宽度！）
    bloomDownsampleShader->setUniformFloat("srcResolution", (float)bloomFBOs[i-1]->getWidth());
    //  ↑ 为什么用上一级的宽度？
    //  因为 shader 需要知道输入纹理的分辨率来计算 texelSize
    //  输入纹理是 bloomFBOs[i-1]，所以用它的宽度

    drawFullscreenQuad();
    bloomFBOs[i]->unbind();
}
```

**逐级数据流图示**：

```
i=1: brightnessFBO → bloomFBOs[0]  （已完成，不在循环里）
i=1: bloomFBOs[0]  → bloomFBOs[1]  输入600×400  输出300×200
i=2: bloomFBOs[1]  → bloomFBOs[2]  输入300×200  输出150×100
i=3: bloomFBOs[2]  → bloomFBOs[3]  输入150×100  输出75×50
i=4: bloomFBOs[3]  → bloomFBOs[4]  输入75×50    输出37×25
i=5: bloomFBOs[4]  → bloomFBOs[5]  输入37×25    输出18×12
i=6: bloomFBOs[5]  → bloomFBOs[6]  输入18×12    输出9×6
i=7: bloomFBOs[6]  → bloomFBOs[7]  输入9×6      输出4×3
```

每一步的输入是上一级的输出，形成链式传递。

```cpp
bloomDownsampleShader->end();  // 所有级都处理完了，结束 shader
```

### Shader 代码逐行解读

```glsl
// bloom_downsample.frag

uniform sampler2D srcTexture;   // ← 由 C++ 的 setUniformTexture2D("srcTexture", 0) 设置
uniform float srcResolution;    // ← 由 C++ 的 setUniformFloat("srcResolution", ...) 设置

void main() {
    // texelSize = 一个像素在 UV 空间里的尺寸
    // 比如输入纹理宽 600，那 texelSize.x = 1/600 ≈ 0.00167
    vec2 texelSize = vec2(1.0) / srcResolution;

    vec3 result = vec3(0.0);

    // === 中心采样（权重最大 0.5）===
    result += texture(srcTexture, vUV).rgb * 0.5;
    // texture(srcTexture, vUV) 取当前像素位置的颜色
    // 乘以 0.5 是因为中心最重要

    // === 十字采样（上下左右，各 0.125）===
    float x = texelSize.x;  // 一个像素的 UV 偏移量
    float y = texelSize.y;

    result += texture(srcTexture, vUV + vec2( x,  0.0)).rgb * 0.125;  // 右边1像素
    result += texture(srcTexture, vUV + vec2(-x,  0.0)).rgb * 0.125;  // 左边1像素
    result += texture(srcTexture, vUV + vec2(0.0,  y )).rgb * 0.125;  // 上面1像素
    result += texture(srcTexture, vUV + vec2(0.0, -y )).rgb * 0.125;  // 下面1像素

    // === 对角采样（四个角，各 0.03125）===
    vec2 d1 = vec2( x,  y) * 1.5;  // 右上1.5像素
    vec2 d2 = vec2(-x,  y) * 1.5;  // 左上1.5像素
    vec2 d3 = vec2( x, -y) * 1.5;  // 右下1.5像素
    vec2 d4 = vec2(-x, -y) * 1.5;  // 左下1.5像素

    result += texture(srcTexture, vUV + d1).rgb * 0.03125;
    result += texture(srcTexture, vUV + d2).rgb * 0.03125;
    result += texture(srcTexture, vUV + d3).rgb * 0.03125;
    result += texture(srcTexture, vUV + d4).rgb * 0.03125;

    FragColor = vec4(result, 1.0);
}
```

**为什么不同级不需要不同的 shader？**

因为 shader 是参数化的！`srcResolution` 这个 uniform 每帧被 C++ 设置成不同值：
- 第0级：`srcResolution = 1200`，`texelSize = 1/1200`
- 第1级：`srcResolution = 600`，`texelSize = 1/600`
- 第2级：`srcResolution = 300`，`texelSize = 1/300`

同一个 shader，不同的 `srcResolution`，自动适配不同分辨率。

**模糊半径的"魔法"**

第7级时，纹理只有 4×3 像素。`texelSize = 1/4 = 0.25`。
十字采样的偏移量 `x = 0.25`，在 UV 空间里偏移了整个纹理的 1/4！
这在原始分辨率下等价于偏移 300 个像素——巨大的模糊半径。

这就是多级下采样的核心优势：**用很小的采样核，在低分辨率下获得大半径模糊**。

---

## 上采样（Upsample）

### 目标

从最粗糙的级别开始，逐级放大回精细级别，每次把低分辨率的结果**累加**到高分辨率上。

### 核心概念：为什么是累加？

下采样产生了多个不同模糊半径的版本：
- bloomFBOs[0]：轻微模糊
- bloomFBOs[7]：极度模糊

我们要的效果是**所有模糊半径叠加在一起**——既有近处的微弱光晕，也有远处的大范围光晕。

如果只取最粗一级，那只有大范围光晕，缺少近距离的细节。
如果只取最细一级，那只有小范围光晕，缺少大范围的柔和扩散。

**累加 = 1级 + 2级 + ... + 8级 = 全范围光晕**。

### 数据流

```
bloomFBOs[7] (4×3)    ──上采样──→ 累加到 bloomFBOs[6]
                                      ↓
bloomFBOs[6] (9×6)    ──上采样──→ 累加到 bloomFBOs[5]
                                      ↓
bloomFBOs[5] (18×12)  ──上采样──→ 累加到 bloomFBOs[4]
                                      ↓
                   ...
                                      ↓
bloomFBOs[1] (300×200)──上采样──→ 累加到 bloomFBOs[0]
                                      ↓
                            bloomFBOs[0] = 最终 Bloom 结果
```

### C++ 控制代码逐行解读

```cpp
// ========== Pass 3: 逐级上采样 + 累加 ==========
bloomUpsampleShader->begin();
bloomUpsampleShader->setUniformFloat("filterRadius", bloomFilterRadius);
// filterRadius 控制上采样时的模糊扩散程度
// 0.01 表示采样偏移 1% 的 UV 空间

// ★★★ 关键：开启加法混合 ★★★
CK(glEnable(GL_BLEND));
CK(glBlendFunc(GL_ONE, GL_ONE));
// GL_ONE, GL_ONE 的含义：
//   输出颜色 = 源颜色 × 1 + 目标颜色 × 1
//   = 新画的颜色 + FBO 里已有的颜色
//   = 累加！不是替换！
//
// 默认的混合模式是 GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA（alpha 混合）
// 那样会覆盖而不是累加，效果完全不同
```

#### 循环：从最粗级到最细级

```cpp
for (int i = BLOOM_MIP_LEVELS - 1; i > 0; i--) {
    // i 从 7 开始递减到 1
    // 每次把 bloomFBOs[i] 上采样到 bloomFBOs[i-1]

    // 第一步：绑定目标 FBO（写入）
    bloomFBOs[i-1]->bind();
    // ★ 注意：不清空！因为加法混合要在已有内容上累加！
    // 下采样时写入的值还保留着，上采样的结果会叠加上去

    // 第二步：绑定源纹理（读取）—— 低分辨率级别
    bloomFBOs[i]->bindColorTexture(0);
    // ↑ 读取 i 级的纹理，写入 i-1 级的 FBO

    bloomUpsampleShader->setUniformTexture2D("srcTexture", 0);
    bloomUpsampleShader->setUniformFloat("srcResolution", (float)bloomFBOs[i]->getWidth());
    // ↑ srcResolution 用源纹理（低分辨率）的宽度

    // 第三步：画全屏四边形——触发上采样 shader
    drawFullscreenQuad();
    // 因为加法混合，shader 输出 += FBO 已有内容
    // 相当于：bloomFBOs[i-1] = bloomFBOs[i-1]（原有）+ upsample(bloomFBOs[i]）

    bloomFBOs[i-1]->unbind();
}

CK(glDisable(GL_BLEND));  // 关闭加法混合，恢复默认
bloomUpsampleShader->end();
```

#### 逐级详细过程

```
i=7: 读 bloomFBOs[7](4×3)    → 上采样 → 累加到 bloomFBOs[6](9×6)
     bloomFBOs[6] = 原有下采样值 + bloomFBOs[7]的上采样结果

i=6: 读 bloomFBOs[6](9×6)    → 上采样 → 累加到 bloomFBOs[5](18×12)
     bloomFBOs[5] = 原有下采样值 + bloomFBOs[6]的上采样结果
     而 bloomFBOs[6] 已经包含了 bloomFBOs[7] 的信息
     所以 bloomFBOs[5] 间接包含了 bloomFBOs[7] 和 bloomFBOs[6] 的信息

i=5: 读 bloomFBOs[5](18×12)  → 上采样 → 累加到 bloomFBOs[4](37×25)
     bloomFBOs[4] 间接包含了 [5][6][7] 的信息

i=4: → 累加到 bloomFBOs[3]，间接包含 [4][5][6][7]
i=3: → 累加到 bloomFBOs[2]，间接包含 [3][4][5][6][7]
i=2: → 累加到 bloomFBOs[1]，间接包含 [2][3][4][5][6][7]
i=1: → 累加到 bloomFBOs[0]，间接包含 [1][2][3][4][5][6][7]

最终 bloomFBOs[0] = 所有 8 个级别的累加！
```

**这就是为什么最后只用 bloomFBOs[0]**——它已经包含了所有级别的信息。

### Shader 代码逐行解读

```glsl
// bloom_upsample.frag

uniform sampler2D srcTexture;   // 低分辨率级别的纹理
uniform float srcResolution;    // 低分辨率级别的宽度
uniform float filterRadius;     // 采样偏移量（0.01）

void main() {
    vec2 texelSize = vec2(1.0) / srcResolution;
    // 对于 9×6 的纹理，texelSize ≈ (0.111, 0.167)

    // ★ Tent Filter：4个偏移采样（菱形分布）★
    vec3 result = vec3(0.0);

    // filterRadius 是 UV 空间的偏移量
    // 0.01 表示在 UV 空间偏移 1%
    // 对于 600×400 的纹理，1% = 6像素
    result += texture(srcTexture, vUV + vec2( filterRadius,  0.0)).rgb;  // 右偏
    result += texture(srcTexture, vUV + vec2(-filterRadius,  0.0)).rgb;  // 左偏
    result += texture(srcTexture, vUV + vec2( 0.0,  filterRadius)).rgb;  // 上偏
    result += texture(srcTexture, vUV + vec2( 0.0, -filterRadius)).rgb;  // 下偏

    // 4个采样取平均
    result *= 0.25;

    FragColor = vec4(result, 1.0);
}
```

**为什么偏移采样能产生模糊？**

想象一个纯白的像素（1,1,1），周围全是黑色（0,0,0）：

```
不偏移：采样到 (1,1,1)，输出 = (1,1,1) → 锐利

偏移0.01：
  右偏采样点可能落在黑区 → (0,0,0)
  左偏采样点可能落在黑区 → (0,0,0)
  上偏采样点可能落在白区 → (1,1,1)
  下偏采样点可能落在黑区 → (0,0,0)
  平均 = (0.25, 0.25, 0.25) → 变暗了 → 边缘柔化了！
```

偏移越大，跨越黑白边界的概率越高，模糊越强。

**上采样 + 双线性插值 = 额外模糊**

当低分辨率纹理被放大到高分辨率 FBO 时，GPU 的双线性插值（`GL_LINEAR`）会自动在相邻像素间做平滑。这本身也是一层模糊，和 tent filter 的模糊叠加，让光晕更加柔和。

---

## 下采样 vs 上采样对比

| | 下采样 | 上采样 |
|---|---|---|
| 方向 | 从细到粗（i=0→7） | 从粗到细（i=7→1） |
| 输入 | 上一级的高分辨率纹理 | 当前级的低分辨率纹理 |
| 输出 | 当前级的低分辨率 FBO | 上一级的高分辨率 FBO |
| 混合模式 | 普通写入（替换） | 加法混合（累加） |
| 清空目标 | 是（glClear） | 否（保留已有内容，往上加） |
| 模糊方式 | 13-tap 采样加权平均 | 4点 tent filter + 双线性插值 |
| 模糊半径 | 由 texelSize 决定（低级大） | 由 filterRadius 决定 |

---

## 最终合成

```cpp
// Pass 4: 色调映射 + Bloom 合成
tonemapShader->begin();

// 绑定两个输入纹理
hdrFBO->bindColorTexture(0);          // 纹理单元0：原始 HDR 场景
bloomFBOs[0]->bindColorTexture(1);    // 纹理单元1：Bloom 结果（所有级别的累加）

tonemapShader->setUniformTexture2D("hdrTexture", 0);   // 对应纹理单元0
tonemapShader->setUniformTexture2D("bloomTexture", 1); // 对应纹理单元1
tonemapShader->setUniformFloat("bloomStrength", 0.18f);
```

对应 shader：
```glsl
vec3 hdrColor  = texture(hdrTexture, vUV).rgb;    // 原始场景
vec3 bloomColor = texture(bloomTexture, vUV).rgb;  // Bloom 光晕

vec3 combined = hdrColor + bloomColor * bloomStrength;
//                        ↑ 0.18 控制光晕亮度
// 最终 = 原始场景 + 一层朦胧光晕
```

---

## 可视化总结

```
下采样阶段（从左到右，越来越小越来越模糊）：

[1200×800] → [600×400] → [300×200] → [150×100] → ... → [4×3]
  亮度提取     轻微模糊    中等模糊     较大模糊             极度模糊

上采样阶段（从右到左，累加到上一级）：

[4×3] ──→ [9×6] ──→ [18×12] ──→ ... ──→ [600×400]
  ↑累加      ↑累加       ↑累加               ↑累加
  结果包含    结果包含     结果包含             最终结果=
  [7]        [6]+[7]     [5]+[6]+[7]         [0]+[1]+...+[7]
                                                  = 全范围Bloom
```
