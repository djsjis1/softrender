# TASK-007：全局命名空间收敛

## 基本信息

- 创建日期：2026-09-05
- 状态：**第 1 批已完成**（ECS 组件），余 52 个类型待分批
- 涉及模块：**全部** `src/`、`examples/`（22 个）、`tests/`、`practice/`
- 变更级别：**L**（架构级 + 破坏公共接口 + 已建 ADR）
- 前置依赖：TASK-001、TASK-005（格式化完成）、TASK-006（测试拆分完成）
- **决策记录**：[`docs/adr/ADR-001-namespace-convergence.md`](../adr/ADR-001-namespace-convergence.md)

> ⚠️ **本档案下方的「收敛策略：渐进式」一节已被 ADR-001 推翻。**
> 原计划的「全量别名桥」方案有根本缺陷：`using sr::Position;` 仍然把
> `Position` 注入全局命名空间，**别名桥不解决撞名问题**。
> 实际采用的是 ADR-001 的方案 D：**按引用面大小分类**。
> 下方原文保留作为决策演进记录（ADR 不得事后修改，但任务档案可以标注）。

## 目标

把 `src/` 中位于全局命名空间的核心类型收敛进 `namespace sr`，
消除对下游集成方的命名冲突风险。

## 现状（实测扫描）

**60 个顶层类型位于全局命名空间，分布在 30 个头文件**：

| 模块 | 文件 | 全局类型 |
|---|---|---|
| `core/` | `color.h` | `Color` |
| | `framebuffer.h` | `Framebuffer` |
| | `depthbuffer.h` | `DepthBuffer` |
| | `float_framebuffer.h` | `FloatFramebuffer` |
| | `msaa_framebuffer.h` | `MSAAFramebuffer` |
| | `gbuffer.h` | `GBuffer` |
| | `ssao.h` | `SSAOConfig`、`SSAO` |
| | `command_buffer.h` | `CommandBuffer`、`ThreadPool` |
| `math/` | `vec.h` | `Vec2`、`Vec3`、`Vec4` |
| | `mat3.h` / `mat4.h` / `quat.h` / `rect.h` | `Mat3`、`Mat4`、`Quat`、`Rect2` |
| `raster/` | `clip.h` | `ClipVertex` |
| | `renderer.h` | `Renderer` |
| | `renderer2d.h` | `Renderer2D` |
| `scene/` | `camera.h` | `Camera`、`Camera2D` |
| | `components.h` | `Position`、`Velocity`、`Rotation`、`Scale`、`MeshRef`、`MaterialRef`、`Light`、`CameraTag` |
| | `mesh.h` | `Vertex`、`Mesh` |
| | `texture.h` | `Texture` |
| | `cubemap.h` | `Cubemap` |
| | `skeleton.h` | `Bone`、`Skeleton`、`VertexBoneWeights`、`Keyframe`、`AnimationChannel`、`Animation` |
| | `animation_player.h` | `AnimSample` |
| | `particle_system.h` | `Particle`、`EmitterConfig`、`ParticleSystem` |
| | `render_queue.h` | `DrawItem`、`RenderQueue` |
| | `gltf_types.h` | `GltfMaterial`、`GltfTexture`、`GltfImage`、`GltfNode`、`GltfSkin`、`GltfAnimation` |
| `shader/` | `shader.h` | `Varyings`、`Shader` |
| | `shaders.h` | `FlatShader`、`GouraudShader`、`PhongShader` |
| | `advanced_shaders.h` | `DepthShader`、`FinalShader` |
| | `pbr_shader.h` | `PBRShader` |

此外还有全局**枚举**：`clip.h` 的 `enum ClipPlane`、`blend.h` 的 `enum class BlendMode`；
以及全局**自由函数**：`blendPixels`、`interpolateVaryings`、`drawLine`、
`drawLineGradient`、`fillTriangle` 等。

已在命名空间内的（**不需要迁移**）：`sr`（arena / benchmark / log / profiler /
resource_manager）、`sr2d`、`colors`、`transform`、`tga`、`imagewriter`、
`imageloader`、`meshgen`、`texgen`、`objloader`、`gltfloader`、`raster_fast`、
`fxaa`、`postprocess`、`regression`。

## 为什么这是最高优先级的架构问题

### 1. 已经造成过实际冲突（不是理论风险）

`namespace sr2d` 的引入原因，就是 2D 图元函数名（`fillRect` / `fillCircle` /
`SpritePalette`）与 `examples/` 中已有的全局同名符号冲突。
**同一个仓库内部就已经发生过命名冲突。**

### 2. 对下游集成方的影响是致命的

本项目的核心卖点是「可作为纯头文件库集成使用」（`CMakeLists.txt` 提供
`SoftRenderer::softrenderer` 别名与 `find_package` 支持）。

而 `Position`、`Velocity`、`Rotation`、`Scale`、`Light`、`Camera`、`CameraTag`、
`Animation`、`Keyframe`、`Bone`、`Texture`、`Mesh`、`Vertex`、`Color`、`Particle`、
`Shader` 这些名字，**下游用户几乎必然已经在自己的代码里定义过**。

任何人 `#include "scene/components.h"` 都会撞上重定义错误——而这 8 个 ECS 组件
恰恰是本项目 ECS 能力的入口。

### 3. 违反明确规范

`cpp_project_rules.md`「命名空间规范」：「禁止全局命名空间定义业务符号」。

## 收敛策略：渐进式，禁止一次性大改

一次性把 60 个类型全搬进 `namespace sr` 会触及**每一个源文件**
（52 个头文件 + 22 个示例 + 1079 行测试 + `practice/`），
且会立刻破坏所有下游代码。

分三阶段，**每阶段独立可提交、可回滚**：

### 阶段一：建立命名空间与别名桥（不破坏任何现有代码）

新建 `src/core/namespace.h`：

```cpp
#pragma once
// 所有 SoftRenderer 公共符号的目标命名空间。
// 迁移期间全局作用域保留 using 别名，现有代码无需修改即可编译。
namespace sr {}
```

迁移某个类型时：把定义移入 `namespace sr`，在全局作用域保留
`using sr::Framebuffer;` 等别名。**现有代码一行不改即可继续编译**，
新代码用 `sr::` 前缀。

### 阶段二：逐模块迁移，按冲突风险从高到低

| 优先级 | 目标 | 理由 |
|---|---|---|
| 1（最高危） | `scene/components.h` 的 8 个 ECS 组件 | `Position`/`Velocity`/`Rotation`/`Scale`/`Light`/`CameraTag` 是 ECS 领域标准命名，几乎必然冲突 |
| 2 | `scene/skeleton.h`（`Animation`/`Keyframe`/`Bone`/`Skeleton`）、`scene/texture.h`（`Texture`）、`scene/mesh.h`（`Mesh`/`Vertex`）、`scene/camera.h`（`Camera`/`Camera2D`） | 图形领域通用名 |
| 3 | `core/color.h`（`Color`）、`math/`（`Vec2/3/4`、`Mat3`、`Mat4`、`Quat`、`Rect2`） | 数学库名，与 Eigen / glm 等易冲突 |
| 4 | `shader/`（8 个 Shader 类型 + `Varyings`）、`core/` 其余、`raster/` | 名字已带项目特征，冲突概率较低 |

每迁移一个模块的固定流程：
改定义 → 删除该模块的全局 `using` 别名 → 全量构建 → 跑 94 测试 → **单独 commit**。

### 阶段三：移除全部全局别名，升主版本号

`cpp_project_rules.md`「ABI / API 兼容性规范」：「禁止在同一大版本内删除公共接口」。
移除别名属 **API Break**，必须升主版本：

- `CMakeLists.txt:4` `VERSION 1.0.0` → `2.0.0`
- `cmake/version.cmake` 同步
- 在 `docs/changelog.md` 标注为不兼容变更，并给出迁移指引

## 必须建 ADR

本任务是架构级变更 + 影响公共接口，`architecture_decision_record.md` 与
`ai_workflow_rules.md` Step 3 都要求记录。ADR 需写明：

- **结论**：收敛到 `namespace sr`，分三阶段渐进迁移
- **权衡**：下游兼容性 vs 命名安全
- **被拒方案**：
  1. *一次性全量迁移* —— 拒绝：单个 commit 触及全仓，无法 review、无法二分定位回归
  2. *不迁移，只在文档中告知用户注意冲突* —— 拒绝：header-only 库的重定义错误
     是编译期硬错误，用户无法绕过；且 `sr2d` 的历史证明冲突会真实发生
  3. *改用 `inline namespace` 做版本化* —— 拒绝：本项目无多版本共存需求，
     额外复杂度无收益
- **适用与失效条件**：适用于 header-only 库形态；若将来改为编译型静态/动态库
  （符号不再进入用户编译单元），命名冲突风险大幅降低，可重新评估

## 进度清单

### 第 1 批：ECS 组件 —— 已完成（2026-09-07）

- [x] 创建 `docs/adr/README.md` 索引
- [x] 创建 `docs/adr/ADR-001-namespace-convergence.md`（4 个备选方案对比）
- [x] 新建 `src/core/namespace.h`（约定锚点 + 迁移进度文档）
- [x] **实测引用面**：`components.h` 仅被 2 个文件引用；`Light` 零使用；
      22 个示例无一使用 ECS → 判定可不留别名直接迁完
- [x] `scene/components.h`：8 个组件迁入 `namespace sr`，**不留全局别名**
- [x] `tests/test_core.cpp` ECS 套件改用 `sr::` 限定
- [x] 新增**编译期哨兵**：在测试文件全局作用域声明 8 个同名类型。
      能编译通过本身就证明目标达成；若有人加回 `using sr::X;` 会直接报重定义
- [x] 新增用例 `ECS.globalCanaryTypesAreDistinctFromSrComponents`（`std::is_same` 断言）
- [x] 验证：全局类型数 **60 → 52**；构建零警告；单元测试 120 → **121 全绿**；
      像素回归 3 场景仍匹配基准图；25 个 exe 与基线一致
- [x] 同步 `docs/architecture.md` §5、`docs/modules.md`、`docs/changelog.md`

### 后续批次（待做）

按 ADR-001，先用同样方法**实测每个类型的引用面**，再决定是否需要别名桥：

- [ ] 第 2 批：`skeleton.h`（`Animation`/`Keyframe`/`Bone`/`Skeleton`/
      `VertexBoneWeights`/`AnimationChannel`）、`texture.h`（`Texture`）、
      `mesh.h`（`Mesh`/`Vertex`）、`camera.h`（`Camera`/`Camera2D`）
- [ ] 第 3 批：`color.h`（`Color`）、`math/`（`Vec2/3/4`、`Mat3`、`Mat4`、`Quat`、`Rect2`）
- [ ] 第 4 批：`shader/`（8 个）、`core/` 其余、`raster/`
- [ ] 全局枚举 `ClipPlane`（已改 enum class，但仍在全局）、`BlendMode`
- [ ] 全局自由函数 `blendPixels`、`interpolateVaryings`、`drawLine`、`fillTriangle` 等
- [ ] 全部完成后移除所有别名，版本升至 2.0.0
- [ ] 更新 `README.md` 的集成示例代码

### 扫描时的陷阱（第 1 批实测经验）

1. **`Select-String` 默认大小写不敏感**：搜 `Light` 会命中小写局部变量 `light`。
   必须加 `-CaseSensitive`。
2. **嵌套枚举项会冒充全局类型**：`GltfAnimation::Channel::Path::Rotation` 与
   全局 `struct Rotation` 同名但完全无关。初扫时曾因此高估迁移成本。
3. **按行匹配 `^(class|struct)` 会误计**：部分头文件的 namespace 体不缩进
   （`NamespaceIndentation: None`），导致 namespace 内部的类型也被当成全局。
   正确做法是对整个文件文本用 `IndexOf("\nnamespace ")` 定位首个 namespace，
   只统计其**之前**的声明。初版的 65 这个错误数字就是这么来的，实际是 60。

## 风险

- **改动面覆盖全仓，任何遗漏都导致编译失败** → 缓解：编译器会精确报错（安全失败）；
  每模块单独 commit，出问题可精确 revert
- **`examples/` 中已有全局同名符号与迁移后的 `using` 别名二次冲突** → 缓解：
  阶段二每步都全量构建 22 个示例，冲突立即暴露；这正是 `sr2d` 当年遇到的情况
- **与并行任务冲突** → 缓解：本任务**必须最后执行**，且执行期间冻结其他任务
- **中途放弃导致半迁移状态** → 缓解：阶段一的别名桥保证「迁移到一半」时
  代码仍然完全可用，不存在必须一次做完的压力

## 关联文档

- `docs/architecture.md` §5「命名空间现状」
- `docs/modules.md`
- `docs/adr/ADR-001-namespace-convergence.md`（待创建）
- `.qoder/rules/cpp_project_rules.md`「命名空间规范」「ABI / API 兼容性规范」
- `.qoder/rules/architecture_decision_record.md`

## 变更日志

| 日期 | 变更 |
|------|------|
| 2026-09-05 | 创建任务；实测扫描确认 60 个全局类型 / 30 个头文件，完成迁移优先级排序 |
| 2026-09-07 | 立 ADR-001，推翻原「全量别名桥」方案，改为按引用面分类（方案 D） |
| 2026-09-07 | 完成第 1 批：8 个 ECS 组件迁入 `sr` 且不留别名；全局类型 60 → 52；
              新增编译期哨兵 + 1 个用例；121 测试全绿 |
