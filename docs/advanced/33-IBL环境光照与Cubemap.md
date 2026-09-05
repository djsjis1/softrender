# 33 · IBL 环境光照与 Cubemap

> 真实世界的光不只来自几个点光源——天空、墙壁、地面都在反射光。IBL（Image-Based Lighting）用一张环境贴图（cubemap）近似"来自所有方向的光"，让金属球能反射天空、非金属表面有环境色。

## 一、为什么需要 IBL

PBR 的金属材质 `metallic=1` 时漫反射为 0，只有镜面反射。没有环境光来源时，金属全黑——这在物理上是正确的（因为没有光可反射），但在视觉上不可接受。

**IBL 提供"虚拟的环境光"**——一个 cubemap 模拟周围环境的光照，来自所有方向。金属表面反射这个环境就"亮"了。

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
| 镜面反射 | 反射向量 R = reflect(-V, N) | 金属球映出天空 |
| 漫反射辐照度 | 法线 N 的半球积分 | 环境色"染色"非金属表面 |

漫反射辐照度 = 对法线半球内所有方向采样 cubemap，cos 加权平均。本项目用 Fibonacci 半球 16 次采样近似。

## 四、代码入口

```cpp
Cubemap envMap;
generateSkyCubemap(envMap, 64);      // 程序化天空
// 或 loaded from HDR equirectangular

PBRShader shader;
shader.envMap = &envMap;             // 一行启用 IBL
```

## 五、当前简化

- **镜面 IBL**：直接采样原始 cubemap，忽略 roughness（相当于只有 mip 0）。完整实现需要预滤波 mipmap 链
- **漫反射 IBL**：16 次 Fibonacci 采样，非预计算卷积。生产效率用预计算的 diffuse irradiance map

## 六、动手实验

1. 在 `demo_ibl_pbr` 中注释 `shader.envMap = &envMap`——金属球变全黑
2. 把 `generateSkyCubemap` 的 zenith/horizon 颜色互换（白天→日落）——反射颜色跟着变
3. 把 `sampleIrradiance` 的采样数从 16 改到 4——漫反射 IBL 变噪

---

上一篇：`advanced/32-G-Buffer`
下一篇：`advanced/34-骨骼动画`
