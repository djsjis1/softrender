# SoftRenderer

基于 C++23 的教学型 CPU 软件渲染器：从画线、三角形光栅化，到可编程着色、PBR、IBL、骨骼动画和后处理。不依赖 OpenGL、DirectX 或 Vulkan。

2.0.0 起按职责分层：公共头统一在 `include/softrenderer/`，按组件导出
`SoftRenderer::core` / `::render` / `::scene` / `::io` / `::runtime` / 可选 `::entt`。
找缓冲只进 `core`，找绘制只进 `render`，找资源加载只进 `io`；核心渲染无需接入文件解析、线程或窗口。

```cpp
#include <softrenderer/core/framebuffer.h>
#include <softrenderer/render/pipeline/renderer.h>

Framebuffer fb(800, 600);
Renderer renderer(800, 600);   // 只用 render + core 即可绘制
```

要"一条调用画一帧"，用统一前向管线门面（阴影贴图 + 队列排序 + FXAA，
后处理可注入线程池分条并行，见 `examples/demo_forward.cpp`）：

```cpp
#include <softrenderer/render/pipeline/forward_pipeline.h>

ForwardPipeline pipeline(800, 600);
pipeline.view = ...;  pipeline.proj = ...;
pipeline.shadowEnabled = true;  pipeline.fxaaEnabled = true;
pipeline.submit({&mesh, &shader, RenderLayer::Opaque, 0, center, lightVP * model});
pipeline.render(fb);
```

```cmake
find_package(SoftRenderer 2.0 REQUIRED COMPONENTS core render)
target_link_libraries(app PRIVATE SoftRenderer::core SoftRenderer::render)
```

核心算法以头文件为主；图像、模型加载器编入 `SoftRenderer::io` 静态库，第三方头不外露。
实时窗口示例使用 Windows GDI，离屏渲染与数学模块不依赖窗口系统。

## 能力与边界

| 模块 | 已实现能力 |
| --- | --- |
| 3D 管线 | 六面视锥裁剪、背面剔除、深度测试、透视校正插值、可编程 Shader、discard |
| 光照 | Flat / Gouraud / Phong / Cook-Torrance PBR、阴影映射、法线贴图、IBL |
| 图像处理 | mipmap、双线性/三线性采样、4x MSAA、FXAA、Bloom、SSAO、色调映射及其他后处理 |
| 2D | 图元、渐变、混合、图像变换、点阵文字、相机、scissor 与 HUD |
| 场景与资源 | OBJ、glTF/glb、材质、骨骼/动画、粒子、EnTT 组件与资源缓存 |
| 工程设施 | 分条并行示例、命令缓冲/线程池、可复用 Arena、Profiler、单元/像素/安装集成测试 |

这些能力分布在不同示例和模块中，并非都已整合进同一个 Renderer。SSE2 光栅化仍是独立实验路径；动画已支持 Linear/Step/CubicSpline，节点与骨骼层级、skin 导入已支持，sparse accessor 与压缩扩展仍待完善。覆盖规则已统一到整数子像素；MSAA 示例按样本提交颜色。可选性能目标及实测结果见[性能基线](docs/engineering/58-覆盖规则与性能基线.md)。完整限制与优先级见[项目上下文](docs/project_context.md)与[架构文档](docs/architecture.md)。

## 构建和测试

需要 CMake 3.20+ 与支持项目所用 C++23 特性的工具链。Windows 本轮实测 MSVC 19.44；Linux/macOS 的执行结果需以远端 CI 为准。第三方源码已内置，无需在线下载依赖。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel

# 快速迭代：所有单元测试一次执行，加三场景像素回归
ctest --test-dir build -C Release -L "fast|regression" --output-on-failure

# 完整验证：逐用例报告，包含真实下游源码/安装集成测试
ctest --test-dir build -C Release --parallel 4 -LE fast --output-on-failure
```

Visual Studio 使用多配置生成器，测试和安装也要指定 `-C Release` / `--config Release`。Ninja/Makefile 的配置在首次配置时由 `CMAKE_BUILD_TYPE` 决定。工程移动目录后请创建新的构建目录，不能沿用指向旧路径的 CMakeCache。

详细步骤、Sanitizer、配置开关与安装说明见[构建与使用指南](docs/engineering/46-构建与使用指南.md)。

## 运行示例与练习

从仓库根目录运行；离屏示例输出到 `output/`：

```powershell
.\build\bin\Release\stage06_depth.exe
.\build\bin\Release\stage12_pbr.exe
.\build\bin\Release\demo_2d.exe
.\build\bin\Release\stage08_shader_live.exe  # Windows 实时窗口
```

Ninja/Makefile 的可执行文件位于 `build/bin/`。将练习源码放入 `practice/my_first.cpp` 后，重新构建即可生成 `practice_my_first` 目标，无需手动修改 CMake。

## 作为库集成

源码集成：

```cmake
add_subdirectory(path/to/softrender softrender-build)
target_link_libraries(your_target PRIVATE SoftRenderer::softrenderer)
```

作为子项目时，示例、练习、测试默认关闭；公共目标自动提供 C++23、头文件路径、资源实现库及线程依赖。

安装后集成：

```sh
cmake --install build --config Release --prefix ./build/install
```

```cmake
find_package(SoftRenderer 2.0 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE SoftRenderer::softrenderer)
```

配置下游工程时通过 `-DCMAKE_PREFIX_PATH=<安装前缀>` 指定路径。安装树可整体移动；源码和安装模式统一使用 `#include <softrenderer/version.h>`。

## 裸机与嵌入式

core + math + render + resource + scene 全部 header-only，公共头零 `throw`、零 RTTI、零线程/文件依赖，可在 `-fno-exceptions -fno-rtti` 下编译（由 `sr_freestanding_headers_check` 门禁强制）。缓冲可放静态内存/指定 RAM 段，渲染链路不经堆：

```cpp
static Color g_pixels[320 * 240];
static float g_depth[320 * 240];
sr::BasicFramebuffer<sr::Color> fb(320, 240, g_pixels);  // 外部存储，析构不释放
sr::Renderer renderer(320, 240, g_depth);
renderer.setExternalVertexCache(g_vcache, g_vstamps, kMaxVerts);  // 顶点缓存同样零堆
```

裸机构建：`-DSR_NO_EXCEPTIONS=ON`（io/runtime/tests 自动关闭）；RGB565/灰度面板直渲染见 [ADR-005](docs/adr/ADR-005-pixel-formats.md)。完整步骤与契约见[裸机集成指南](docs/engineering/60-裸机与无操作系统集成指南.md)。

## 目录与文档

| 路径 | 职责 |
| --- | --- |
| `include/softrenderer/` | 数学、缓冲、渲染管线、资源、场景、运行时、IO 声明（公共头唯一入口） |
| `src/io/` | 文件加载实现（`softrenderer_io` 静态库） |
| `platform/` | Windows 显示与输入后端 |
| `examples/`, `practice/` | 示例与练习，各自维护 CMake 目标 |
| `tests/unit/` | GoogleTest 单元测试；新增 Arena/Renderer 测试独立成文件 |
| `tests/integration/` | 像素回归、真实下游工程和安装迁移测试 |
| `tests/fixtures/golden/` | 纳入版本控制的图像基准 |
| `third/`, `cmake/` | 第三方源码、版本与安装导出 |

- 学习：[文档索引](docs/README.md)、[学习路线](docs/00-学习路线与项目导读.md)、[模块职责](docs/modules.md)、[架构](docs/architecture.md)
- 使用：[构建指南](docs/engineering/46-构建与使用指南.md)、[问题排查](docs/troubleshooting.md)
- 维护：[当前状态](docs/project_context.md)、[任务](docs/tasks/README.md)、[变更日志](docs/changelog.md)

动态的测试数量和验收状态统一维护在项目状态页；历史 TASK 保留当时的事实，不作为当前状态依据。
