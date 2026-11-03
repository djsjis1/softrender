# 33 · IBL 环境光照与 Cubemap

> 真实世界的光不只来自几个点光源——天空、墙壁、地面都在反射光。PBR 的金属 `metallic=1` 时漫反射为 0，没有环境光就全黑。IBL（Image-Based Lighting）用一张 cubemap 近似"来自所有方向的光"，让金属球反射天空、非金属表面带上环境色。

## 一、直接光与间接光

PBR 的光照分两部分：

1. **直接光**：太阳/点光源 → Cook-Torrance BRDF（见 [25 · PBR 光照](25-PBR光照-Cook-Torrance-BRDF.md)）
2. **间接光（IBL）**：环境 cubemap → 镜面反射 + 漫反射辐照度

金属没有漫反射项，只有镜面反射；没有环境可反射时物理上确实全黑，但视觉上不可接受。IBL 提供"虚拟环境光"解决这个问题。

## 二、Cubemap 结构

6 张面（+X/-X/+Y/-Y/+Z/-Z），用方向向量选面并映射 UV：

```
选面：找 |x|,|y|,|z| 最大者 → 确定面
UV：用另外两个分量 + 归一化 + [-1,1]→[0,1]
采样：双线性
```

## 三、两种 IBL 用途

| 用途 | 采样方向 | 说明 |
|---|---|---|
| 镜面反射 | 反射向量 `R = reflect(-V, N)` | 金属球映出天空，结果再乘菲涅尔 F 与粗糙度衰减 |
| 漫反射辐照度 | 法线 `N` 的半球积分 | 环境色"染色"非金属表面，再乘 `kD * albedo` |

漫反射辐照度 = 对法线半球内所有方向采样 cubemap，cos 加权平均。本项目用 Fibonacci 半球采样近似。

## 四、效果对比

| 无 IBL | 有 IBL |
|---|---|
| 金属全黑 | 金属反射天空 |
| 非金属只有直接光 | 非金属有环境色染色 |

## 五、代码入口

```cpp
Cubemap envMap;
generateSkyCubemap(envMap, 64);      // 程序化天空；也可从 HDR equirectangular 加载

PBRShader shader;
shader.envMap = &envMap;             // 一行启用 IBL
```

| 文件 | 作用 |
|---|---|
| `include/softrenderer/render/shader/pbr_shader.h` | IBL 采样逻辑 |
| `include/softrenderer/resource/cubemap.h` | `Cubemap` 类 + 程序化天空生成 |
| `examples/demo_ibl_pbr.cpp` | IBL + PBR 完整 demo |

## 六、当前实现与局限

- **镜面 IBL**：`sampleReflection(R, roughness)` 按粗糙度选择 mip 层，用 box filter mip 链近似预滤波。不是真正的 GGX 预滤波（按 NDF 卷积），高光形状会比离线渲染更"圆"。
- **漫反射 IBL**：`sampleIrradiance(N, samples = 32)` 用 Fibonacci 半球实时采样，非预计算卷积。生产级实现会用预计算的 diffuse irradiance map。
- 六面必须全部有效：只检查第一面会让缺失面的采样落到默认 Texture（无 mip），因此构造函数逐面校验。

## 七、动手实验

1. 在 `demo_ibl_pbr` 中注释 `shader.envMap = &envMap`——金属球变全黑。
2. 把 `generateSkyCubemap` 的 zenith/horizon 颜色互换（白天→日落）——反射颜色跟着变。
3. 把 `sampleIrradiance` 的采样数从 32 改到 4——漫反射 IBL 变噪。
4. 把材质的 `roughness` 从 0.1 调到 0.9——观察镜面反射从清晰到模糊（mip 层变化）。

## 八、小结

- IBL = 镜面反射（R 采样）+ 漫反射辐照度（N 半球积分）。
- Cubemap 用方向向量选面，双线性采样。
- 粗糙度通过 mip 层近似，漫反射用 Fibonacci 半球采样近似。
