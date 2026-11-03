# 31 · 增量式边函数与 SIMD 光栅化

> 边函数随 x/y 线性变化，逐像素遍历只需增量更新。本篇讲覆盖规则、绕序归一化与权重映射，以及 SSE2 路径。

边函数随 x/y 线性变化，所以逐像素遍历只需增量更新。当前实现将覆盖与插值分开：
覆盖使用共享整数子像素规则，深度及属性继续使用浮点插值。

## 覆盖规则

`sr::TriangleCoverage` 把屏幕位置四舍五入到 1/256 像素网格，中点远离零。
以 y 向上的屏幕坐标为准，逆时针归一化后，顶边与左边包含边界，其余边不包含。
通过在非包含边的整数边式中减一实现，不给浮点插值加偏置。

相同端点构成的共享边只能属于一个三角形，遍历起点不同也不会因浮点累计产生重复覆盖或裂缝。
坐标绝对值超过 2^20、非有限或量化后退化的几何不绘制；这个范围用于保证 int64 乘法和步进不溢出。

```cpp
auto coverage = sr::TriangleCoverage::create(a, b, c);
if (!coverage) return;
auto bounds = coverage->bounds(width, height);
if (bounds.empty()) return;
auto row = coverage->atPixel(bounds.minX, bounds.minY);
for (int y = bounds.minY; y <= bounds.maxY; ++y) {
    auto pixel = row;
    for (int x = bounds.minX; x <= bounds.maxX; ++x) {
        if (sr::TriangleCoverage::contains(pixel)) { /* 深度与着色 */ }
        coverage->advanceX(pixel);
    }
    coverage->advanceY(row);
}
```

`containsSample` 使用同一规则判定 MSAA 子样本。快速步进只用于 bounds 所限定的扫描区间，
不能对任意构造的整数值无限次累计。裸 barycentric 仍是数学插值函数，不能用其全非负判断替代覆盖契约。

## 绕序与权重映射

- Renderer 按原始屏幕面积执行可选背面剔除；关闭 `cullBackface` 后可绘制两种绕序。
- `TriangleCoverage` 对量化后几何归一化边方向，并应用 y 向上的 top-left 归属规则。
- 浮点插值边式乘面积符号，`invArea` 使用绝对面积的倒数；覆盖偏置不参与插值。

边函数与属性权重是错位映射：边 01 对应顶点 2，边 12 对应顶点 0，边 20 对应顶点 1。
因此深度表达式是 `(e1*z0 + e2*z1 + e0*z2) * invArea`。

## SSE2 路径

`raster::fillTriangleSSE2` 用两个 `__m128i` 二元组表示四个像素的 int64 边值。
对三条边按位 OR 后提取每个 64 位元素的符号位，得到覆盖掩码，再用 `_mm_add_epi64` 一次推进四像素。
`_mm_castsi128_pd` 仅用于位解释和 movemask，不做浮点运算。尾部不足四像素使用同一整数覆盖规则。

`fillTriangleIncremental`、SSE2、基础三角形填充与 Renderer 共用 `coverage.h`。
浮点 `setupEdges` 仍负责快速路径深度插值；边 01/12/20 分别对应顶点 2/0/1 的权重。
快速路径只接受常量颜色，仍未接入 Renderer 的 Shader/Varyings 接口。

## 验证与性能

覆盖测试检查独立矩形期望、两种绕序、MSAA 子样本、裁剪后的共享边、极值和扫描起点；
以互不相等的顶点深度验证权重，并覆盖绕序反转、绘制顺序与裁剪后覆盖。
SSE2/标量对照覆盖宽度 1–17 的所有尾部情况与非对称深度，独立数值期望另由原深度测试保留。

没有来源和复现实验的"4–8 倍加速"表格已移除。主管线基线见
[覆盖规则与性能基线](../engineering/15-性能基准框架.md)，SSE2 实验内核速度不能替代整帧性能结论。

## 对应代码

`include/softrenderer/render/raster/coverage.h`、`render/pipeline/renderer.h`、`render/raster/rasterizer_fast.h`。
