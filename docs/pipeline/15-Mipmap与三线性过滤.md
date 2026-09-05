# 15 · Mipmap 与三线性过滤

> 远处纹理闪烁（摩尔纹）的根源：一个像素覆盖几十个纹素，只采 4 个必然漏信息。Mipmap 预存逐级缩小的纹理链，采样时按像素覆盖面积选合适的层级。

## 一、问题：远处为什么会闪烁

相机离地面很远时，一个屏幕像素可能覆盖纹理上的几十甚至上百个纹素。双线性采样只取最近的 4 个纹素，中间的大量纹素信息全部丢失——每个像素采到的 4 个点随机落在不同纹素上，帧与帧之间跳变，形成**摩尔纹闪烁**。

**解法**：预先生成一系列逐级缩小的纹理（mipmap 链），远处像素用缩小后的低分辨率层级采样——此时每个像素覆盖的纹素数接近 1，不再闪烁。

## 二、Mipmap 链生成

从原始纹理（level 0）开始，每级用 **2×2 box filter** 缩半：

```
level 0: 256×256  ← 原始
level 1: 128×128  ← 2×2 平均
level 2: 64×64
level 3: 32×32
...
level N: 1×1
```

代码：`texture.h` 的 `generateMipmaps()` 循环做 `w=w/2, h=h/2` 直到 1×1。

```cpp
// 2×2 box filter：每 4 个父级纹素平均成 1 个
for (int y = 0; y < newH; ++y)
    for (int x = 0; x < newW; ++x) {
        Color avg;
        avg.r = (c00.r + c10.r + c01.r + c11.r + 2) / 4; // +2 是四舍五入
        ...
    }
```

## 三、LOD 计算（选哪一层）

给定屏幕像素的 UV 坐标变化率（偏导数），估算它覆盖了多少纹素：

$$\rho = \max\left(\sqrt{(\partial u/\partial x)^2 + (\partial v/\partial x)^2},\; \sqrt{(\partial u/\partial y)^2 + (\partial v/\partial y)^2}\right) \times \text{texSize}$$

$$\lambda = \log_2(\rho)$$

$\lambda$ 就是**浮点 LOD 级别**。整数部分 = 选哪层，小数部分 = 在相邻层间插值（trilinear）。

代码：`Texture::sampleTrilinear()` 中 `std::log2(rho)` 得到 `lod`，`floor(lod)` 得 level0，`lod - level0` 得 frac。

## 四、三线性插值

1. 在 level N 做双线性采样 → 得 `c0`
2. 在 level N+1 做双线性采样 → 得 `c1`
3. `lerp(c0, c1, frac)` 得到最终颜色

这样 LOD 过渡是连续平滑的，不会出现"层间突变"。

## 五、代码入口

| 操作 | 代码 |
|---|---|
| 生成 mipmap | `Texture::generateMipmaps()`（加载时自动调用） |
| 设置过滤器 | `tex.setFilter(Texture::Filter::Trilinear)` |
| 三线性采样 | `tex.sampleTrilinear(u,v, dudx,dudy, dvdx,dvdy)` |
| 统一入口 | `tex.sample(...)` 按 `filter_` 自动选法 |

## 六、动手实验

1. 把 `filter_` 设成 `Bilinear`（不用 mipmap），渲染一个延伸到远方的棋盘地面——远处闪烁严重。
2. 设成 `Trilinear`，闪烁消失，但极远处略模糊——这是正确行为（低频信息丢失）。
3. 在 `generateMipmaps` 里打断点，看 256×256 纹理生成的 mip 链是怎样的。

---

上一篇：`pipeline/08-裁剪`
下一篇：`pipeline/16-完整视锥裁剪`
