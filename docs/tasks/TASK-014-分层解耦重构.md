# TASK-014：分层解耦重构（父任务）

- 创建日期：2026-10-23
- 状态：进行中
- 涉及模块：`include/`、`src/`、`cmake/`、`tests/`、`examples/`、`practice/`、`docs/`
- 变更级别：L（跨模块 / 公共接口 / 架构级）
- 设计决策：[ADR-004](../adr/ADR-004-module-boundaries.md)

## 目标

让目录、头文件依赖与构建目标三者的边界一致，使「找缓冲只进 core、找绘制只进 render、
找资源加载只进 io」，并让下游按需取用组件：使用核心渲染无需同时接入文件解析、线程或窗口。

## 重构前基线（2026-10-23 实测，develop @ 2adfbf6）

### 全量构建：失败（3 个示例目标）

| 目标 | 错误 | 根因 |
|---|---|---|
| `stage01_framebuffer` | `drawLine` 未找到 ×6 | 2D 图元已迁入 `namespace draw2d`，示例仍用未限定全局名 |
| `stage03_sprite_anim` | `fillRect` 未找到 ×1；C4244 ×2 被 `/WX` 视为错误 | 同上；`fillCircle` 的 float 形参收到 int 实参 |
| `stage05_transform` | `drawLine` 未找到 ×1 | 同上 |

### 测试基线

| 目标 | 结果 |
|---|---|
| `unit_tests` | 169 用例 / 43 套件：**166 通过 / 3 失败** |
| `render_regression` | 3 / 3 通过（基准图未变动） |

3 项失败：`RendererDiscard.drawMesh_discardAll_preservesClearDepthAndColor`、
`RendererDiscard.drawMesh_partialDiscard_backgroundVisibleInEitherOrder`、
`RasterCoverage.drawMesh_clippedQuad_hasNoInternalCrackOrDoubleHit`。

### 目标清单

Windows 上 25 个 exe：16 个 `stage*`（含 3 个 `_live` 实时窗口）+ 6 个 `demo_*` +
`practice_hello_2d` + `unit_tests` + `render_regression`。离屏程序 20 个。

### 与文档的偏差（已确认）

`src/raster/renderer.h` 在 git 历史中只有初始化提交与一次 include 补齐，
**从未包含 TASK-009（discard 深度提交）与 TASK-013（Renderer 共用覆盖规则）的修复**。
即 `docs/changelog.md`、`ACTIVE.md` 宣称的「169 用例全绿」「Renderer 与基础填充共用覆盖规则」
与 HEAD 实际状态不符。判定以实测为准，不以文档为准。

## 阶段 1 修复记录（用户确认「先修复再重构」）

| 修复 | 内容 |
|---|---|
| 覆盖规则统一 | `Renderer::rasterTriangle` 改用 `sr::TriangleCoverage` 的整数 top-left 规则，替代浮点边函数 + `>=0` 判定；属性插值仍用浮点重心坐标 |
| 深度提交顺序 | 深度改在 `shader.fragment` 返回 `true` 之后提交，被 discard 的片元不再占据深度 |
| 示例调用点 | 补齐 `draw2d::` 限定与 int→float 显式转换 |

修复后实测：全量构建 **0 错误 0 警告**；`unit_tests` **169/169**；
`render_regression` **3/3**；20 个离屏程序退出码全为 0。基准图未更新。

## 进度清单

- [x] 阶段 1：基线与边界（构建/测试/目标清单实测、失败项定位与修复、任务与 ADR 建档）
- [ ] 阶段 2：基础层与公共入口（[TASK-015](TASK-015-基础层与公共入口.md)）
- [ ] 阶段 3：渲染职责拆分（[TASK-016](TASK-016-渲染职责拆分.md)）
- [ ] 阶段 4：场景与加载解耦（[TASK-017](TASK-017-场景与加载解耦.md)）
- [ ] 阶段 5：组件发布与验收（[TASK-018](TASK-018-组件发布与验收.md)）

## 关联任务

- [TASK-006](TASK-006-测试目录重构与覆盖率.md)：测试目录拆分与覆盖率。本任务负责把
  `tests/unit/test_core.cpp` 按模块拆分，并让公共命名空间哨兵独立编译，两者合并执行。
- [TASK-007](TASK-007-全局命名空间收敛.md)：全局命名空间收敛。本任务的阶段 2 承接其第 3~5 批，
  进度只在 TASK-015 维护，避免重复记账。

## 关联文档

- [ADR-004](../adr/ADR-004-module-boundaries.md)：模块边界与 2.0 兼容性决策
- [architecture.md](../architecture.md)、[modules.md](../modules.md)：分层与依赖方向
- [changelog.md](../changelog.md)：阶段 1 修复与后续各批次登记

## 风险

| 风险 | 说明 |
|---|---|
| API Break 面大 | 头文件路径与命名空间一次性调整，仓库内全部调用点需同步 |
| 热路径改动 | 覆盖规则改动已通过像素回归验证；后续批次不得再改渲染结果 |
| 文档与代码不同步 | 本轮已发现文档宣称的修复不在 HEAD；每批次必须实测后再写文档 |

## 变更日志

| 日期 | 变更 |
|---|---|
| 2026-10-23 | 创建父任务；记录重构前基线；完成阶段 1 三项既有缺陷修复；ADR-004 立项 |
| 2026-10-23 | 阶段 2 第 1 批：建立 `include/softrenderer/` 公共入口，搬迁 23 个头文件；构建/单测/像素回归/示例全部通过 |
| 2026-10-23 | 阶段 2 第 2 批：31 个头文件落到最终目录；`src/` 不再有公共头；`Image`、`FloatFramebuffer` 拆分完成 |
| 2026-10-23 | 阶段 3：`Varyings`/`Shader`/`SurfaceShader`、`barycentric`、`wireTriangle`、`Camera2D`、生成器、`Renderer` 去宿主计时；功能命名空间收敛到 `sr::*` |
| 2026-10-23 | 阶段 4：`SceneAsset`/`Material`/`SceneNode`/`Skin`/`AnimationClip`；动画加载期解码；I/O 私有化 + STATIC；`ResourceCache`；ThreadPool 拆分 |
| 2026-10-23 | 阶段 5（部分）：组件化 CMake + 选项 + 2.0.0 + 安装裁剪；逐个公共头编译、命名空间哨兵、边界白名单；consumer core-only |
| 2026-10-23 | 阶段 5（收尾）：全局类名全部迁入 `sr`（无别名桥）；`test_core.cpp` 拆成 10 个模块级文件；哨兵升级为全局声明 60 个同名类型后包含全部公共头；实测 179/179 + 3/3 + 20 示例 + packaging 3/3 |

## 当前未完成项

| 项 | 说明 | 归属 |
|---|---|---|
| 覆盖率实测 | CI 已把范围改为 `include/softrenderer` + `src`，但本机无 LLVM/gcov，未在本地跑 | TASK-018 |
| 远端平台结果 | Linux/macOS 需实际 CI 运行后才能声明 | TASK-004 / TASK-018 |
| 全仓 clang-format | 本机未安装 clang-format，仅能保证新代码风格一致 | TASK-005 |

已完成（本轮）：全局类名迁移（TASK-007）与 `test_core.cpp` 按模块拆分（TASK-006）。