# 55 · IBL 环境反射

> 金属材质在没有环境光时是全黑的——因为金属没有漫反射，只有镜面反射。IBL（Image-Based Lighting）用 cubemap 提供"来自所有方向的光"，让金属真正"亮"起来。

## 一、原理

PBR 的光照分两部分：

1. **直接光**：太阳/点光源 → Cook-Torrance BRDF
2. **间接光（IBL）**：环境 cubemap → 镜面反射 + 漫反射辐照度

### 镜面 IBL

用反射向量 `R = reflect(-V, N)` 采样 cubemap，乘以菲涅尔 F 和粗糙度衰减。

### 漫反射 IBL

用法线 N 采样 cubemap 的辐照度近似（半球积分），乘以 `kD * albedo`。

## 二、用法

```cpp
Cubemap envMap;
generateSkyCubemap(envMap, 64); // 或从 HDR 文件加载

PBRShader shader;
shader.envMap = &envMap; // 一行开启 IBL
```

## 三、效果对比

| 无 IBL | 有 IBL |
|---|---|
| 金属全黑 | 金属反射天空 |
| 非金属只有直接光 | 非金属有环境色染色 |

## 四、对应代码

| 文件 | 作用 |
|---|---|
| `include/softrenderer/render/shader/pbr_shader.h` | IBL 采样逻辑 |
| `include/softrenderer/resource/cubemap.h` | Cubemap 类 + 程序化天空生成 |
| `examples/demo_ibl_pbr.cpp` | IBL + PBR 完整 demo |

---

上一篇：[54 · 项目当前状态总览](../engineering/54-项目当前状态总览.md)
下一篇：[56 · glTF 纹理加载](56-glTF纹理加载.md)
