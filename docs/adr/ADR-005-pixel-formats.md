# ADR-005：存储像素格式与运算域分离

- 日期：2026-10-25
- 状态：已采纳

## 背景与选择

`Framebuffer` 原先硬编码 `Color*`（RGBA8888，4 字节/像素）。目标设备里存在需求明确的
其他布局：STM32 + ILI9341/ST7789 的原生格式是 RGB565（2 字节/像素），单色 OLED 是 8 位灰度。
直接修改 `Color` 的类型是错误方案，因为 `Color` 同时承担三个冲突角色：

| 角色 | 使用方 | 对类型的要求 |
| --- | --- | --- |
| 存储单元 | `Framebuffer`、`Image` | 越紧凑越好（565 是正解） |
| 运算域 | `blend.h` 五种混合、纹理采样、MSAA 累加 | 需要逐通道算数与 8 位 alpha（565 做不到） |
| IO 契约 | `io/tga.cpp`、`io/image_write.cpp`、黄金图 | 必须是 RGBA8 通道顺序 |

备选 A（运行时格式枚举 + 每像素 `switch`）：`row()` 返回类型无法统一，只能退化为 `void*`，
且把 `Renderer::rasterTriangle` 的行指针直写变成"每像素一次分支"。拒绝。

备选 B（模板参数化帧缓冲 + 像素类型 trait，即 AGG 的 `pixfmt_*` 做法）：格式进入类型，
每像素零分支、零虚调用、无运行时格式表。采用。

备选 C（只新增 `Color565` 转换函数，不改帧缓冲）：不解决"渲染直接落到 565 缓冲"，
装配到 STM32 仍需一整块 8888 帧缓冲。作为第一层保留，但不作为终点。

```
Color (RGBA8888) = 运算域 ── 顶点/片元着色、混合、采样、后处理
        │
        │  PixelT::fromFloat / fromColor   ← 唯一量化点
        ▼
Framebuffer<PixelT> = 存储域 ── Color / Color565 / Gray8
        │
        │  framebuffer_convert.h            ← 集中转换与放大
        ▼
LCD / 文件 / 另一格式的帧缓冲
```

## 决策

1. 新增 `core/pixel_format.h`，定义存储像素类型的**最小契约**：`kBytes`、`fromColor`、
   `fromFloat`、`toColor`、`operator==`。`Color` 自身补齐这四个成员（恒等实现），
   使其可直接作为 `PixelT` 使用，不引入第二个"颜色"概念。
2. `core/framebuffer.h` 改为 `BasicFramebuffer<PixelT>`，并提供
   `using Framebuffer = BasicFramebuffer<Color>`。既有 22 个示例、`io` 写出、
   后处理与 MSAA 全部走别名，**源码零改动**。
3. `Renderer::drawMesh` / `rasterTriangle`、`sr::fillTriangle*`、
   `rasterizer_fast` 的帧缓冲参数改为**模板成员/模板函数**（不是模板类）。
   调用方 `renderer.drawMesh(fb, mesh, shader)` 由参数推导，调用点零改动。
4. 新增 `core/framebuffer_convert.h` 提供 `convertFramebuffer()` 与
   `NearestUpscaler`（预计算映射表 + 逐行输出），对标 SDL 的 `SDL_ConvertPixels`
   与"渲染分辨率与显示分辨率解耦"的呈现方式。转换逻辑只此一份，不散落到调用点。
5. 2D 图元（`shape2d.h` / `blit2d.h` 的全部自由函数）同样模板化，可画进任意格式。
   成立的前提是它们只经 `plotBlend` 的 `setPixel` / `getPixel` 读写，混合在 `Color`
   （RGBA8888、带 8 位 alpha）域内完成，最后一步才量化 —— 与 3D 路径同一机制。
   真正受限的只有**目标侧 alpha**：565 / 灰度缓冲读回的 dst 恒为不透明，因此半透明
   图元叠在半透明背景上会退化为"按不透明背景混合"。这是存储格式的物理限制，
   已被 `Draw2DFormat.AlphaBlendMatchesOnOpaqueBackdrop` 显式锁定。
6. 仍固定 RGBA8888 的两处：`Renderer2D`（有状态包装，内部持有 `Framebuffer*` 成员，
   模板化会把整个类的成员类型拖进模板）与 MSAA（逐样本存 8 位颜色 + alpha，
   本就需要高精度中间缓冲）。两者都可以通过"先画进 8888 再 `convertFramebuffer`"使用。

## 明确不做

- **不引入 `stride`/`pitch` 与外部缓冲构造**：`Framebuffer` 保持 `new PixelT[]` 内部持有
  的简洁 API（此前的外部缓冲区方案已被否决）。
- **不做运行时格式切换**：两套实例化会同时进 flash，裸机只能留一套；需要哪套由
  `PixelT` 在编译期决定。
- **不做 tile/span 缓冲重写**：`rasterTriangle` 每像素立即写深度与颜色，
  改成行缓冲会改变红线函数的语义与性能特征，收益不抵风险。

## 量化与验证

- 5 位量化用 `(v * 31 + 127) / 255` 而非 `v >> 3`：实测 `v=5` 时截断得 0、四舍五入得 1，
  全范围最大偏差 4 级（截断最大 7 级）。
- 扩展用位复制 `(q << 3) | (q >> 2)`，保证 `fromColor(toColor(p)) == p` 对全部 65536 个
  565 值成立（穷举测试锁定），且 0/31 映射到 0/255 精确。
- 灰度用 Rec.601 整数权重 76/150/29（**和为 255**）配 `(sum + 127) / 255`。
  不能用 77/150/29 + `>> 8`：那组和为 256、右移等于除以 256，纯绿 (0,255,0)
  得 149，而 0.587 × 255 = 149.7 应为 150。这是实现期被单测捕获的真实偏差。
- 格式一致性测试必须让**三个顶点取值互不相等**（否则同色输入无法区分量化是否正确），
  并断言"同一场景在 8888 与 565 下展开后的每通道差值不超过量化步长一半"。
- 2D 侧最重要的断言不是颜色容差，而是**覆盖范围与格式无关**：同一组图元画进
  8888 与 565 后，"哪些像素被写过"必须逐像素一致（`Draw2DFormat.OpaquePrimitivesCoverIdenticalPixels`）。
  颜色容差按混合深度分级：不透明 4、AlphaBlend/Additive 8、Multiply 12
  （Multiply 含 dst×dst，会把 dst 的量化误差放大一倍）。
