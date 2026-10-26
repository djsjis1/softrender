# 架构决策记录索引

> 规范见 `.qoder/rules/architecture_decision_record.md`。
> **不可修改原则**：已采纳的 ADR 不得事后修改。若决策变化，建新 ADR 并把旧状态标为「已替代（见 ADR-XXX）」。禁止删除历史 ADR。

| 编号 | 日期 | 标题 | 状态 |
|------|------|------|------|
| [ADR-001](ADR-001-namespace-convergence.md) | 2026-09-07 | 全局命名空间收敛：按引用面分类迁移到 `namespace sr` | 已采纳（已完成） |
| [ADR-002](ADR-002-data-and-task-errors.md) | 2026-10-05 | 输入校验与异步任务错误边界 | 已采纳 |
| [ADR-003](ADR-003-raster-coverage.md) | 2026-10-06 | 确定性子像素覆盖与逐样本提交 | 已采纳 |
| [ADR-004](ADR-004-module-boundaries.md) | 2026-10-23 | 模块边界、公共 include 与可选组件（2.0 兼容性） | 已采纳（已完成） |
| [ADR-005](ADR-005-pixel-formats.md) | 2026-10-25 | 存储像素格式与运算域分离 | 已采纳 |
| [ADR-006](ADR-006-rasterizer-fast-positioning.md) | 2026-10-26 | `rasterizer_fast.h` 定位为验证工具，不接入主渲染链路 | 已采纳 |
| [ADR-007](ADR-007-baremetal-freestanding.md) | 2026-10-26 | 裸机（freestanding）支持策略 | 已采纳 |

## 待立 ADR 的已知决策点

以下决策已在任务档案或代码注释中做出但尚未立 ADR，触及时应补：

| 决策 | 当前位置 | 为何需要 ADR |
|---|---|---|
| 第三方依赖采用源码内嵌 `third/` 而非 FetchContent / 包管理器 | `third/README.md` | 属「选择第三方库」场景 |
| `runtime/log.h` 保留双模式（spdlog + 内置）而非统一依赖 spdlog | `architecture.md` §6.2 | 属架构分层与依赖取舍 |
| 实时窗口后端只做 Win32，不做跨平台抽象层 | `architecture.md` §8 | 属「跨平台兼容性决策」场景 |
