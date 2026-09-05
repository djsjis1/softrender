# 任务索引

> 格式见 `.qoder/rules/task_management_rules.md`。
> 已完成任务移入 `DONE/` 并更新本索引。

| 编号 | 状态 | 标题 | 涉及模块 | 创建日期 |
|------|------|------|----------|----------|
| TASK-001 | 进行中 | 工程基线建设（版本控制 / 文档骨架 / 规则落地） | 全仓、docs、.qoder/rules | 2026-09-05 |
| TASK-002 | **已完成** | 光栅化器缺陷修复（深度插值权重错位 + 顺时针绕序） | raster、tests | 2026-09-05 |
| TASK-003 | **已完成** | 外部输入校验与边界安全（image_loader UB / 越界读 / 混合精度 / 日志模板化） | scene、core、tests | 2026-09-05 |
| TASK-004 | 代码完成 / 待远端验收 | CI 跨平台修复与测试基础设施 | CMake、.github、tests | 2026-09-05 |
| TASK-005 | 待开始 | 构建系统现代化（target-based / Sanitizer / 静态检查 / 格式化） | CMake、全仓 | 2026-09-05 |
| TASK-006 | 待开始 | 测试目录重构与覆盖率接入 | tests、CMake、.github | 2026-09-05 |
| TASK-007 | 待开始 | 全局命名空间收敛（架构级，需 ADR） | 全部 src/、examples、tests | 2026-09-05 |

## 依赖关系

```
TASK-001 ──┬─→ TASK-004 ─→ TASK-005 ─→ TASK-006 ─┐
           ├─→ TASK-002 ✓                         ├─→ TASK-007
           └─→ TASK-003 ✓                         │
                                                   ┘
```

- **TASK-001 是所有任务的前置**：没有版本控制就无法安全地做 TASK-005 的大范围
  CMake 改造，也无法做 TASK-007 的分阶段重构
- TASK-002 / TASK-003 相互独立，可并行
- TASK-007 必须最后做：它触及每一个源文件，与任何并行任务都会冲突

## 已完成任务

- **TASK-004** — CI 跨平台修复与测试基础设施 — 2026-09-05 **代码完成**
  修复 ubuntu/macos job 必然编译失败（三个 realtime 目标加 `if(WIN32)` 守卫）；
  去掉 smoke test 吞失败的 `|| true`；接线像素回归并修复两个「永远假通过」陷阱
  （相对路径 + 自动创建基准），已做反向验证；ctest 39.89 秒 → 14.06 秒。
  单元测试 113 → 120，新增 `render_regression`（3 场景）。
  **未完项**：推送远端验收、删除 `test_framework.h`（环境阻断删除工具）。
  档案：[`TASK-004-CI跨平台与测试基础设施.md`](TASK-004-CI跨平台与测试基础设施.md)

- **TASK-003** — 外部输入校验与边界安全 — 2026-09-05 完成
  修 `image_loader.h` 有符号整数溢出 UB + 补尺寸校验与 RAII 释放；
  `Framebuffer::getPixel` / `DepthBuffer::get` 越界返回哨兵值（消除 Release 下越界读 UB）；
  `blend.h` 截断改四舍五入；5 个枚举合规（含 `ClipPlane` 裸 enum → enum class）。
  **实施中新发现并修复 `core/log.h` 内置模式的 4 个缺陷**（非 POD 过 varargs 的 UB、
  `{}` 占位符从不替换、字面 `%` 的 UB、`levelName` 数组漏 `Off` 导致越界读），
  改为变参模板 + 可测试的 `sr::formatLog`。
  单元测试 97 → 113，全量构建零警告。
  档案：[`DONE/TASK-003-输入校验与边界安全.md`](DONE/TASK-003-输入校验与边界安全.md)

- **TASK-002** — 光栅化器缺陷修复 — 2026-09-05 完成
  修正 `rasterizer_fast.h` 深度插值重心权重错位（实测误差达 0.325）
  与顺时针三角形静默丢弃；新增 3 个回归测试，单元测试 94 → 97。
  档案：[`DONE/TASK-002-光栅化器缺陷修复.md`](DONE/TASK-002-光栅化器缺陷修复.md)
