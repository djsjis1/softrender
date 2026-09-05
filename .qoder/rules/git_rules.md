---
trigger: model_decision
description: 任务涉及提交代码、撰写 Commit 信息、分支管理、PR/MR 或打 Tag 时加载本规范
---
# Git 工作流规范

## 本文件定位

本文件定义 Commit 格式、分支策略与 PR 规范。

与 `documentation_rules.md` 对齐：Commit 信息统一使用中文。

---

# Commit 格式

使用 `<type>(<scope>): <subject>` 格式：

```
feat(timeline): 新增移动操作的撤销重做支持

- 实现 MoveCommand 的 execute/undo
- 撤销栈深度限制为 kMaxUndoDepth

Refs: TASK-001, ADR-001
```

## type 取值

| type | 含义 |
|------|------|
| feat | 新功能 |
| fix | Bug 修复 |
| refactor | 重构（既不新增功能也不修复） |
| perf | 性能优化 |
| docs | 仅文档变更 |
| test | 仅测试变更 |
| chore | 构建 / 脚本 / 依赖维护 |
| revert | 回滚 |

## 规则

* subject 使用中文，不超过 50 字，使用祈使句
* 一次提交只做一件事（原子性），禁止混合多个职责
* 涉及 TASK 或 ADR 的提交，必须在 footer 用 `Refs: TASK-XXX, ADR-XXX` 关联
* 禁止提交 `WIP`、`temp`、`fix bug` 等无意义信息
* 禁止提交构建产物、临时文件（按 `project_structure_rules.md` 的 .gitignore 规范）

---

# 分支策略

| 分支 | 用途 | 规则 |
|------|------|------|
| `main` | 发布版本 | 禁止直接 push，只接受 release 合入 |
| `develop` | 开发主干 | 日常集成分支 |
| `feature/TASK-XXX-描述` | 新功能 | 从 develop 拉出，完成后合回 develop |
| `fix/TASK-XXX-描述` | 缺陷修复 | 同上 |
| `release/vX.Y.Z` | 发布准备 | 从 develop 拉出，只允许修复不允许新功能 |

功能分支命名必须包含 TASK 编号，保证可追溯。

---

# PR / MR 规范

合入 `develop` / `main` 必须通过 PR，且满足：

* 标题符合 Commit 格式
* 描述包含：动机、变更点、测试情况、关联的 TASK / ADR
* CI 全绿（格式、静态分析、编译、测试、Sanitizer，见 `cpp_project_rules.md`）
* Review 人数：合并前至少 1 人；涉及公共接口变更、并发相关、安全敏感（输入解析 / 权限控制）、性能关键路径的变更至少 2 人
* 禁止自己批准合入自己的 PR，禁止带红灯合并

---

# Tag 与版本

* 使用语义化版本：`vX.Y.Z`
* 主版本 X：API / ABI 不兼容变更
* 次版本 Y：向后兼容的功能新增
* 修订号 Z：向后兼容的缺陷修复
* 每个 Tag 必须附发布说明（变更摘要 + 影响范围）

---

# AI 提交规则

* 每次代码修改完成后，AI 自动执行 `git add` + `git commit`
* Commit 信息必须符合本文件全部格式要求
* 未经用户同意，AI 不得改写历史（rebase、reset、force push）

## 自动分支策略

AI 在 commit 前自动判断是否需要创建分支：

| 条件 | 操作 |
|------|------|
| S 级变更（单文件、< 30 行） | 直接在当前分支 commit |
| M/L 级变更（多文件、≥ 30 行、跨模块） | 自动创建 `feature/TASK-XXX-描述` 或 `fix/TASK-XXX-描述` 分支后 commit |
| 已在 feature/fix 分支上 | 直接 commit，不重复创建 |

分支命名规范不变：`feature/TASK-XXX-描述` 或 `fix/TASK-XXX-描述`，无 TASK 编号时用简短描述命名。

## 用户信息

```
user.name = xiefujun
user.email = 853240265@qq.com
```

