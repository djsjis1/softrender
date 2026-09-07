# 13 · 后处理管线：Bloom 与色调映射

> PBR + 强光产生远超 1.0 的 HDR 值，8 位屏幕显示不了。后处理管线在"渲染完之后、显示之前"对整张图做全屏操作：色调映射把 HDR 压回 LDR，Bloom 让亮区产生光晕。本篇讲清这条"全屏 pass"链。

## 一、为什么需要浮点帧缓冲

8 位 `Framebuffer` 每通道 [0,255]，对应 [0,1]。但 PBR 高光、自发光、太阳直射可以轻易到 10、100。直接 clamp 到 1 会丢失所有亮度层次——一片死白。

解法：渲染到**浮点帧缓冲**（`sr::FloatFramebuffer`，每像素 `Vec3f`），暂存 HDR 值，最后一步再压回 [0,1]。

## 二、色调映射（Tone Mapping）

把 [0,∞) 映射到 [0,1] 的非线性曲线。最简的是 Reinhard `c/(c+1)`，但高光会发灰。本项目用 **ACES 近似**：

$$\text{ACES}(x) = \frac{x(2.51x+0.03)}{x(2.43x+0.59)+0.14}$$

它有漂亮的**肩部滚降**：亮区逐渐饱和而非硬截断，暗区保持对比度。代码 `sr::FloatFramebuffer::acesToneMap`。

色调映射后还需 **gamma 校正**（线性→sRGB，`pow(c, 1/2.2)`），否则画面偏暗。

## 三、Bloom（辉光）

模拟镜头/眼睛对亮区的散射：亮物体周围出现柔和光晕。三步：

1. **亮部提取**：只保留亮度 > 阈值（如 1.0）的像素，其余置黑。
2. **高斯模糊**：对亮部图做可分离两遍模糊（水平+垂直），得到扩散的光晕。
3. **加法合成**：原图 + 模糊图 × 强度。

代码 `sr::FloatFramebuffer::brightPass`、`gaussianBlur`、`addBlend`。

> 为什么可分离？2D 高斯核 = 1D 水平 × 1D 垂直，两遍 1D 卷积等价于一遍 2D，但开销从 O(r²) 降到 O(2r)。

## 四、对应代码

| 概念 | 代码 |
|---|---|
| HDR 浮点缓冲 | `src/core/float_framebuffer.h` |
| ACES 色调映射 | `sr::FloatFramebuffer::acesToneMap` |
| Gamma 校正 | `sr::FloatFramebuffer::gammaCorrect` |
| 亮部提取 | `brightPass` |
| 可分离高斯模糊 | `gaussianBlur` |
| 对比示例 | `examples/stage11_postprocess.cpp` |

## 五、动手实验

1. 把 Bloom 的模糊半径从 12 改到 30，光晕变大变柔。
2. 把 ACES 换成 Reinhard `c/(c+1)`，对比高光区的"白度"和色彩饱和度。
3. 去掉 gamma 校正，画面会明显偏暗——理解显示器需要 sRGB 编码。

## 六、小结

- HDR 浮点缓冲暂存超 1 的亮度。
- **色调映射**（ACES）把 HDR 压回 LDR，带肩部滚降。
- **Bloom** = 亮部提取 + 可分离高斯模糊 + 加法合成。
- 后处理是"全屏 pass"，在渲染完成后对整张图操作。

> **更多后处理效果**（DOF 景深、运动模糊、暗角、色差）见 `advanced/43-后处理效果.md`——本篇是后处理管线基础，43 篇在此基础上扩展了额外效果。

---

上一篇：[12 · PBR 光照：Cook-Torrance BRDF](12-PBR光照-Cook-Torrance-BRDF.md)
下一篇：[14 · 多线程并行光栅化](14-多线程并行光栅化.md)
