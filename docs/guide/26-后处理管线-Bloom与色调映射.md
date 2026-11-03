# 26 · 后处理管线：Bloom、色调映射与全屏效果

> PBR + 强光产生远超 1.0 的 HDR 值，8 位屏幕显示不了。后处理管线在"渲染完之后、显示之前"对整张图做全屏操作：色调映射把 HDR 压回 LDR，Bloom 让亮区产生光晕，DOF/暗角/色差做镜头模拟。

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

## 四、更多全屏效果

| 效果 | 函数 | 原理 |
|---|---|---|
| DOF 景深 | `postprocess::depthOfField` | 根据深度计算 CoC，按半径分桶模糊 |
| 径向运动模糊 | `postprocess::radialMotionBlur` | 沿屏幕径向方向采样 |
| 暗角 | `postprocess::vignette` | 边缘亮度衰减 |
| 色差 | `postprocess::chromaticAberration` | R/B 通道径向偏移 |

DOF 的核心是 **CoC（Circle of Confusion）= \|depth − focusDist\| / focusRange**：CoC 越大模糊半径越大，焦点平面 CoC=0 完全清晰。注意每像素半径由自身 CoC 决定，因此不能拆成"先横后纵"两趟（两趟与二维窗口不等价）。

```cpp
#include <softrenderer/render/postprocess/post_processing.h>

postprocess::depthOfField(colorBuf, depthBuf, W, H,
    /*focusDist=*/ 5.0f, /*focusRange=*/ 3.0f, /*maxBlur=*/ 8, outBuf);
postprocess::vignette(outBuf, 0.4f);
postprocess::chromaticAberration(outBuf, W, H, 3.0f, finalBuf);
```

## 五、对应代码

| 概念 | 代码 |
|---|---|
| HDR 浮点缓冲 | `include/softrenderer/core/float_framebuffer.h` |
| ACES 色调映射 | `sr::FloatFramebuffer::acesToneMap` |
| Gamma 校正 | `sr::FloatFramebuffer::gammaCorrect` |
| 亮部提取 | `brightPass` |
| 可分离高斯模糊 | `gaussianBlur` |
| DOF / 暗角 / 色差等 | `include/softrenderer/render/postprocess/post_processing.h` |
| 对比示例 | `examples/stage13_postprocess.cpp` |

## 六、动手实验

1. 把 Bloom 的模糊半径从 12 改到 30，光晕变大变柔。
2. 把 ACES 换成 Reinhard `c/(c+1)`，对比高光区的"白度"和色彩饱和度。
3. 去掉 gamma 校正，画面会明显偏暗——理解显示器需要 sRGB 编码。
4. 把 DOF 的 `maxBlur` 调到 30，观察边缘采样成本的增长。

## 七、小结

- HDR 浮点缓冲暂存超 1 的亮度。
- **色调映射**（ACES）把 HDR 压回 LDR，带肩部滚降。
- **Bloom** = 亮部提取 + 可分离高斯模糊 + 加法合成。
- 后处理是“全屏 pass”，在渲染完成后对整张图操作；实时预览中应优先降分辨率而不是逐算子微优化。

## 八、综合示例：群星闪耀

`examples/demo_stars.cpp` 用上述管线纯 CPU 生成 1920×1080 星空图，是后处理的完整用例：

| 层 | 技术 | 说明 |
|---|---|---|
| 星云背景 | FBM 分形噪声 | 紫蓝+橙红双色，5 层 octave |
| 暗星 ×800 | 高斯衰减点 | 不同色温（蓝/白/橙），半径 0.3~0.9px |
| 亮星 ×80 | 高斯 + 外层光晕 | HDR 强度 1~4，半径 1~2.5px |
| 超亮星 ×12 | 多层高斯 + 十字衍射 | HDR 强度 5~15，衍射光芒 30~90px |
| 色调映射 | ACES 近似 | HDR→LDR，亮星不过曝 |
| Gamma | sRGB 2.2 | 线性→显示 |

关键技术点：所有星星用加法混合累积到 `sr::FloatFramebuffer`（亮度可超 1.0）；高斯衰减 `exp(-dist²/(2σ²))` 模拟点扩散函数；十字衍射为 4 条线（0°/90°/45°/-45°），立方衰减且宽度随距离缩小；色温映射把随机温度转成蓝白/白/橙红 RGB。

```bash
./build/bin/Release/demo_stars        # 输出 output/stars.tga (1920×1080)
```

207 万像素、星云背景逐像素 FBM（5 octave）约 2 秒，星星绘制约 0.5 秒，色调映射约 0.3 秒，单线程 Release 总计约 3 秒。
