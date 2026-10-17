# 架构决策记录索引

> 规范见 `.qoder/rules/architecture_decision_record.md`。
>
> **不可修改原则**：已采纳的 ADR 不得事后修改内容。若决策变化，创建新 ADR 并把旧
> ADR 状态标记为「已替代（见 ADR-XXX）」。禁止删除历史 ADR。

| 编号 | 日期 | 标题 | 状态 |
|------|------|------|------|
| ADR-001 | 2026-09-07 | 全局命名空间收敛：按引用面分类迁移到 `namespace sr` | 已采纳（分阶段执行中） |

| [ADR-002](ADR-002-data-and-task-errors.md) | 2026-10-05 | 输入校验与异步任务错误边界 | 已采纳 |

| [ADR-003](ADR-003-raster-coverage.md) | 2026-10-06 | 确定性子像素覆盖与逐样本提交 | 已采纳 |

## 待立 ADR 的已知决策点

以下决策已在任务档案中做出但尚未立 ADR，触及时应补：

| 决策 | 当前位置 | 为何需要 ADR |
|---|---|---|
| `raster_fast` 是否接入 `Renderer` 主渲染链路 | `docs/tasks/DONE/TASK-002` | 属架构级变更 + 性能关键路径 |
| 第三方依赖采用源码内嵌 `third/` 而非 FetchContent / 包管理器 | `third/README.md` | 属「选择第三方库」场景 |
| `core/log.h` 保留双模式（spdlog + 内置）而非统一依赖 spdlog | `docs/architecture.md` §6.2 | 属架构分层与依赖取舍 |
| 实时窗口后端只做 Win32，不做跨平台抽象层 | `docs/architecture.md` §8 | 属「跨平台兼容性决策」场景 |
