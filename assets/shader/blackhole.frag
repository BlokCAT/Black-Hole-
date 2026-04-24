#version 460 core
#define PI 3.14159265359

in vec2 vUV;
out vec4 FragColor;

uniform mat4 inverseVP;
uniform vec3 cameraPos;
uniform vec3 blackholePos;
uniform float schwarzschildRadius;
uniform samplerCube environmentMap;
uniform float time;


// 3D星星哈希：输入方向，输出亮度
float starHash(vec3 p) {
    p = fract(p * vec3(123.45, 678.91, 234.56));
    p += dot(p, p.yzx + 45.67);
    return fract(p.x * p.y * p.z);
}

float hash3D(vec3 p) {
    p = fract(p * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yxz + 19.19);
    return fract((p.x + p.y) * p.z);
}
// 极简纯净星空
vec3 proceduralStarfield(vec3 dir) {
    // 1. 划分空间网格 (缩放系数决定星星的密集度，250左右比较合适)
    vec3 p = dir * 250.0;
    vec3 id = floor(p);       // 获取当前像素所在的格子ID
    vec3 f = fract(p) - 0.5;  // 获取当前像素在格子内的局部坐标 [-0.5, 0.5]

    // 2. 基于格子ID生成一个伪随机数 (0.0 到 1.0)
    // 这样做保证了同一个格子里的像素共用一个随机属性，不会乱闪
    float randVal = fract(sin(dot(id, vec3(12.9898, 78.233, 54.53))) * 43758.5453);

    float star = 0.0;
    
    // 3. 极度克制：只允许大约 1.5% 的格子发光
    if (randVal > 0.985) {
        // 计算当前像素到格子中心的距离
        float dist = length(f);
        
        // 使用 smoothstep 画出一个边缘平滑的小圆点，避免锯齿
        // 0.1 决定了星星的绝对大小，越小星星越细微
        star = smoothstep(0.1, 0.0, dist);
        
        // 利用随机数让不同格子的星星有明暗差异
        star *= (randVal - 0.985) * 60.0; 
    }

    // 统一给一点极其微弱的冷色调（偏蓝白），让星空显得清澈
    return vec3(star) * vec3(0.9, 0.95, 1.0); 
}


float noise3D(vec3 p, float periodZ) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    // Z方向取模：角度方向周期循环，消除接缝
    float iz0 = mod(i.z, periodZ);
    float iz1 = mod(i.z + 1.0, periodZ);

    // 构造8个顶点：X和Y不变，Z用取模后的值
    vec3 i000 = vec3(i.x,   i.y,   iz0);
    vec3 i100 = vec3(i.x+1, i.y,   iz0);
    vec3 i010 = vec3(i.x,   i.y+1, iz0);
    vec3 i110 = vec3(i.x+1, i.y+1, iz0);
    vec3 i001 = vec3(i.x,   i.y,   iz1);
    vec3 i101 = vec3(i.x+1, i.y,   iz1);
    vec3 i011 = vec3(i.x,   i.y+1, iz1);
    vec3 i111 = vec3(i.x+1, i.y+1, iz1);

    // 三线性插值（和原来一样，只是顶点坐标变了）
    return mix(
        mix(
            mix(hash3D(i000), hash3D(i100), u.x),
            mix(hash3D(i010), hash3D(i110), u.x),
            u.y
        ),
        mix(
            mix(hash3D(i001), hash3D(i101), u.x),
            mix(hash3D(i011), hash3D(i111), u.x),
            u.y
        ),
        u.z
    );
}


vec3 acceleration(vec3 pos, vec3 vel) {
    vec3 toBlackhole = blackholePos - pos;
    float dist = length(toBlackhole);
    float safeDist = max(dist, 0.01);  
    vec3 rHat = normalize(toBlackhole); 
    float r = safeDist;
    float Rs = schwarzschildRadius;
    // 光速归一化后的速度方向
    vec3 vHat = normalize(vel);
    // 径向速度分量（正值=朝向黑洞）
    float vr = dot(vHat, rHat);
    vec3 aNewton = (Rs * 0.5) / (r * r) * rHat;
    float rSq = r * r;

    // GR 径向修正
    vec3 aGR_radial = -1.5 * Rs / rSq * (vr * vr - 1.0/3.0) * rHat;

    // GR 切向修正
    vec3 aGR_tangent = 3.0 * Rs / rSq * vr * vHat;
    return aNewton + aGR_radial + aGR_tangent;
}

float fbm3D_Dust(vec3 p, float periodZ) {
    float noiseAccumulator = 1.0;
    float freq = 2.0;
    float curPeriod = periodZ;
    
    for (int i = 0; i < 4; i++) {
        float n = noise3D(p * freq, curPeriod) * 2.0 - 1.0;
        noiseAccumulator +=  n;
        freq *= 2.0;
        curPeriod *= 2.0;  // 频率翻倍，周期也翻倍，保持一致
    }
    return noiseAccumulator;
}
uniform float diskOuterRadius = 5.5;   // 外半径（Rs倍数）
uniform float diskThickness   = 0.09;  // 厚度（Rs倍数）
void sampleDiskVolume(vec3 worldPos, vec3 rayDir, out float density, out vec3 emission) {
    density = 0.0; 
    emission = vec3(0.0);

    vec3 local = worldPos - blackholePos;  
    float Rs = schwarzschildRadius;
    float rXZ = length(local.xz);          
    float absY = abs(local.y);

    float innerR = Rs * 1.3;//定义吸积盘的内边缘
    float outerR = diskOuterRadius * Rs;//定义吸积盘的外边缘
    // 径向
    float radialFade = smoothstep(innerR, innerR * 1.15, rXZ)
                     * smoothstep(outerR, outerR * 0.8, rXZ);
    //计算 h
    float radialT = clamp((rXZ - innerR) / (outerR - innerR), 0.0, 1.0); //将径向位置映射到0-1
    float thicknessScale = log2(2.3 - radialT);
    float diskH = diskThickness * Rs *thicknessScale; //算出厚度
    // 垂直
    float verticalGaussian = exp(-(local.y * local.y) / (2.0 * diskH * diskH));
    float baseDensity = verticalGaussian * radialFade ;
    if (baseDensity < 0.001) return;  

    // ==========================================
    // 2. 极坐标噪声采样 + 旋臂扭曲 + 旋转

    float angle = atan(local.z, local.x);

    // 旋臂扭曲：越靠近黑洞扭曲越强（和距离的倒数相关）
    float twistStrength = 4.0;
    float staticTwist = twistStrength * pow(Rs / rXZ, 3);

    // 全局自转（慢一点）
    float omega = 0.08;
    float globalRotation = time *omega;
    float spiralAngle = angle - staticTwist - globalRotation;

    // 角度方向：旋臂扭曲后的角度
    float basePeriod = 10.0; 
    float normalizedAngle = fract(spiralAngle / (2.0 * PI)); 
    float polarZ = normalizedAngle * basePeriod;

    // 径向：加入气体流入效果
    float inflowSpeed = 0.07;  // 降低吸入速度
    float polarX = rXZ * 1.4 + time * inflowSpeed;

    vec3 dustCoord = vec3(polarX, local.y * 4.0, polarZ);
    float rawNoise = fbm3D_Dust(dustCoord, basePeriod) * 2.0 - 1.0;
    float dustVal = pow(clamp(rawNoise, 0.0, 1.0), 6.0);

    //着色部分
    float t = clamp(1.0 - (rXZ - innerR) / (outerR - innerR), 0.0, 1.0);
    density = baseDensity * dustVal ;

    // 3. 多普勒效应
    // 气体在盘平面上做圆周运动，速度方向 = 切线方向
    // 切线 = cross(盘法线, 径向方向) = cross(y轴, 径向)
    vec3 radialDir = normalize(vec3(local.x, 0.0, local.z));
    vec3 tangentDir = cross(vec3(0.0, 1.0, 0.0), radialDir);

    // 气体轨道速度：全局自转角速度 × r

    // 线速度随半径衰减：内圈多普勒强，外圈弱
    float gasSpeed = omega * innerR * pow(innerR / rXZ, 0.5);
    vec3 gasVelocity = tangentDir * gasSpeed;

    // 多普勒因子
    // v_rel > 0 → 蓝移(更亮更蓝)，v_rel < 0 → 红移(更暗更红)
    float v_rel = dot(-rayDir, gasVelocity);
    float doppler = 1.0 + v_rel * 2.0;  // 2.0 控制多普勒强度
    doppler = clamp(doppler, 0.01, 2.0);  // 限制范围

    // 黑体辐射近似色温
    vec3 colorOut = vec3(0.4, 0.02, 0.0);   // 外缘 ~1500K：极暗红
    vec3 colorMid = vec3(1.0, 0.45, 0.05);  // 中段 ~3500K：橙红
    vec3 colorIn  = vec3(0.7, 0.85, 1.0);   // 内缘 ~10000K：蓝白

    vec3 diskColor;
    if (t < 0.5) {
        diskColor = mix(colorOut, colorMid, t * 2.0);
    } else {
        diskColor = mix(colorMid, colorIn, (t - 0.5) * 2.0);
    }

    // 多普勒亮度调制：蓝移更亮，红移更暗（不对称）
    float intensity = pow(t, 2.5) * 5.0 * dustVal;
    if (doppler > 1.0) {
        intensity *=1;   // 蓝移：d=2 → 11倍亮
    } else {
        intensity *= pow(max(0.001, doppler), 6.0);  // 红移：d=0.4 → 降到极暗
    }

    // 多普勒颜色偏移：柔和过渡，不会割裂
    vec3 blueShift  = vec3(0.2, 0.25, 1.0);   // 蓝移色
    vec3 redShift   = vec3(0.4, 0.0, 0.0);    // 红移色
    float shiftAmount = max(-1.0f, min(1.0f, (doppler - 1.0) * 1.2f)); // 正=蓝移，负=红移

    if (shiftAmount > 0.0) {
        diskColor = mix(diskColor, blueShift, shiftAmount * 2.9);   // 0.6 控制蓝移颜色强度
    } else {
        diskColor = mix(diskColor, redShift, -shiftAmount *2.3);   // 0.6 控制红移颜色强度
    
    }

    emission = diskColor * intensity;
}
vec3 traceRay(vec3 origin, vec3 dir, out float outMinDist, out vec4 outDiskColor) {
    float h = 0.06; 
    int maxSteps = 1200;  // GR修正后光子可能绕多圈，需要更多步数
    vec3 pos = origin;
    vec3 vel = dir;
    float minDist = 1e10;
    outDiskColor = vec4(0.0);

    float jitter = fract(sin(dot(vUV, vec2(12.9898, 78.233))) * 43758.5453) * 0.4;
    pos += vel * 0.06 * jitter;

    float Rs = schwarzschildRadius;
    float innerR = Rs * 1.3;
    float outerR = diskOuterRadius * Rs;
    float diskH = diskThickness * Rs;
    
    float maxCutoffY = diskH * 3.5;

    for (int i = 0; i < maxSteps; i++) {
        vec3 toBlackhole = blackholePos - pos;
        float dist = length(toBlackhole);
        minDist = min(minDist, dist);

        // 超出范围 → 终止
        if (dist > 1650.0) break;
        // 吸积盘不透明度已满 → 终止
        if (outDiskColor.a > 0.9999) break;
        // 进入视界 → 终止（光子被黑洞捕获）
        if (dist < schwarzschildRadius * 0.5) break;

        vec3 local = pos - blackholePos;
        float rXZ = length(local.xz);
        float absY = abs(local.y);

        // 自适应步长
        float stepSize = 0.05;
        if (dist < schwarzschildRadius * 4.0) {
            // 靠近视界：步长更小，让光子环更锐利
            stepSize = mix(0.02, 0.03, (dist - schwarzschildRadius * 0.5) / (schwarzschildRadius * 3.5));
        }
        // 体积采样（用最大cutoff，sampleDiskVolume内部会根据实际diskH判断）
        if (rXZ >= innerR && rXZ <= outerR && absY < maxCutoffY) {
            float d;
            vec3 e_val;
            sampleDiskVolume(pos, vel, d, e_val);

            if (d > 0.00001) {
                float sd = d * stepSize * 1.5;
                outDiskColor.rgb += (1.0 - outDiskColor.a) * sd * e_val;
                outDiskColor.a   += (1.0 - outDiskColor.a) * sd;
            }
        }

        // ---- RK4积分更新光线方向 ----
        vec3 a1 = acceleration(pos, vel);
        vec3 a2 = acceleration(pos + vel * stepSize * 0.5, vel + a1 * stepSize * 0.5);
        vec3 a3 = acceleration(pos + (vel + a1 * stepSize * 0.5) * stepSize * 0.5, vel + a2 * stepSize * 0.5);
        vec3 a4 = acceleration(pos + (vel + a2 * stepSize) * stepSize, vel + a3 * stepSize);
        vel = normalize(vel + (a1 + 2.0*a2 + 2.0*a3 + a4) * stepSize / 6.0);
        
        pos += vel * stepSize;
    }
    
    outMinDist = minDist;
    return vel;
}
void main() {
    // 从屏幕UV重建世界空间光线
    vec4 clipPos = vec4(vUV * 2.0 - 1.0, -1.0, 1.0);
    vec4 worldPos = inverseVP * clipPos;
    worldPos /= worldPos.w;
    vec3 rayDir = normalize(worldPos.xyz - cameraPos);

    // 光线追踪
    float minDist;
    vec4 diskColor;
    vec3 finalDir = traceRay(cameraPos, rayDir, minDist, diskColor);

    // 背景：光子进入视界→全黑
    // 在施瓦西黑洞中，进入光子球（r < 1.5Rs）的光子必然落入视界
    // 所以用 1.5Rs 作为判断阈值（而非 Rs）
    vec3 color;
    if (minDist < schwarzschildRadius )
        color = vec3(0.0);
    else
        color = proceduralStarfield(finalDir);
        // HDR环境贴图（需要加载HDR文件时取消注释下面这行，注释上面那行）
        // color = texture(environmentMap, finalDir).rgb;

    // 合成：背景 × (1-盘不透明度) + 盘颜色
    vec3 finalColor = color * (1.0 - diskColor.a) + diskColor.rgb;

    // 直接输出 HDR 颜色，不做色调映射
    // 色调映射在单独的后处理 pass 中完成
    FragColor = vec4(finalColor, 1.0);
}
