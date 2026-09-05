# 22 · 增量式边函数与 SIMD 光栅化

> 朴素光栅化每像素算 3 次叉积（6 乘 6 减）。增量式边函数把它降为 3 次加法；SSE2 再让 4 像素同时判断内外。两者叠加，光栅化内循环提速 4-8 倍。

## 一、增量式边函数

边函数 $E(x,y) = \text{cross2}(\text{edge},\; p - v)$ 对 $x, y$ 是**线性**的：

$$E(x+1, y) = E(x,y) + \frac{\partial E}{\partial x}$$
$$E(x, y+1) = E(x,y) + \frac{\partial E}{\partial y}$$

偏导数是常数（只取决于边向量），所以：

- 行首算一次 $E_0, E_1, E_2$
- 行内逐像素加 $\partial E/\partial x$
- 换行加 $\partial E/\partial y$

**每像素从 6 乘 6 减 → 3 加**。深度 $z$ 同理增量递推。

## 二、SSE2 四像素并行

用 `__m128` 同时维护 4 个连续 x 像素的 3 条边函数值：

1. `_mm_cmpge_ps(e, zero)` 得 4 个比较结果
2. `_mm_and_ps` 三条边取交
3. `_mm_movemask_ps` 提取 4-bit mask
4. mask == 0xF → 4 像素全内，批量写
5. mask != 0 → 逐 bit 处理
6. 增量用 `_mm_add_ps(e, de4)` 一次 +4

尾部不足 4 像素回退标量。

## 三、对应代码

| 概念 | 代码 |
|---|---|
| 边函数增量预计算 | `raster/rasterizer_fast.h`：`setupEdges` |
| 标量增量光栅化 | `fillTriangleIncremental` |
| SSE2 并行光栅化 | `fillTriangleSSE2` |
| 统一入口 | `fillTriangleFast`（自动选 SSE2/标量） |

## 四、性能对比（参考值）

| 方法 | 每像素操作 | 800×600 场景 |
|---|---|---|
| 朴素 barycentric | 6 乘 + 6 减 + 3 除 | ~120ms |
| 增量标量 | 3 加 | ~35ms |
| 增量 + SSE2 | 3 加 / 4 像素 | ~12ms |

## 五、动手实验

1. 在 `stage12_threaded` 里把光栅化换成 `raster_fast::fillTriangleFast`，对比帧时间。
2. 把 SSE2 的 4 像素组改成 8 像素（AVX `_mm256`），看是否进一步提速。

---

上一篇：[21 · 项目架构总览与进阶路线](../engineering/21-项目架构总览与进阶路线.md)
下一篇：[23 · Alpha 混合与透明渲染](23-Alpha混合与透明渲染.md)
