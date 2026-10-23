# ADR-001：全局命名空间收敛 —— 按引用面分类迁移到 `namespace sr`

## 背景

SoftRenderer 是 **header-only 库**，`CMakeLists.txt` 提供 `SoftRenderer::softrenderer`
别名与 `find_package(SoftRenderer)` 支持，「可作为纯头文件库集成使用」是项目明确卖点。

header-only 意味着：本项目头文件里的每个符号都会**直接进入使用者的编译单元**。

实测扫描（正则匹配 `src/` 下所有头文件中位于全局作用域的 `class` / `struct` 声明）：

> **60 个顶层类型位于全局命名空间，分布在 30 个头文件。**

其中包含大量领域通用名：

| 类别 | 全局类型 |
|---|---|
| ECS 组件 | `Position`、`Velocity`、`Rotation`、`Scale`、`MeshRef`、`MaterialRef`、`Light`、`CameraTag` |
| 数学 | `Vec2`/`Vec3`/`Vec4`、`Mat3`、`Mat4`、`Quat`、`Rect2` |
| 渲染目标 | `Framebuffer`、`DepthBuffer`、`Color`、`FloatFramebuffer`、`MSAAFramebuffer`、`GBuffer` |
| 场景 | `Mesh`、`Vertex`、`Texture`、`Cubemap`、`Camera`、`Camera2D`、`Animation`、`Keyframe`、`Bone`、`Skeleton`、`Particle`、`ParticleSystem` |
| 着色 | `Shader`、`Varyings`、`FlatShader`、`GouraudShader`、`PhongShader`、`PBRShader`、`DepthShader`、`FinalShader` |
| 其他 | `Renderer`、`Renderer2D`、`ClipVertex`、`RenderQueue`、`DrawItem`、`CommandBuffer`、`ThreadPool`、`Profiler`、`SSAO`、`Gltf*`（6 个）等 |

这违反 `cpp_project_rules.md`「命名空间规范」：**禁止全局命名空间定义业务符号**。

## 问题

`Position`、`Scale`、`Rotation`、`Light`、`Animation`、`Texture`、`Mesh`、`Vertex`、
`Color`、`Camera` 这些名字，下游使用者几乎必然已在自己的代码里定义过。
任何人 `#include <softrenderer/scene/components.h>` 都会撞上重定义错误——而这 8 个 ECS 组件
恰恰是本项目 ECS 能力的入口。

### 这不是理论风险，项目内部已经踩过

`namespace draw2d` 的引入原因，就是 2D 图元函数名（`fillRect` / `fillCircle` /
`SpritePalette`）与 `examples/` 中已有的全局同名符号发生了**真实冲突**。
同一个仓库内部尚且如此，跨项目集成只会更严重。

### 约束

不能简单地「全部搬进 `namespace sr`」：60 个类型被 22 个示例、1300+ 行测试、
以及 `src/` 内部大量交叉引用使用。一次性迁移会：

1. 产生一个触及全仓、无法 review 的巨型 commit
2. 立刻破坏所有下游代码（API Break）
3. 出问题时无法二分定位

## 备选方案

### 方案 A：一次性全量迁移，不留别名

把 60 个类型全部搬进 `namespace sr`，同步改所有引用点，版本升 2.0.0。

- 优点：一步到位，不留过渡态
- 缺点：单个 commit 触及全仓（52 个头文件 + 22 个示例 + 1300 行测试）；
  无法 review；无法二分定位回归；中途任何编译错误都会阻塞全部工作

### 方案 B：全量别名桥渐进迁移（TASK-007 原方案）

先把类型搬进 `namespace sr`，同时在全局作用域保留 `using sr::Framebuffer;` 等别名；
逐模块删除别名；最后统一移除并升 2.0.0。

- 优点：任意时刻代码都可编译，可分阶段提交、可回滚
- 缺点：**别名桥本身不解决撞名问题**。`using sr::Position;` 仍然把 `Position`
  注入全局命名空间，下游照样撞名。也就是说阶段一、二的收益是零，
  全部价值都压在最后的阶段三——而阶段三仍是一次触及全仓的大改。
  等于用三个阶段的工作量，换来和方案 A 一样的最终状态。

### 方案 C：不迁移，只在文档中告知使用者注意冲突

- 优点：零成本
- 缺点：header-only 库的重定义是**编译期硬错误**，使用者无法绕过（不像运行时冲突
  可以规避）。文档告知不构成解决方案。且 `draw2d` 的历史已证明冲突会真实发生。

### 方案 D：按引用面大小分类处理（选定）

先用实测确定每个类型的**真实引用面**，再分类：

- **引用面小的**（只在少数文件出现）→ **直接完整迁移，不留别名**。
  一次改完，立即获得真实的撞名消除，且改动量可控、可验证。
- **引用面大的**（`Color`、`Vec3f`、`Framebuffer`、`Renderer` 等遍布全仓）→
  建立 `namespace sr` 与别名桥基础设施，**按模块逐个迁移**，
  每个模块单独 commit、单独验证，最后统一移除别名并升 2.0.0。

## 最终方案

**方案 D：按引用面分类迁移。**

## 选择原因

1. **方案 B 的别名桥对高冲突名字毫无收益**。`using sr::Position;` 依然把 `Position`
   放进全局命名空间。既然别名桥解决不了最危险的那批名字，就不该对它们用别名桥。

2. **实测证明最高危的一批名字引用面极小**。ECS 的 8 个组件（`Position` / `Velocity` /
   `Rotation` / `Scale` / `MeshRef` / `MaterialRef` / `Light` / `CameraTag`）：
   - `scene/components.h` 被**恰好 2 个文件**引用：其自身与 `tests/test_core.cpp`
   - `Light` 除定义处外**全仓零使用**
   - 22 个示例**没有一个**使用 ECS

   也就是说，撞名风险最高的 8 个类型，可以**立刻完整迁移、不留别名**，
   改动面只有 2 个文件。这是方案 A/B 都没有识别出的机会。

3. **`GltfAnimation::Channel::Path::Rotation` 之类的嵌套枚举不构成冲突**，
   扫描时必须区分「全局 `Rotation` 结构体」与「嵌套枚举项 `Path::Rotation`」，
   否则会高估迁移成本。（初次扫描时这两者曾被混淆。）

4. **大引用面的类型确实需要过渡态**，但过渡态的价值在于「让改动可以分批」，
   而不是「让下游无感」——后者做不到。因此对它们用别名桥是正确的，
   只要明确它是**分批工具**而非**兼容承诺**。

5. **可验证性**：每一批迁移后都能跑全量测试（当前 120 单元 + 3 回归）与
   25 个 exe 基线比对，出问题能精确定位到某一批。

## 影响范围

### 立即执行（本次）

* 新增 `docs/adr/`（本 ADR + 索引）
* 新增 `include/softrenderer/core/namespace.h`：声明 `namespace sr` 为所有 SoftRenderer 公共符号的
  目标命名空间，并写明迁移约定与进度
* `include/softrenderer/scene/components.h`：8 个 ECS 组件迁入 `namespace sr`，**不留全局别名**
* `tests/test_core.cpp`：ECS 套件改用 `sr::` 限定
* 公共接口变更：`Position` 等 8 个名字从全局作用域消失 → **API Break**

### 后续分批（未执行）

* 按引用面从小到大迁移其余 52 个类型
* 每批：改定义 → 加/删别名 → 全量构建 → 跑测试 → 单独 commit
* 全部完成后移除所有别名，版本 `1.0.0` → `2.0.0`
  （`CMakeLists.txt` 与 `cmake/version.cmake` 同步）

### 关于版本号的处理

`cpp_project_rules.md`「ABI / API 兼容性规范」：禁止在同一大版本内删除公共接口。

本次移除 8 个 ECS 组件的全局名已构成 API Break。**决策：暂不升 2.0.0**，
理由：项目从未发布过任何 tag、无外部使用者、`CMakeLists.txt` 的 `VERSION 1.0.0`
仅用于生成版本头。待全部 60 个类型迁移完成、真正对外发布时一次性升 2.0.0，
避免为每次分批迁移反复改版本号。此决定在 `docs/changelog.md` 记录为不兼容变更。

## 失效条件

* 若本项目改为**编译型静态库 / 动态库**（符号不再进入使用者编译单元），
  命名冲突风险大幅降低，应重新评估是否继续迁移
* 若确定本项目**永不对外集成**（仅作教学示例集合），方案 C（不迁移）变为可接受，
  本 ADR 应标记为「已替代」

## 状态

已采纳（2026-09-07）—— 分阶段执行中，本次完成 ECS 组件批次
