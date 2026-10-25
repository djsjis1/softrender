# 任务索引

> 状态只在 [ACTIVE.md](ACTIVE.md) 维护，本页只做档案导航，避免两处互相矛盾。
> 建档与流转规范见 `.qoder/rules/task_management_rules.md`。

## 未结案档案

| 编号 | 标题 |
| --- | --- |
| [TASK-001](TASK-001-工程基线建设.md) | 工程基线建设 |
| [TASK-004](TASK-004-CI跨平台与测试基础设施.md) | CI 跨平台与测试基础设施 |
| [TASK-005](TASK-005-构建系统现代化.md) | 构建系统现代化 |
| [TASK-006](TASK-006-测试目录重构与覆盖率.md) | 测试目录重构与覆盖率 |
| [TASK-007](TASK-007-全局命名空间收敛.md) | 全局命名空间收敛 |
| [TASK-014](TASK-014-分层解耦重构.md) | 分层解耦重构（父任务） |
| [TASK-015](TASK-015-基础层与公共入口.md) | 基础层与公共入口 |
| [TASK-016](TASK-016-渲染职责拆分.md) | 渲染职责拆分 |
| [TASK-017](TASK-017-场景与加载解耦.md) | 场景与加载解耦 |
| [TASK-018](TASK-018-组件发布与验收.md) | 组件发布与验收 |

## 已结案归档（DONE/）

| 编号 | 标题 |
| --- | --- |
| [TASK-002](DONE/TASK-002-光栅化器缺陷修复.md) | 快速光栅化深度权重与顺时针修复 |
| [TASK-003](DONE/TASK-003-输入校验与边界安全.md) | 外部输入校验与边界安全 |
| [TASK-008](DONE/TASK-008-library-integration.md) | 构建与安装集成完善 |
| [TASK-009](DONE/TASK-009-render-discard.md) | 丢弃片元的深度写入 |
| [TASK-010](DONE/TASK-010-arena-safety.md) | Arena 边界安全与跨帧复用 |
| [TASK-011](DONE/TASK-011-gltf-validation.md) | glTF 数据读取与动画采样校验 |
| [TASK-012](DONE/TASK-012-thread-failures.md) | 线程任务异常与生命周期 |
| [TASK-013](DONE/TASK-013-raster-coverage.md) | 覆盖规则、MSAA 提交与渲染基线 |

## 约定

- 结案不搬文件：档案保留原路径以免破坏外部链接，只有确认无后续引用的才移入 `DONE/`。
- 档案里的分析与实测数字是当时的快照；当前数值一律以[项目上下文](../project_context.md)为准。
- 一个任务跨多个阶段时只在父任务档案维护进度，子任务档案不重复记录计数。

## 备注

- `project_review.md` 已在 2.0 重构时移除；`DONE/TASK-008~012` 正文里的同名引用是历史残留，对应内容已并入 [project_context](../project_context.md) 与 [architecture](../architecture.md)。
- TASK-009 / TASK-013 的档案曾与 HEAD 代码不符（其修复未进入 `src/raster/renderer.h` 的 git 历史），
  阶段 1 已按实测重新落地，详见 [TASK-014](TASK-014-分层解耦重构.md)。
- TASK-007 的第 3~5 批由 TASK-015 承接，进度只在 TASK-015 维护。
