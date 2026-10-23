# 变更日志

> 格式：日期 | 版本 | 变更内容 | 影响范围
>
> 按 `documentation_rules.md` 要求，每次有意义的变更都必须在此登记。
> 重大接口变更需同时在变更内容中标注版本号。

## 2026-10-23

### 2.0.0 分层解耦重构（TASK-014 阶段 2–5）

版本从 1.0.0 升到 **2.0.0**：头文件路径与命名空间按分层重排，**不保留旧路径转发或全局别名**，
属破坏性变更。保留 C++23、全部渲染功能、示例目标名、`practice` 练习流程与数学/绘制头文件式用法。

**目录与命名**

- 公共头唯一入口 `include/softrenderer/`：`math/ core/ resource/ render/{raster,draw2d,pipeline,shader,postprocess}/ scene/ io/ runtime/`。
- 私有实现集中到 `src/io/`（加载器 + 第三方实现单元）；`src/` 下不再有公共头。
- 功能命名空间收敛：`sr::draw2d`、`sr::postprocess`、`sr::colors`、`sr::transform`、`sr::meshgen`、`sr::texgen`、`sr::animation`、`sr::io`、`sr::keyframe`。

**核心数据与绘制解耦**

| 变更 | 说明 |
|---|---|
| `core/image.h` | 从 TGA 头提取与文件格式无关的 `sr::Image`；Texture / blit / 生成器只依赖它，Texture 不再有 `loadFromFile` |
| `render/postprocess/post_processing.h` | ACES / gamma / LDR resolve / brightPass / gaussianBlur / addBlend 从 `FloatFramebuffer` 迁出为自由函数；缓冲只保留存储与 `row()` 访问 |
| `render/pipeline/varyings.h`、`shader.h`、`surface_shader.h` | 插值契约、管线回调、内置 uniforms 三分；内置着色器继承 `SurfaceShader`，clip 只依赖 Varyings |
| `resource/vertex.h` | `Vertex` 从 mesh.h 独立；`render/shader/deferred_lighting.h` 承接 `lightingPass` |
| `math/barycentric.h` | 重心坐标成为纯数学，基础光栅化不再包含整个 2D 模块；`wireTriangle` 归 `sr::draw2d` |
| `scene/camera2d.h` + `Renderer2D::setViewTransform(Mat3)` | render 不再反向依赖 scene |
| Renderer 去掉 `SR_PROFILE_SCOPE` | 绘制头不再引入时钟/线程/锁；计时由调用方用 `sr::ProfileScope` 包围 |
| `resource/mesh_generator.h`、`texture_generator.h` | 程序化生成独立成头 |

**场景、加载与宿主能力**

- 自有场景数据：`sr::SceneAsset`（网格/材质/纹理/节点/蒙皮/动画片段）、`Material`、`SceneNode`、`Skin`、`AnimationClip`；材质用纹理**索引**引用资源，整体可安全移动/复制。
- 动画在加载阶段解码（时间轴、关键帧值、三次插值切线，含长度/类型/索引/有限值/单调性/零四元数校验），运行时 `sr::animation::sampleChannel` / `sampleAnimation` 只做纯数学，**不访问 tinygltf**；Linear/Step/CubicSpline、四元数归一化与 bind pose 语义不变。
- I/O 私有化：`sr::io::loadGltfScene` / `loadGltfMesh` / `loadObjMesh` / `loadImage` / `readTga` / `writeTga` / `writeImage`；实现移入 `src/io/*.cpp`（`gltf_loader.cpp`、`obj_loader.cpp`、`image_loader.cpp`、`image_write.cpp`、`tga.cpp`），第三方头（stb / tinyobj / tinygltf / nlohmann）只在实现单元可见。`gltf_accessor.h`、`gltf_mesh.h`、`convertModel` 为私有实现，内部单测显式获得 `src/io/` 私有 include。
- `runtime/resource_cache.h`：`ResourceCache::load<T>(key, factory)`，工厂返回 `shared_ptr<T>`，失败不入缓存，不再要求 `T::loadFromFile`。
- `runtime/thread_pool.h`：ThreadPool 与 CommandBuffer 分文件；`scene/keyframe_animation.h` 承接原 skeleton 的简化欧拉角动画。

**CMake 与对外集成**

- 组件目标：`SoftRenderer::core`（math/core/resource）、`::render`、`::scene`、`::io`（STATIC）、`::runtime`、可选 `::entt`；`SoftRenderer::softrenderer` 为已启用组件的聚合目标。
- 新选项 `SR_BUILD_IO`、`SR_BUILD_RUNTIME`、`SR_ENABLE_ENTT`（默认 ON）；三者关闭时只配置核心库，**不查找 Threads、不编译加载器**。
- 组件定义集中到 `cmake/targets.cmake`；`find_package(SoftRenderer COMPONENTS core)` 只导入 core 闭包，不查找 Threads。
- 安装只发布 `include/softrenderer/` 公共头 + 组件目标 + 可选 EnTT 头；**不再安装 stb / tinyobj / tinygltf / nlohmann 头**，也不安装 tests/benchmarks/私有实现头。

**验收实测（Windows Release）**

- 全量构建 0 错误 0 警告；可执行目标 25 个（与基线一致）。
- `unit_tests` **179/179** 通过（新增 ResourceCache 失败不缓存、动画已解码片段采样等用例）。
- `render_regression` 3/3 通过，基准图未更新。
- 20 个离屏程序（14 stage + 6 demo）退出码全为 0；窗口示例只构建。
- 架构门禁：每个公共头单独编译（`public_headers_check`）、命名空间冲突哨兵（`namespace_sentinel`）、模块 include 白名单（`architecture_boundaries`）。
- 下游消费：`consumer_subdirectory` / `consumer_installed` 均通过（安装到临时前缀后整体移动再 `find_package`），并新增只请求 core 的 `consumer_core` 场景。
- 组件隔离：`-DSR_BUILD_IO=OFF -DSR_BUILD_RUNTIME=OFF -DSR_ENABLE_ENTT=OFF` 配置与构建通过。

**本轮完成的 2.0.0 收尾（2026-10-23 晚）**

- **全局类名迁移完成（TASK-007）**：27 个仍含全局声明的公共头（`framebuffer.h`、`depthbuffer.h`、
  `blend.h`、`math/{vec,mat3,mat4,quat,rect}.h`、`renderer.h`、`renderer2d.h`、`clip.h`、`render_queue.h`、
  `shader.h`、`surface_shader.h`、`varyings.h`、`triangle.h`、`shaders.h`、`advanced_shaders.h`、
  `pbr_shader.h`、`cubemap.h`、`mesh.h`、`texture.h`、`vertex.h`、`camera.h`、`camera2d.h`、
  `particle_system.h`、`skeleton.h`）全部包进 `namespace sr`；`color.h` 的 `Color`、`log.h` 的
  `sr_log_init/sr_log_shutdown` 一并收敛（改为 `sr::logInit` / `sr::logShutdown`）。
  `namespace raster_fast` → `sr::raster`，与「功能函数进 `sr::<模块>`」的约定一致。
  **未保留任何别名桥**；应用侧统一 `using namespace sr;`。
- **命名空间门禁升级**：`tests/architecture/namespace_sentinel.cpp` 现在先在全局作用域声明
  60 个同名类型（`Framebuffer`/`Mesh`/`Texture`/`Camera`/`Renderer`/`Shader`/`Varyings`/
  `Material`/`SceneNode`/`Position`/`GBuffer`/`Mat4`/`Log` …），再包含**全部**公共头，
  并用 `static_assert` 确认这些类型位于 `sr`。任何回退都会在编译期失败。
- **`test_core.cpp` 按模块拆分完成（TASK-006）**：1468 行的单文件拆成 10 个模块级文件
  （`math_test` / `core_buffer_test` / `resource_test` / `render_pipeline_test` / `draw2d_test` /
  `scene_test` / `ecs_test` / `runtime_test` / `io_test` / `pixel_regression_support_test`），
  与 `tests/unit/` 已有的 `animation/arena/command_buffer/coverage/gltf/renderer` 单测并列。
  ECS 的编译期哨兵类型移入 `namespace ecsCanary`，使测试文件可以用 using-directive。
- **外部消费工程补全**：新增 `tests/integration/consumer_render`（只请求 `SoftRenderer::render`，
  验证核心渲染无需 io/runtime/线程/窗口）；`run_consumer.cmake` 现同时验证聚合、core-only、
  render-only 三类工程在 `add_subdirectory` 与「安装后整体移动前缀再 `find_package`」两种模式下可用，
  并断言安装产物只含公共头与组件（泄露 `tests/`、`benchmarks/`、`stb_/tiny_/nlohmann` 头即失败）。
- **第三方实现单元归位**：三个 `*_IMPLEMENTATION` 宏集中到 `src/io/stb_impl.cpp`、
  `src/io/tinygltf_impl.cpp`、`src/io/tinyobj_impl.cpp`（文件名即实现的库）；
  `third/` 不再放项目编写的包装单元，旧 `third/tinygltf/tiny_gltf_impl.cpp`
  （实际只含 stb 实现）与 `src/io/gltf_loader_impl.cpp` 已删除。
- **新增加载→采样端到端用例**：`tests/unit/gltf_animation_test.cpp` 写一个内嵌 buffer 的
  真实 glTF（三角形 + translation 动画），加载后把 `SceneAsset` 移出结果对象、删掉文件，
  再采样验证线性插值与首末帧钳制——锁定「加载器返回后仍可采样动画」。
- **新增行为与边界门禁**：`tests/unit/postprocess_test.cpp` 用独立算出的期望值锁定 ACES/gamma/
  brightPass/gaussianBlur/addBlend/resolveToLdr 的数值公式（并验证效果函数不修改源缓冲）；
  `check_boundaries.cmake` 增加公共头 include **循环依赖检查**（已用合成 a↔b 用例验证可命中）。
- **实测（Windows Release）**：全量构建 0 错误 0 警告；`unit_tests` **179/179**；
  `render_regression` **3/3**（基准图未更新）；**20 个离屏示例退出码全为 0**；
  可执行目标 **25 个**（与基线一致）；`ctest -L packaging` **3/3**
  （`architecture_boundaries` + `consumer_subdirectory` + `consumer_installed`）。
- **仍未完成**：覆盖率本地实测（本机无 LLVM/gcov 工具链，见 TASK-005/006）与
  macOS/Linux 远端 CI 结果——两者都必须真实跑过才能报告，本轮不宣称通过。

## 2026-10-23

### 与实测状态对齐：覆盖规则、discard 深度与示例调用点（TASK-014 阶段 1）

本轮先实测再修复。重构前基线（`develop` @ `2adfbf6`）：全量构建**失败**——
`stage01_framebuffer` 6 处与 `stage05_transform` 1 处 `drawLine`、`stage03_sprite_anim`
1 处 `fillRect` 未限定命名空间，另有 2 处 C4244 被 `/WX` 视为错误；`unit_tests`
169 用例中 166 通过 3 失败；`render_regression` 3/3 通过。

核实发现：`include/softrenderer/render/pipeline/renderer.h` 在 git 历史中只有初始化提交与一次 include 补齐，
**TASK-009（discard 深度提交）与 TASK-013（Renderer 共用覆盖规则）的修复从未进入该文件**，
与本变更日志和 `ACTIVE.md` 的既有陈述不符。判定以实测为准。

| 修复 | 文件 | 说明 |
|---|---|---|
| 覆盖规则统一 | `include/softrenderer/render/pipeline/renderer.h` | `rasterTriangle` 改用 `sr::TriangleCoverage` 的整数 top-left 规则，替换浮点边函数 + `>=0` 判定；重心坐标由整数边函数值换算，属性插值仍为浮点 |
| 深度提交顺序 | `include/softrenderer/render/pipeline/renderer.h` | 深度改在 `shader.fragment` 返回 `true` 后提交；被 discard 的片元不再占据深度 |
| 示例调用点 | `examples/stage01_framebuffer.cpp`、`examples/stage05_transform.cpp`、`examples/stage03_sprite_anim.cpp` | 补齐 `draw2d::` 限定；`fillCircle` 的 float 形参显式转换 |

验证：Windows Release 全量构建 0 错误 0 警告；`unit_tests` 169/169；`render_regression` 3/3
（基准图未更新）；20 个离屏程序退出码全为 0。

文档：新增 [ADR-004](adr/ADR-004-module-boundaries.md)（模块边界与 2.0 兼容性决策）；
新增父任务 [TASK-014](tasks/TASK-014-分层解耦重构.md) 与子任务 TASK-015~018；
补齐缺失的 [docs/tasks/README.md](tasks/README.md) 索引。

## 2026-10-06

### 子像素覆盖、MSAA 修复与主管线基线（TASK-013）

- 共享 sr::TriangleCoverage：8 位子像素、int64 边式及 y 向上的 top-left 规则，消除相同共享边的重复覆盖；检查非有限、退化和坐标范围。
- Renderer、基础三角形与标量/SSE2 共用覆盖规则，保持浮点属性插值；背面先剔除并优化取整/包围盒初始化。
- 提取 sr::MSAAFlatRenderer<1/4>，仅向覆盖且深度通过的样本提交颜色。MSAAFramebuffer 拒绝无效尺寸、容量溢出与非法样本编号。
- 新增可选 SR_BUILD_BENCHMARKS 与 renderer_benchmark，固定场景输出中位数、p95和哈希；不承诺所有场景加速。
- 新增 14 项测试；Release 169 单测、3 像素场景和两个消费工程通过；Debug ASAN 通过（2 项既有用例跳过）。相关三个离屏示例通过，基准图未修改。
- 更新 ADR-003、覆盖/MSAA/性能教程、架构、模块、构建开关、状态和任务档案。远端 Sanitizer 与跨平台验证仍待执行。

## 2026-10-05

### 19:30 第二轮：资源读取与任务失败（TASK-011/012）

- 新增 sr::gltf::AccessorView / readMeshPrimitive / convertModel，统一字节范围、类型、stride、归一化和索引校验，加载失败不发布部分资源。
- 动画实现正确 CubicSpline Hermite 插值，修复失败输出未初始化，保留 bind pose 并传递 channel 标志；移除不安全的 accessorFloats，sampleChannel 改为 bool。公共版本仍为 1.0.0，源码兼容迁移见 ADR-002。
- 修复 tinygltf 已解码像素的二次解码错误；拒绝不支持的像素布局；蒙皮示例通过共享视图读取列主序逆绑定矩阵。
- ThreadPool 增加 submitFuture；submit 异常由 waitAll 报告；回收部分启动线程、排空析构和防止 worker 自等待。并行命令收集错误并等待全部回调结束。
- 新增独立 glTF、动画、线程测试，增加 25 项单测；Ubuntu 独立 TSAN 作业已配置、未远端执行。
- 更新加载/纹理/动画/线程教程、架构依赖、支持范围、当前状态与任务档案。

验证：Release 155 单测、3 像素场景和两个消费工程通过；Debug ASAN 153 单测通过、2 项按约定跳过；三个相关 demo 成功输出。线程创建失败只审查清理路径，未作 OS 故障注入；远端 TSAN/UBSAN 和覆盖率仍待执行。

### 运行时正确性、内存复用与库集成（TASK-008/009/010）

- Renderer 仅在 fragment 接受片元后写入深度，修复 discard 镂空区域错误遮挡；不改变既有图像基准。
- Arena 增加对齐/长度/乘法校验、RAII 块所有权；reset 为 O(1) 并复用扩展块，新增 capacity() 观测保留容量。峰值内存保留到析构。
- 根 CMake 拆出 examples/practice/tests；新增 SR_BUILD_EXAMPLES、SR_BUILD_PRACTICE、SR_BUILD_TESTS，作为依赖时默认关闭。
- 修复子工程路径、版本宏、EnTT 安装 include 层级、tinygltf/json 漏装和线程传递依赖；支持相对自定义安装目录。
- 新增独立 Arena/Renderer 单测和两种真实下游工程验证；修复 Debug /WX 的 GTEST_SKIP 不可达代码问题；新增 Ubuntu ASAN+UBSAN CI。
- 覆盖率提取限定仓库根下的 src，排除第三方库内同名 src 目录。
- 统一 README、当前状态、任务入口、架构及构建/分配器文档。

本地验证：Release 全量 25 个程序；130 个单元测试与 3 个像素场景通过；源码引入、安装前缀迁移的消费工程通过。Debug MSVC ASAN 下 128 单测通过、2 个 Release-only 用例跳过，3 个像素场景通过。本轮没有远端 Linux/macOS、UBSAN、TSAN、覆盖率或静态分析的运行结论。

## 2026-09-07

### 零警告强制与 clang-tidy 配置（TASK-005c 部分）

| 变更 | 影响范围 |
|---|---|
| `sr_dev_flags` 加 `/WX`（MSVC）与 `-Werror`（GCC/Clang） | `CMakeLists.txt` |
| 新增 `.clang-tidy`：启用五组检查，对 12 项已知会在本代码库误报的逐条写明禁用理由 | `.clang-tidy` |
| 新增 `third/.clang-tidy`（`Checks: '-*'`）屏蔽 720 个 vendored 文件 | `third/.clang-tidy` |
| `cpp_project_rules.md` 增加 snake_case 文件命名例外条款 | `.qoder/rules/` |

开启 `/WX` 的依据是实测：多次全量构建在 `/W4` 下已是零警告，所以它只是把
已有约定变成构建期强制。实测确认 `unit_tests.vcxproj` 的 Debug 与 Release
两套配置均含 `<TreatWarningAsError>true</TreatWarningAsError>` +
`<WarningLevel>Level4</WarningLevel>`，且开启后全量重编零错误。

作用范围限缩：`sr_dev_flags` 只被本仓示例 / 练习 / 测试目标链接，因此
`third_party_impl`、googletest、spdlog 与**下游集成方均不受影响**。

> `clang-tidy` 本身仍未安装，配置**未经实跑调参**，已在文件头显式标注。
> 全仓 `clang-format` 同样待工具就位后单独 commit 执行。

### 全局命名空间收敛第 2 批：core 高级缓冲（TASK-007 / ADR-001）

**不兼容变更（API Break）**：以下符号需加 `sr::` 前缀：
`SSAO`、`SSAOConfig`、`GBuffer`、`FloatFramebuffer`、`MSAAFramebuffer`、
`kMSAA4Offsets`、`CommandBuffer`、`ThreadPool`。

全局类型数 **52 → 45**（TASK-007 累计 60 → 45）。均**不留全局别名**。

顺带修复两个真实缺陷：

| 缺陷 | 说明 |
|---|---|
| `msaa_framebuffer.h` 用 `std::pair` 却未 include `<utility>` | 与 TASK-004 发现的 `clip.h`/`renderer.h` 同类：MSVC 靠传递包含侥幸通过，libstdc++/libc++ 不保证 |
| `command_buffer.h` 的 `ThreadPool::stop_` **未初始化** | 它是线程池退出标志，工作线程构造后立即读取。未初始化值叠加多线程 → **难重现的偶发挂死或提前退出**，是最难查的一类缺陷 |

### 实测发现：测试文件不能用 `using namespace sr;`

原以为全局哨兵声明会遮蔽 using-directive 引入的名字，实测错误：

```
tests\test_core.cpp(1181,5): error C2872: “Position”: 不明确的符号
```

1. using-directive 把 `sr` 的成员当作**最近公共外围命名空间**（此处即全局）
   的成员参与查找，与真正的全局声明**同级** → 二义性，而非遮蔽
2. gtest 的 `TEST(GBuffer, ...)` 会创建名为 `GBuffer` 的**命名空间**，
   与 `sr::GBuffer` 类同名也冲突

因此 `tests/test_core.cpp` 统一写显式 `sr::` 限定；示例（无哨兵、无 gtest 宏）
则可用 `using namespace sr;`，已用于 `demo_stars.cpp` /
`stage11_postprocess.cpp` / `stage9_msaa.cpp`。

> 后续若想恢复测试文件的 using-directive 便利，应先把编译期哨兵拆到单独文件。
> 而这正好是 TASK-006 测试目录拆分的一部分，**两者宜合并执行**。

### 全量引用面实测数据已入库

52 个剩余类型的引用次数 / 文件数已逐项实测并记入
`docs/tasks/TASK-007-全局命名空间收敛.md`，作为后续分批的直接依据。

关键结论：**`math/` 的低文件数是假象** —— `Vec3` 只出现于 1 个文件，
但全仓实际用的是别名 `Vec3f`（未被统计）。`Color` 已达 400 次 / 49 文件，
`Vec3f` 只会更多。因此 `math/` 应归入高引用面、放到最后。

---

### 全局命名空间收敛第 1 批：ECS 组件（TASK-007 / ADR-001）

**不兼容变更（API Break）**：8 个 ECS 组件类型从全局命名空间消失，
现需写 `sr::` 前缀。

| 旧（全局） | 新 |
|---|---|
| `Position` | `sr::Position` |
| `Velocity` | `sr::Velocity` |
| `Rotation` | `sr::Rotation` |
| `Scale` | `sr::Scale` |
| `MeshRef` | `sr::MeshRef` |
| `MaterialRef` | `sr::MaterialRef` |
| `Light` | `sr::Light` |
| `CameraTag` | `sr::CameraTag` |

迁移指引：`registry.emplace<Position>(e, ...)` → `registry.emplace<sr::Position>(e, ...)`，
`registry.view<Position, Velocity>()` → `registry.view<sr::Position, sr::Velocity>()`。
成员字段与语义未变（`MeshRef::meshIndex` 仍为 `-1` 表示未设置）。

#### 为何现在就做、且不留全局别名

先立了 [`ADR-001`](adr/ADR-001-namespace-convergence.md)，其中推翻了 TASK-007
原计划的「全量别名桥」方案。关键认识：

> `using sr::Position;` 仍然把 `Position` 注入全局命名空间，下游照样撞名。
> **别名桥不解决撞名问题**，它只是让大改动可以分批提交的工具，
> 不是对下游的兼容承诺。因此对高冲突名字不应使用别名桥。

改为**按引用面分类**：实测发现引用面最小、冲突风险最高的正是这 8 个 ECS 组件：

| 实测项 | 结果 |
|---|---|
| 引用 `scene/components.h` 的文件 | **恰好 2 个**（自身 + `tests/test_core.cpp`） |
| `Light` 除定义处外的使用次数 | **0** |
| 使用 ECS 的示例（共 22 个） | **0 个** |

所以可以一次完整迁完，立即获得真实的撞名消除，而不是只做准备工作。

#### 新增

| 文件 | 内容 |
|---|---|
| `docs/adr/README.md` | ADR 索引 + 5 个待立 ADR 的已知决策点 |
| `docs/adr/ADR-001-namespace-convergence.md` | 4 个备选方案对比、选定理由、失效条件 |
| `include/softrenderer/core/namespace.h` | `namespace sr` 的约定锚点与迁移进度文档 |

#### 回归防护：编译期哨兵

`tests/test_core.cpp` 在**全局作用域**声明了 8 个同名哨兵类型：

```cpp
struct Position { int canary = 0; };
struct Light    { int canary = 0; };
// … 共 8 个
```

这能编译通过本身就证明了目标达成：下游使用者现在**可以**定义自己的
`Position` / `Light` 而不与本库冲突。若将来有人加回 `using sr::Position;`，
测试文件会直接报**重定义编译错误** —— 把 ADR-001 的核心结论用编译器锁定，
而不只写在文档里。

另新增用例 `ECS.globalCanaryTypesAreDistinctFromSrComponents`，用
`std::is_same` 断言哨兵类型与 `sr::` 组件确实是不同类型。

#### 验证

| 验证项 | 结果 |
|---|---|
| 全局类型数扫描 | **60 → 52**，8 个 ECS 名字均已不在全局作用域 |
| Release 全量构建 | 零错误零警告，退出码 0 |
| 单元测试 | 120 → **121 全绿** |
| 像素回归 | 3 个场景仍匹配基准图（证明迁移未改变任何渲染输出） |
| 抽样 11 个示例 | 全部正常退出 |
| exe 清单 | **25 个**，与基线一致 |

#### 版本号处理

本次已构成 API Break，但**暂不升 2.0.0**。理由：项目从未发布过任何 tag、
无外部使用者、`VERSION 1.0.0` 仅用于生成版本头。待 52 个类型全部迁完、
真正对外发布时一次性升版，避免为每批迁移反复改版本号。已在 ADR-001 记录。

---

## 2026-09-05

### 覆盖率接入与测试规则例外（TASK-006 部分完成）

| 变更 | 影响范围 |
|---|---|
| `ci.yml` 新增独立 `coverage` job（ubuntu + gcov + lcov + genhtml） | `.github/workflows/ci.yml` |
| 只统计 `src/`（`lcov --extract "*/src/*"`），排除系统头 / 第三方 / 测试代码本身 | 同上 |
| `.gitignore` 补 `build-*/` 与 `coverage*.info` | `.gitignore` |
| `testing_rules.md` 新增两条项目例外条款 | `.qoder/rules/testing_rules.md` |

**门禁策略：首次只报告不设阈值。**

`testing_rules.md` 要求行覆盖率 ≥ 80%、分支 ≥ 60%，但在实测基线未知的情况下
直接设门禁极可能立即红灯，违反「禁止带红灯合并」而使 CI 失去意义。
先把基线量出来写进本文件，下一轮再启用棘轮式门禁（不允许下降）。

**为何覆盖率 job 不与 Sanitizer 共用构建**：ASAN 与 gcov 同时开会相互干扰，
且目标不同（一个查内存错误、一个量覆盖度）。

**未验收**：覆盖率 job 需 Linux 环境，本地无法验证，验收依赖远端 CI 首次运行。
已知风险：`lcov` 各版本的 `--ignore-errors` 可接受值不同（lcov 2.x 对 unused /
empty / inconsistent 默认报错退出），已预防性列全，首次运行可能仍需微调。
因为本 job 不设阈值、不阻塞合并，即使需微调也不会让 CI 失去意义。

#### 新增的两条规则例外条款

1. **存量用例不批量重命名**：120 个用例采用 `TEST(模块名, 场景描述)` 的
PascalCase 命名，不符合规定的 `被测方法_场景_预期结果` 格式，但不属于禁止的
无意义命名。批量重命名会改变 ctest 测试名、使 CI 历史失去可比性，属低价值高扰动。
**新增用例强制用新格式**（已执行）。

2. **插值类测试的数据要求**：输入必须让各顶点取值互不相等，且采样点不得取
对称盲点。源于 TASK-002 的真实教训：顶点深度全相等时任何权重排列都算出
同一个值，缺陷被 100% 掩盖。同时记录了「一致性测试不能替代正确性测试」。

#### 测试目录拆分：已推迟

原计划把 `tests/test_core.cpp`（1300+ 行 / 120 用例 / 33 套件）拆为
`tests/unit/<模块>/<类名>_test.cpp`。**本次推迟**，理由：

环境阻止删除文件（TASK-004 中 `DeleteFile` 连续两次报成功而文件仍在）。
拆分的最后一步必然是删除原文件；若无法删除，它只能被清空成惰性桩
（否则 120 个 `TEST()` 与拆分后的文件重复定义，直接链接失败），
结果是仓库里同时存在「桩文件 + 30 个拆分文件」，**比拆分前更乱**。

拆分是纯机械操作，收益是目录合规 + include 面收敛，**无功能价值且无时间敏感性**；
而留下桩文件的代价是持续的可读性损害。完整拆分程序已写入
`docs/tasks/TASK-006-测试目录重构与覆盖率.md`，**执行前必须先确认能删除文件**。

---

### 构建系统现代化（TASK-005 第一、二节，已完成）

#### target-based 改造

消除 `cpp_project_rules.md`「构建系统规范」明令禁止的全局指令：

| 变更 | 影响范围 |
|---|---|
| 删除 **6 处** `include_directories()` | `CMakeLists.txt` |
| 删除 `link_libraries(softrenderer)` | 同上 |
| 删除 `add_compile_options(/utf-8 /W4)` 与两处 `add_compile_definitions` | 同上 |
| 新增 `sr_add_example()` / `sr_add_realtime_example()` 函数，22 个示例改为调用 | 同上 |
| 新增 `sr_dev_flags` INTERFACE 库（项目内部开发标志，**不导出**） | 同上 |
| 删除 3 处冗余的 `target_link_libraries(... third_party_impl)` | 同上 |

**关键发现**：那 6 处 `include_directories()` 与 `softrenderer` 的 INTERFACE include、
`third_party_impl` 的 PUBLIC include **完全重复** —— 所以是直接删除而非搬迁，
语义零变化。改造后残留全局指令 **0 处**，`target_*` 调用 27 处。

**「公共接口要求」与「开发偏好」的划分**：

| 标志 | 位置 | 理由 |
|---|---|---|
| include 路径、`cxx_std_23`、`/utf-8`、`_CRT_SECURE_NO_WARNINGS` | `softrenderer` INTERFACE（导出） | 下游编译本项目头文件时真正必需 |
| `/W4`、`-Wall -Wextra`、`NOMINMAX`、`WIN32_LEAN_AND_MEAN` | `sr_dev_flags`（**不导出**） | 本项目的开发偏好，不应强加给集成方 |

> `/utf-8` 与 `_CRT_SECURE_NO_WARNINGS` 属接口要求而非偏好：前者因为 `src/`
> 头文件含大量中文注释（不开报 C4819）；后者因为 `core/tga.h` 在**头文件内**
> 直接调 `std::fopen`，该调用在使用方的编译单元展开，MSVC 会在**使用方**报 C4996。
>
> 后者是个妥协（会抑制下游自己的 C4996），根治办法是把 `tga.h` 的 I/O
> 改成不依赖弃用函数，已记为待办。

#### Sanitizer

新增 `option(SR_ENABLE_SANITIZERS ... OFF)`，满足「Debug 必开 ASAN/UBSAN、
Release 禁止开启」的要求。此前项目**没有任何 Sanitizer 配置**，而 TASK-003 修的
整数溢出 UB 与越界读正是 ASAN/UBSAN 能自动抓到的类型。

三个关键设计选择：

1. **用 `$<$<CONFIG:Debug>:...>` 而非 `if(CMAKE_BUILD_TYPE STREQUAL "Debug")`**：
   MSVC 是多配置生成器，`CMAKE_BUILD_TYPE` 为空，一次配置产出两套产物。
   只有生成器表达式能把标志精确限定到 Debug。
2. **用全局 `add_compile_options` 而非 per-target**：Sanitizer 是 **ABI 级**设置。
   实测证据：只加到项目目标上时，googletest（`add_subdirectory` 引入）未开 ASAN，
   链接报 **LNK2038** `annotate_string` / `annotate_vector` 不匹配 + LNK1319。
3. **整个块移到所有 `add_subdirectory` 之前**：`add_compile_options` 改目录属性，
   只对其后的目标与子目录生效；`third/log`（spdlog）在第 19 行就引入了。

第 2、3 点是 `engineering_rules.md`「禁止全局指令」的**有意例外**，
例外理由已写在 `CMakeLists.txt` 对应位置的注释里。

**实测验证**：

| 验证项 | 结果 |
|---|---|
| Debug 构建（ASAN） | 零错误（LNK2038 已消除） |
| Debug 运行 | **118 passed / 2 skipped / 0 failed**，**无 AddressSanitizer 报错** |
| 2 个 skipped | 正是两个越界测试（Debug 下 `assert` 生效→`GTEST_SKIP()`），与 TASK-003 设计一致 |
| **Release 是否带 Sanitizer** | Debug exe 5,087,744 B / 导入 ASAN DLL = **True**；Release exe 847,360 B / = **False** |
| 改造后 Release 全量构建 | 零错误零警告，**25 个 exe 与基线逐项一致** |
| 改造后测试 | 121 项 ctest 全绿 / 12.48 秒；120 单元 + 3 回归全绿 |
| 20 个离屏示例 | 全部正常退出 |

最后两行是本节的关键验收：体积相差 6 倍且只有 Debug 导入 ASAN 运行时，
机械化地证明了「Release 禁止开启 Sanitizer」而不是靠假设。

**阻塞项**：第三节（`/WX` + `.clang-tidy`）与第四节（全仓 clang-format）需先安装
LLVM 工具链（实测 `clang-format` / `clang-tidy` 均未安装），本次未做。

#### 文档同步

- `docs/troubleshooting.md`：新增两个 Sanitizer 坑——退出码 `0xC0000135`
  （ASAN 运行时 DLL 不在 PATH）与 LNK2038（ABI 不一致）
- `engineering_rules.md`「命令」节：更新为 120 用例 / `-LE fast` / `-L regression` /
  `-L fast` 四种跑法，新增 Sanitizer 命令段，DoD 最小验证集补上 `render_regression`
  与「CMakeLists 改动需比对 25 个 exe 基线」

---

### CI 跨平台修复与像素回归接线（TASK-004，代码完成）

单元测试 113 → **120**；新增集成回归目标 `render_regression`（3 个场景）；
全量 `ctest` 从 **39.89 秒 → 14.06 秒**；Windows 上 24 个 exe 全部产出。

#### CI 修复

| 变更 | 影响范围 |
|---|---|
| **修复 ubuntu / macos job 必然编译失败**：给 `stage1_sprite_anim` / `stage6_realtime` / `stage8_realtime` 的整个 `add_executable` 加 `if(WIN32)` 守卫（旧写法只守住了 `target_link_libraries`，没守住编译） | `CMakeLists.txt` |
| **去掉 smoke test 末尾的 `\|\| true`**：该写法让步骤永远不会失败，属典型的掩盖症状 | `.github/workflows/ci.yml` |
| smoke 清单 11 项 → 20 项（补 `stage12_threaded`、`stage1_sprite`、5 个 demo、`practice_hello_2d`） | 同上 |
| 新增「至少跑到 15 个」下限断言，防止「一个都没找到」也绿灯 | 同上 |
| 新增 **Verify platform guards** 步骤：把平台守卫修复本身变成回归测试 | 同上 |
| `ctest` 加 `--parallel -LE fast` | 同上 |

**守卫完备性的验证方式**：全仓扫描确认 `src/` 与 `tests/` 的 Win32 引用数为 **0**，
Win32 依赖只存在于三个已守卫的示例 + `platform/win32_window.h` 本身。
这比用 `if(FALSE)` 模拟更有说服力：证明的不是「`if(WIN32)` 语法有效」，
而是「没有遗漏的第四个文件」。

#### 像素回归接线

`pixel_regression.h` 与 `tests/golden/` 此前零引用、目录为空，能力未落地。
现已接线，并修复了**两个会导致永远假通过**的陷阱：

| 陷阱 | 旧行为 | 修复 |
|---|---|---|
| 硬编码相对路径 `"tests/golden/"` | `ctest --test-dir build` 的工作目录是 `build/`，解析成 `build/tests/golden/` → 找不到基准图 | 新增 `goldenDir()` / `goldenPath()`，由 CMake 用 `file(TO_CMAKE_PATH)` 注入**绝对路径** `SR_GOLDEN_DIR` |
| 基准图不存在时自动创建并返回 `passed=true` | 叠加上条 → **测试永远绿灯**，比没有测试更危险 | 改为 `SR_UPDATE_GOLDEN` 开关控制；默认（CI）下缺失**直接失败**并给出绝对路径与修复指引 |

**反向验证已做**：把一张基准图重命名走后，测试输出
`[REGRESSION] MISSING golden image: <绝对路径>` 并 **FAILED**（退出码 -1）。
并从 `build/` 目录运行验证了绝对路径注入确实绕开工作目录。

三个回归场景（均确定性、无随机、无时间依赖，128×96）：

| 场景 | 锁定什么 |
|---|---|
| `zbuffer_twoOverlappingCubes` | 深度测试 + 背面剔除 + 视锥裁剪的联合行为 |
| `pipeline_phongTexturedSphere` | 变换链 + 透视校正插值 + 纹理采样 + Phong 着色 |
| `blend2d_alphaComposition` | alpha 混合（三种模式）+ draw2d 图元 + 变换栈 + scissor + 点阵字体 |

刻意**不**回归 `demo_stars`（含随机星点）与 `stage12_threaded`（线程顺序不定）。

另新增 `PixelRegression` 单元套件（7 个用例）验证 `compare()` 本身的判定逻辑：
若它恒返回 true，三个集成场景就全是假通过。其中
`compare_uniformSmallDrift_failsOnAvgDelta` 验证「平均差 < 0.5」这个条件能**单独**触发失败
（全图每像素只差 1 时，无任何像素超单像素阈值，但系统性偏移仍必须被拦下）。

#### ctest 项数与标签

全量 `ctest` 共 **122 项** = 120（discover 展开）+ 1（`unit_tests_all`，`fast` 标签）
+ 1（`render_regression`，`regression` 标签）。

| 命令 | 项数 | 耗时 | 用途 |
|---|---|---|---|
| `ctest -L fast` | 1 | 0.43 秒 | 本地开发迭代 |
| `ctest -L regression` | 1 | 0.59 秒 | 只跑像素回归 |
| `ctest --parallel -LE fast` | 121 | 14.06 秒 | **CI 用**（排除与 discover 项重复的 `unit_tests_all`） |
| `ctest`（串行、无标签） | 122 | ~40 秒 | 不推荐 |

#### 待用户手动完成

| 项 | 原因 |
|---|---|
| 删除 `src/test/test_framework.h`（96 行自研 `minitest`，零引用） | 删除工具被环境阻断，连续两次报成功而文件仍在。属无害死代码，不影响构建 |
| 删除空目录 `src/platform/` | 同上（且 git 不跟踪空目录） |
| 推送到 GitHub 并确认三平台 job 全绿 | 需用户提供远端仓库；本地无法验证 Linux/macOS 构建 |

---

### 外部输入校验与边界安全（TASK-003，已完成）

单元测试 97 → **113**，全量构建**零错误零警告**（修前有 2 个 C4840），
20 个离屏示例全部正常。

#### 计划内修复

| 变更 | 影响范围 |
|---|---|
| **修复** `image_loader.h` 有符号整数溢出 UB：`for (int i = 0; i < w * h; ++i)` → `size_t` | `include/softrenderer/io/image_loader.h` |
| 新增尺寸校验与具名常量 `kMaxImageDimension = 16384`；变量声明即初始化 | 同上 |
| 新增 **RAII 释放**（`unique_ptr` + 自定义删除器）——新增的校验分支提前 return 会泄露图像缓冲 | 同上 |
| **修复** `Framebuffer::getPixel` 在 Release 下越界读 UB：越界返回 `colors::black` | `include/softrenderer/core/framebuffer.h` |
| **修复** `DepthBuffer::get` 越界读 UB（旧版连 `assert` 都没有）：返回 `kFar` | `include/softrenderer/core/depthbuffer.h` |
| **修复** `blend.h` alpha 混合截断而非四舍五入（每次混合系统性偏暗 ≤ 1/255）；新增 `toUint8Round` 统一钳制+取整 | `include/softrenderer/core/blend.h` |
| `blend.h` 补 `#include <algorithm>`（原依赖传递包含） | 同上 |
| 5 个枚举合规：`ClipPlane` 裸 enum → `enum class : uint8_t`；`BlendMode`/`AlphaMode`/`Path`/`Interpolation`/`Filter` 补底层类型 | `clip.h`、`blend.h`、`gltf_types.h`、`texture.h` |
| `clip.h` 新增 `static_assert` 防止 `kClipPlaneCount` 与枚举项数漂移 | `include/softrenderer/render/pipeline/clip.h` |

#### 实施中新发现并修复

| 变更 | 发现方式 |
|---|---|
| **`core/log.h` 内置模式重写为变参模板**，消除 4 个缺陷：① 非 POD 类型经 C 可变参传递是 UB；② `{}` 占位符**从不被替换**（格式串按 printf 解析）；③ 日志文本里的字面 `%` 被 `vfprintf` 当转换符解析 → UB；④ `levelName` 的常量数组**漏了 `Off`**（枚举 7 值 / 数组 6 项）→ 越界读 | 给 `image_loader.h` 加日志时构建报出 2 个 **C4840** |
| 新增可测试入口 `sr::formatLog`（从 `Log::log` 拆出），使格式化逻辑可直接断言而无需捕获 stderr | `engineering_rules.md`「代码必须保留可测试入口」 |
| **`gltf_types.h` 的 `Channel::path` 补默认值**：`gltf_loader.h:327-332` 只在 `target_path` 为三个已知值时赋值，而 glTF 2.0 允许 `"weights"` 与扩展值 → 旧代码是不确定值，读取即 UB。另 4 个 int 字段做防御性初始化（统一 `-1` 哨兵） | 改枚举时通读同文件 |
| **三处注释声称不存在的文件**：`image_loader.h:12`（`third/stb/stb_image_impl.cpp`）、`image_write.h:16`（`third/stb/stb_image_write_impl.cpp`）、`CMakeLists.txt:22-24`（「一个 .cpp 同时提供 stb + tinygltf」）均已修正 | 按「修改文件时若头注释过时须同步更新」核查 |

#### 第三方实现单元归属的真相（与文件名直觉相反）

| 实现单元 | `#define` | 提供的符号 |
|---|---|---|
| `third/tinygltf/tiny_gltf_impl.cpp` | `STB_IMAGE_IMPLEMENTATION`、`STB_IMAGE_WRITE_IMPLEMENTATION` | **stb_image + stb_image_write**（不含 tinygltf） |
| `src/scene/gltf_loader_impl.cpp` | `TINYGLTF_IMPLEMENTATION` | **tinygltf** |
| `third/tiny_obj_loader_impl.cpp` | `TINYOBJLOADER_IMPLEMENTATION` | **tinyobjloader** |

> 教训：本次先轻信了 `CMakeLists.txt` 的旧注释并写进了 `third/README.md` 与
> `docs/architecture.md`，读了 `.cpp` 后才发现错误，已全部修正。
> **结论：涉及第三方实现归属时必须读源文件，不能依赖文件名或既有注释推断。**

#### 新增测试（16 个）

| 套件 | 用例 |
|---|---|
| `Framebuffer`（新） | `framebuffer_getPixel_outOfBounds_returnsSentinel` |
| `DepthBuffer` | `depthBuffer_get_outOfBounds_returnsFar` |
| `Blend` | `toUint8Round_roundsAndClamps`、`blendPixels_alphaBlendRoundsInsteadOfTruncating`、`blendPixels_allModesNeverWrapAround` |
| `ImageLoader`（新） | `loadImage_missingFile_returnsFalse`、`loadImage_emptyPath_returnsFalse`、`loadImage_roundTripPreservesPixelsAndOrientation` |
| `Log`（新） | `formatLog_noArgs_returnsFormatString`、`formatLog_substitutesBracesInOrder`、`formatLog_acceptsNonPodTypes`、`formatLog_extraArgsAreAppendedNotDropped`、`formatLog_extraBracesAreKeptLiteral`、`formatLog_literalPercentIsSafe`、`levelName_coversAllEnumValues`、`setLevel_roundTrips` |

> `loadImage` 此前**全仓零调用方**，往返测试是它第一次被真正执行。
> 往返测试写系统临时目录而非仓库，避开提交二进制 fixture。

#### 公共接口影响

| 变更 | 兼容性 |
|---|---|
| `enum ClipPlane` → `enum class ClipPlane : uint8_t` | **API Break**，但实测仅 `clip.h` 内部使用，无外部引用点 |
| `kClipPlaneCount` 由枚举项改为 `static constexpr int` | 同上，仅内部使用 |
| `BlendMode` / `AlphaMode` / `Path` / `Interpolation` / `Filter` 补底层类型 | 兼容（枚举值与用法不变） |
| `sr::Log::log` 由 C 可变参改为变参模板 | 兼容（调用语法不变，且现在接受更多类型） |
| `blendPixels` 结果可能 **+1** | **行为变更**：四舍五入代替截断。影响所有使用 alpha 混合的输出图像 |
| `sr::formatLog`、`sr::levelName`、`toUint8Round` | 新增公共符号（均在全局或 `namespace sr`） |

> ⚠️ `blendPixels` 的行为变更会使**未来的像素回归基准图**与修复前不同。
> TASK-004 接线像素回归时必须以**修复后**的输出为基准。

---

### 光栅化器缺陷修复（TASK-002，已完成）

commit `73122d3`，`--no-ff` 合入 `develop`。

| 变更 | 影响范围 |
|---|---|
| **修复** `rasterizer_fast.h` 深度插值的重心权重错位（同序映射 → 错位映射），标量版 + SSE2 版共 6 处 | `include/softrenderer/render/raster/rasterizer_fast.h` |
| **修复** `setupEdges` 不做符号归一化导致顺时针三角形被静默丢弃 | `include/softrenderer/render/raster/rasterizer_fast.h` |
| 改写文件头注释，标注本模块为**实验性**（未接入 `Renderer` 主链路）及两个原因 | `include/softrenderer/render/raster/rasterizer_fast.h` |
| **新增 3 个回归测试**，单元测试 94 → 97 | `tests/test_core.cpp` |
| 校正 `docs/engineering/54` 全部五节数据漂移，新增「实验性能力」分区 | 文档 |

**修复前实测误差**：

| 采样点 | 期望深度 | 实际深度 | 误差 |
|---|---|---|---|
| 靠近 v0（w0=0.865） | 0.17917 | 0.50417 | **0.325** |
| 靠近 v1 | 0.49583 | 0.82083 | **0.325** |
| 靠近 v2 | 0.82083 | 0.19583 | **0.625** |
| 顺时针绕序 | 与逆时针一致 | `fb` 全黑、`zb` 恒为 `1e9` | **三角形完全丢弃** |

**为何长期未被发现**：原有两个 `RasterizerFast` 用例传入的深度全为
`0.5f / 0.5f / 0.5f`。顶点深度全相等时任何权重排列都算出同一个值，
缺陷被 100% 掩盖。已据此在 `engineering_rules.md` 新增红线：
「禁止用三个顶点取值全相同的数据写插值类测试」。

**公共接口影响**：无。`fillTriangleFast` / `fillTriangleIncremental` / `fillTriangleSSE2`
签名未变，仅修正内部计算与新增绕序容错。

---

### 工程基线建设（TASK-001）

| 变更 | 影响范围 |
|---|---|
| 初始化 git 仓库于 `develop` 分支，首次导入 891 个文件 | 全仓 |
| 新增 `.gitignore`，覆盖构建产物 / IDE / 测试输出 / 第三方构建产物 / 临时文件五类 | 全仓 |
| 新增 `output/.gitkeep`：所有示例以相对路径 `output/xxx` 写出结果，目录缺失会静默失败 | examples/、practice/ |
| 新增根 `README.md`（项目简介 / 特性 / 依赖 / 构建 / 快速启动 / 目录结构） | 文档 |
| 新增 `docs/architecture.md`（分层结构、模块依赖明细、数据流时序图、已知权衡） | 文档 |
| 新增 `docs/modules.md`（6 个 `src/` 子模块 + `platform/` 的职责与扩展点） | 文档 |
| 新增 `docs/changelog.md`、`docs/troubleshooting.md`、`docs/tasks/` | 文档 |
| 新增 `third/README.md`：8 个依赖的版本、许可证与**源码实测证据** | 文档 |
| 填写 `.qoder/rules/engineering_rules.md` 的「红线约束」与「命令」两节（原为模板占位符） | 规则 |

**实测校正的数据**（原 `docs/engineering/54-项目当前状态总览.md` 与实际不符）：

| 项 | 文档原值 | 实测值 | 证据 |
|---|---|---|---|
| GoogleTest 版本 | 1.15 | **1.18.0** | `third/googletest-main/CMakeLists.txt:7` |
| stb_image 版本 | latest | **2.30** | `third/stb/stb_image.h:1` |
| stb_image_write 版本 | latest | **1.16** | `third/stb/stb_image_write.h:1` |
| tinygltf 版本 | 2.9.3 | **v2.9.\***（头文件未锁定修订号） | `third/tinygltf/tiny_gltf.h:28` |
| `src/` 头文件数 | 35 | **52** | 实测统计 |
| 示例程序数 | 20 | **22** | 实测统计 |
| 文档篇数 | 54 | **72** | 实测统计 |
| 单元测试 | 62 用例 / 22 套件 | **94 用例 / 29 套件** | `unit_tests.exe` 实际运行输出 |
| C++ 标准 | 「`src/` 纯 C++17 标准库」 | **C++23**（`vec.h` 用 `requires` 约束） | `CMakeLists.txt:6` |

**新发现的架构事实**（原无处记录）：

- `third/entt/` 下存在**两份版本不同的 EnTT**：`src/` 是 3.14.0（构建实际使用），
  `single_include/` 是陈旧的 3.13.2
- `raster::fillTriangleFast` **从未被 `Renderer` 调用**，仅 `core/benchmark.h`
  与单元测试引用
- `core/benchmark.h → raster/rasterizer_fast.h` 构成 `core/ → raster/` 的**层次倒置**

---

## 欠债登记

按 `documentation_rules.md`「早期原型阶段可暂缓部分文档，但必须在 changelog 记录欠债」：

| 欠债 | 说明 | 跟进 |
|---|---|---|
| 全局命名空间污染 | **60 个**顶层类型位于全局命名空间（分布在 30 个头文件），`Position`/`Light`/`Texture` 等名字对下游集成方构成冲突风险 | TASK-007 |
| `docs/api.md` / `docs/class_reference.md` 缺失 | 公共接口目前只能从 `docs/modules.md` 与各头文件注释获取 | TASK-001 后续 |
| 第三方版本未完全锁定 | googletest 为 main 分支快照；tinygltf / tinyobjloader 未锁定修订号 | `third/README.md`「已知版本锁定缺口」 |
| 无覆盖率度量 | `testing_rules.md` 要求核心模块行覆盖率 ≥ 80%，当前无任何覆盖率配置 | TASK-006 |
