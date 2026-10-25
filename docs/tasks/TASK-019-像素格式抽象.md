# TASK-019：存储像素格式抽象（ADR-005）

- 状态：已完成（2026-10-25）
- 变更级别：L（跨模块 + 公共接口 + 参考外部方案）
- 关联决策：[ADR-005](../adr/ADR-005-pixel-formats.md)

## 需求

在不破坏现有 22 个示例、像素回归基准与全部单元测试的前提下，为渲染目标增加
RGBA8888 以外的存储格式（RGB565、8 位灰度），并保持模块分层。

## 关键判断

`Color` 同时是存储单元、运算域与 IO 契约，直接改类型会丢 alpha 语义、让按
`Color` 内存布局工作的写出路径静默出错、并作废全部逐像素基准图。因此保留
`Color` 作为唯一运算域，把"存储格式"抽成独立的 `PixelT` 类型参数。

## 改动清单

### 新增

| 文件 | 内容 |
| --- | --- |
| `include/softrenderer/core/pixel_format.h` | `Color565`、`Gray8`、`PixelT` 契约说明 |
| `include/softrenderer/core/framebuffer_convert.h` | `convertFramebuffer`、`NearestUpscaler` |
| `tests/unit/pixel_format_test.cpp` | 40 个用例，6 个套件（PixelFormat / FramebufferFormat / FramebufferConvert / NearestUpscaler / FormatRasterization / Draw2DFormat） |
| `docs/adr/ADR-005-pixel-formats.md` | 决策记录 |

### 修改

| 文件 | 改动 |
| --- | --- |
| `core/color.h` | 补 `kBytes` / `fromColor` / `toColor` / `operator==`，成为 `PixelT` 的恒等特例 |
| `core/framebuffer.h` | `Framebuffer` → `BasicFramebuffer<PixelT>` + `using Framebuffer` 别名；加 `bytesPerPixel` / `byteSize`；`clear` 量化提到循环外 |
| `render/pipeline/renderer.h` | `drawMesh` / `rasterTriangle` / `clear` 改为模板成员函数 |
| `render/raster/triangle.h` | `fillTriangle` / `fillTriangleGradient` 改为模板函数 |
| `render/raster/rasterizer_fast.h` | 三个入口改为模板函数 |
| `render/draw2d/shape2d.h` | 23 个自由函数改为模板函数（含唯一写入出口 `plotBlend`） |
| `render/draw2d/blit2d.h` | 11 个自由函数改为模板函数（含 `blitFramebuffer` 的双格式参数） |
| `tests/CMakeLists.txt` | 注册新测试文件 |
| `benchmarks/stress/stress_common.h` | 引入新头 |
| `benchmarks/stress/stress_ext.cpp` | 新增 3 个场景 |

### 明确不动

`Renderer2D`（有状态包装，内部持有 `Framebuffer*` 成员，模板化会把成员类型拖进模板）、
`msaa_framebuffer.h` / `msaa_renderer.h`（逐样本存 8 位颜色 + alpha，本就需要高精度缓冲）、
`io/*`（TGA / PNG 写出是 RGBA8 契约）。这三类都可以"先画进 8888 再 `convertFramebuffer`"使用。

## 实现期发现并修复的缺陷

灰度初版用 77/150/29（和为 256）+ `>> 8`，右移等于除以 256，纯绿 (0,255,0)
得 149 而 Rec.601 应为 150。改为和为 255 的 76/150/29 配 `(sum + 127) / 255`。
由 `PixelFormat.Gray8WeightsFavourGreen` 与 `FramebufferConvert.ConvertToGray8UsesLuminance`
两个用例捕获。

## 验证（Release）

```
cmake --build build --config Release --parallel     构建零错误零警告
.\build\bin\Release\unit_tests.exe                  324/324（281 → 324，70 套件）
.\build\bin\Release\render_regression.exe           3/3
.\build\bin\Release\renderer_stress.exe             110/110（105 → 110）
.\build\bin\Release\renderer_stress.exe --quick      90/110，1.8 s
ctest -R architecture_boundaries                    分层 + 无环 + 逐头自包含 通过
ctest -R "consumer_|render_regression"              下游消费 2 项 + 回归 通过
19 个离屏示例                                        退出码全 0
```

## 验证（Debug + ASAN）

```
cmake -S . -B build/asan -DSR_ENABLE_SANITIZERS=ON
cmake --build build/asan --config Debug --target unit_tests renderer_stress
.\build\asan\bin\Debug\unit_tests.exe       321 通过 + 3 跳过（越界/契约哨兵）
.\build\asan\bin\Debug\renderer_stress.exe  110/110，156 s
```

无断言中止、无 ASAN 报告。3 个跳过项是 `DepthBuffer::get` / `Framebuffer::getPixel` /
`NearestUpscaler::presentRow` 的越界与契约哨兵——Debug 下 `assert` 会先中止进程，
沿用 `core_buffer_test.cpp` 已有的 `#ifndef NDEBUG GTEST_SKIP() #else ... #endif` 写法。

## 静态分析

`clang-tidy 19.1.5 --checks="-*,clang-analyzer-*,bugprone-*"` 对
`pixel_format.h`、`framebuffer_convert.h`、`framebuffer.h`、`renderer.h` **零诊断**。
`shape2d.h` / `blit2d.h` 命中的只有既有的 `bugprone-narrowing-conversions`
与 `bugprone-easily-swappable-parameters`（绘图 API 的固有形态，与本次改动无关）。

## 实测数据

| 场景 | 内存 | 与 8888 的差异 |
| --- | --- | --- |
| 240×180 3D 渲染（PBR，8 次 draw） | 172800 → 86400 字节 | 逐通道最大差 4 |
| 320×240 2D UI 单帧 | 307200 → 153600 字节 | 最大差 4，覆盖零错配，0.72 ms/帧 |
| 320×240 呈现放大（160×120 → 320×240） | 小画布 75 KB + 行缓冲 | 2× 精确复制、1.6× 无黑边、降采样/1×1/非法尺寸均正确 |
| 565 同格式呈现（160×120 → 320×240） | — | **0.052 ms vs 跨格式 0.44~0.57 ms（8~11 倍）**，逐位一致 |

## 性能收敛

### 新版本 vs 原版本（无回归）

用 `git worktree` 把 HEAD（改动前）单独检出构建，与本工作区交替采样 4~7 次取中位：

| 场景 | 原版本 | 现在 | 比值 |
| --- | --- | --- | --- |
| 49 个 ≥1ms 场景总和 | 2851 ms | 2814 ms | 0.987 |
| deferred_1080p | 164.9 ms | 164.7 ms | 1.00 |
| ssao_1080p | 208.8 ms | 208.3 ms | 1.00 |
| render_1024x1024_x30 | 65.7 ms | 67.0 ms | 1.02 |

结论：模板化后的 RGBA8888 路径与改动前等价，差异在噪声范围内。

### 两处优化

1. `Color565::fromFloat` 原来复用 `Color::fromFloat`，会连 alpha 通道一起钳制换算，而 565 没有 alpha 位 → 改为只算三个通道（`PixelQuantize::toByte`）。
2. `convertFramebuffer` / `NearestUpscaler::presentRow` 增加同格式分支（`if constexpr (is_same_v<SrcPixel, DstPixel>)`）：同格式直接整行拷贝或直接列映射，不再"展开成 8888 再量化回来"。这是 MCU 上"渲染到 565 → 放大到 565 屏幕"的主路径，实测快 8~11 倍。

> 量测方法上的坑：首轮对比得出"新版本快 2.4 倍"是假象——新建的 exe 首次执行撞上 Defender 扫描与冷文件缓存，且脚本复用了同一 JSON 文件名导致读到上一次的旧结果。修正为"预热门 + 每次唯一文件名 + 交替采样"后才得到 0.98 的结论。

## 后续

- 若要支持运行时切换格式，需重新评估：两套实例化会同时进 flash，裸机只能留一套。
- `stride`/`pitch` 与外部缓冲构造仍未提供（此前的方案已被否决），LTDC 扫描缓冲
  需由调用方自行转换。
- `Renderer2D` 若要支持其他格式，需要把 `Framebuffer* fb_` 成员参数化（会波及
  整个类），或改用"8888 渲染 + `convertFramebuffer`"的方式绕过。
