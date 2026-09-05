# 23 · Alpha 混合与透明渲染

> 不透明物体用深度测试解决遮挡；半透明物体必须用**混合（blending）**——新颜色与已有颜色按 alpha 加权合成。本篇讲清混合公式、排序要求、以及预乘 alpha 的优势。

## 一、混合公式

通用形式：`result = src * srcFactor + dst * dstFactor`

| 模式 | srcFactor | dstFactor | 用途 |
|---|---|---|---|
| AlphaBlend | srcA | 1-srcA | 标准半透明（玻璃、水） |
| Additive | 1 | 1 | 发光、粒子、HDR bloom |
| Multiply | dst | 0 | 阴影、暗化 |
| Premultiplied | 1 | 1-srcA | 预乘 alpha（推荐，避免边缘黑边） |

## 二、排序要求

混合**不满足交换律**：A 叠 B ≠ B 叠 A。所以透明物体必须**从远到近**画（画家算法）。这正是 `RenderQueue` 对 `Transparent` 层做远→近排序的原因。

不透明物体先画（写深度），透明物体后画（不写深度，只混合）。

## 三、预乘 alpha 的优势

普通 alpha 纹理的边缘纹素通常是 `(0,0,0,0)`（透明黑）。混合时 `src*srcA + dst*(1-srcA)` 中 `src*0 = 0`，但双线性插值会在边缘混入黑色 → 黑边。

预乘 alpha 在导入时就把 RGB 乘上 A 存好，混合公式变成 `src*1 + dst*(1-srcA)`，插值时黑色纹素的 RGB 已经是 0，不会产生额外暗化。

## 四、对应代码

| 概念 | 代码 |
|---|---|
| 混合模式枚举 | `core/blend.h`：`BlendMode` |
| 像素混合函数 | `blendPixels(src, dst, mode)` |
| 渲染队列排序 | `scene/render_queue.h`：透明层远→近 |

## 五、动手实验

1. 画两个半透明三角形重叠，交换绘制顺序，观察颜色差异。
2. 把 `AlphaBlend` 换成 `Premultiplied`，对比边缘质量。

---

上一篇：[22 · 增量式边函数与 SIMD 光栅化](22-增量式边函数与SIMD光栅化.md)
下一篇：[24 · CMake 工程化：安装、导出与版本](../engineering/24-CMake工程化-安装导出与版本.md)
