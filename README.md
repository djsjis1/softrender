# SoftRenderer

> 基于 C++23 的教学型软件渲染器：不依赖任何图形 API，从 Bresenham 画线一路实现到 Cook-Torrance PBR、IBL、骨骼动画与后处理管线，全部跑在 CPU 上。

## 功能特性

**渲染管线**

| 类别 | 能力 |
|---|---|
| 光栅化 | Bresenham 画线、边函数 + 重心坐标、增量式边函数 |
| 裁剪 | 完整 6 面 Sutherland-Hodgman 视锥裁剪（含顺时针绕序修复） |
| 深度 | Z-buffer、背面剔除（可关闭以渲染双面片） |
| 纹理 | 最近邻 / 双线性 / 三线性过滤、mipmap 链、Repeat 环绕 |
| 插值 | 透视校正（1/w） |
| 着色 | Flat / Gouraud / Phong / Cook-Torrance PBR |
| 阴影 | Shadow Mapping + PCF 3×3 |
| 法线贴图 | TBN 切线空间 |
| 抗锯齿 | 4x MSAA、FXAA |
| 后处理 | Bloom、ACES 色调映射、景深、运动模糊、暗角、色差 |
| 全局光照 | SSAO |
| 环境光 | Cubemap + IBL 辐照度 |

**2D 渲染子系统**（`namespace sr2d`）

矩形 / 圆 / 椭圆 / 圆角矩形 / 多边形 / 渐变 / 粗线、alpha 混合五种模式、
图像 blit 与变换、5×7 点阵字体文字渲染、`Mat3` 变换栈、`Camera2D`、scissor 裁剪。
可与 3D 场景共用同一 `Framebuffer`，直接做 HUD 叠加。

**资源与场景**

- 模型加载：OBJ（tinyobjloader）、glTF 2.0 / glb（tinygltf）
- glTF PBR metallic-roughness 材质全字段解析
- 骨骼蒙皮动画 + 四元数 SLERP / NLERP 插值
- 粒子系统（Billboard 发射器 + 重力 + 颜色渐变）
- EnTT ECS 场景管理、引用计数资源管理器

**工程设施**

- 多线程分条光栅化、命令缓冲、线程池
- Arena 线性分配器
- Chrome Tracing Profiler（输出可直接拖进 Perfetto UI）
- 像素级回归测试工具
- GoogleTest 单元测试：**94 个用例 / 29 个套件**
- GitHub Actions 三平台 CI

## 依赖环境

| 项 | 要求 |
|---|---|
| CMake | **3.20+**（`CMAKE_CXX_STANDARD 23` 需要） |
| 编译器 | 需完整支持 **C++23**：MSVC 2022 17.10+ / GCC 13+ / Clang 17+ |
| 平台 | 渲染核心 100% 平台无关；实时窗口示例仅 Windows（Win32 GDI） |

**无需安装任何第三方库**。全部依赖以源码形式内嵌在 `third/`，清单与版本见
[`third/README.md`](third/README.md)。

可选：`SR_ENABLE_SPDLOG`（默认 ON）控制 spdlog 日志集成。

## 构建

```bash
# 配置
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 编译（MSVC 需指定 --config）
cmake --build build --config Release --parallel

# 运行全部单元测试
ctest --test-dir build --output-on-failure -j
```

产物输出到 `build/bin/<Config>/`（MSVC 多配置生成器）或 `build/bin/`（Makefile / Ninja）。

> **开发迭代时更快的测试方式**：`ctest` 会把 94 个用例注册为 94 个独立进程，
> 进程启动开销远大于测试本身。直接跑测试二进制只需约 30 毫秒：
> ```powershell
> .\build\bin\Release\unit_tests.exe
> ```

## 快速启动

**跑一个示例看效果**（所有示例把结果写到 `output/`，需从**仓库根目录**运行）：

```powershell
cd <仓库根目录>
.\build\bin\Release\stage4_zbuffer.exe      # 深度缓冲：左右对比遮挡关系
.\build\bin\Release\stage10_pbr.exe         # PBR 金属度/粗糙度球阵
.\build\bin\Release\demo_2d.exe             # 2D 场景 + 2D HUD 叠加 3D
.\build\bin\Release\stage8_realtime.exe     # 实时窗口（仅 Windows，WASD 移动）
```

**从零写一个渲染程序**：把 `.cpp` 放进 `practice/` 目录，重新构建即自动生成
可执行程序，目标名为 `practice_<文件名>`，无需修改 `CMakeLists.txt`：

```powershell
# 1. 新建 practice/my_first.cpp（可参考 practice/hello_2d.cpp）
# 2. 重新构建
cmake --build build --config Release --target practice_my_first
# 3. 运行
.\build\bin\Release\practice_my_first.exe
```

**作为库集成到自己的项目**：

```cmake
add_subdirectory(path/to/softrender)
target_link_libraries(your_target PRIVATE SoftRenderer::softrenderer)
```

或安装后 `find_package(SoftRenderer)`，见 `cmake/install.cmake`。

## 目录结构

```
softrender/
├── src/                    渲染器本体：100% 平台无关，纯头文件（52 个 .h + 1 个 .cpp）
│   ├── core/               帧缓冲、深度缓冲、颜色、混合、后处理、Profiler、内存池
│   ├── math/               Vec2/3/4、Mat3、Mat4、Quat、Rect2、变换与投影
│   ├── raster/             光栅化：triangle / line / clip / renderer(3D) / renderer2d(2D)
│   ├── scene/              网格、纹理、相机、骨骼、动画、粒子、glTF/OBJ 加载、ECS 组件
│   ├── shader/             可编程着色管线：Flat / Gouraud / Phong / PBR
│   └── test/               像素回归测试工具
├── examples/               22 个示例：stage1~12 分阶段递进 + demo_* 综合演示
├── practice/               练习目录：每个 .cpp 自动成为一个可执行程序
├── platform/               平台后端（win32_window.h）——仅示例层使用，src/ 永不引用
├── tests/                  GoogleTest 单元测试（test_core.cpp）
├── third/                  第三方库源码（见 third/README.md）
├── docs/                   教程与工程文档（70+ 篇）
│   ├── math/               数学基础：向量 → 矩阵 → 齐次坐标 → 四元数
│   ├── pipeline/           渲染管线：光栅化 → 投影 → 深度 → 纹理 → 光照 → 裁剪
│   ├── advanced/           进阶：抗锯齿、PBR、后处理、多线程、延迟渲染、IBL、骨骼动画
│   ├── engineering/        工程化：测试、CMake、日志、CI、性能、代码规范
│   └── 00-学习路线与项目导读.md   ← 建议从这里开始
├── cmake/                  版本生成、安装导出规则
└── .qoder/rules/           本项目遵循的 AI 开发工作流规范
```

## 架构原则

1. **渲染器本体零平台依赖**：`src/` 只用 C++ 标准库；`platform/` 的 Win32 代码
   只对需要开窗的示例可见，从编译层面保证隔离
2. **header-only 为主**：减少编译单元，方便集成
3. **开源优先**：外围功能（图像 I/O、模型解析、ECS、日志、测试）用成熟开源库
4. **条件编译隔离**：spdlog / Win32 / SSE2 通过宏开关，不影响核心
5. **每个阶段可独立编译运行**：stage1~12 + demo 各自是独立可执行程序

详见 [`docs/architecture.md`](docs/architecture.md) 与 [`docs/modules.md`](docs/modules.md)。

## 学习路径

`docs/` 下的文档与 `examples/` 的 stage 编号一一对应，建议按
[`docs/00-学习路线与项目导读.md`](docs/00-学习路线与项目导读.md) 的顺序推进：

```
数学基础(docs/math) → 光栅化(stage1-3) → 深度(stage4) → 纹理(stage5)
→ 着色(stage6) → 裁剪(stage7) → 阴影/法线贴图(stage8) → 抗锯齿(stage9)
→ PBR(stage10) → 后处理(stage11) → 多线程(stage12) → 综合 demo
```

## 当前状态

见 [`docs/changelog.md`](docs/changelog.md) 与 `docs/tasks/`。

已知待办与架构欠债集中记录在 [`docs/tasks/README.md`](docs/tasks/README.md)。
