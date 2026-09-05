# TASK-007：全局命名空间收敛

## 基本信息

- 创建日期：2026-09-05
- 状态：待开始
- 涉及模块：**全部** `src/`（30 个头文件）、`examples/`（22 个）、`tests/`、`practice/`
- 变更级别：**L**（架构级 + 破坏公共接口 + 必须建 ADR）
- 前置依赖：TASK-001、TASK-005（格式化完成）、TASK-006（测试拆分完成）
- **必须最后执行**：本任务触及每一个源文件，与任何并行任务都会冲突

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

- [ ] 创建 `docs/adr/ADR-001-namespace-convergence.md`
- [ ] 新建 `src/core/namespace.h`
- [ ] 阶段二·优先级 1：迁移 `scene/components.h`（8 个 ECS 组件）+ 单独 commit
- [ ] 阶段二·优先级 2：迁移 `skeleton.h` / `texture.h` / `mesh.h` / `camera.h` + 单独 commit
- [ ] 阶段二·优先级 3：迁移 `color.h` / `math/` 全部 + 单独 commit
- [ ] 阶段二·优先级 4：迁移 `shader/` / `core/` 其余 / `raster/` + 单独 commit
- [ ] 迁移全局枚举 `ClipPlane`、`BlendMode`
- [ ] 迁移全局自由函数 `blendPixels`、`interpolateVaryings`、`drawLine` 等
- [ ] 阶段三：移除全部全局 `using` 别名
- [ ] 版本号升至 2.0.0（`CMakeLists.txt` + `cmake/version.cmake`）
- [ ] 更新 `docs/architecture.md` §5「命名空间现状」
- [ ] 更新 `docs/modules.md` 各模块的类型表
- [ ] 更新 `README.md` 的集成示例代码
- [ ] `docs/changelog.md` 记录不兼容变更与迁移指引

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
