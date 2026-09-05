# AI 开发工作流规则体系

本目录是一套完整的 AI 开发工作流规范，用于约束 AI Agent（及开发者）先分析、再设计、后编码，并保证代码、文档、任务、架构决策同步可追溯。

---

## 文件组成（共 9 份）

| 文件 | 一句话职责 | 加载方式 |
|------|-----------|----------|
| `engineering_rules.md` | 工程总纲（入口）：架构约定 / 通用约定 / 红线约束 / 命令 | 常驻（always_on） |
| `ai_workflow_rules.md` | 七步工作流 + S/M/L 变更分级 + 唯一自检清单 | 常驻 |
| `cpp_project_rules.md` | C++ 语言与工程细节 | 按需：glob（编辑 C++ 源文件 / CMake 时） |
| `testing_rules.md` | 测试怎么写、测什么场景 | 按需：model_decision |
| `git_rules.md` | Commit 格式、分支策略、PR 规范 | 按需：model_decision |
| `project_structure_rules.md` | 目录划分与文件命名 | 按需：model_decision |
| `task_management_rules.md` | 任务建档与状态流转 | 按需：model_decision |
| `architecture_decision_record.md` | 重要设计决策的 ADR 记录 | 按需：model_decision |
| `documentation_rules.md` | 文档编写与同步 | 按需：model_decision |

---

## 冲突裁决优先级

规则冲突时，越接近具体项目的规则优先级越高：

```
项目专属规则 > 语言规则（cpp_project_rules.md）
            > 工作流与领域规范（工作流 / 测试 / Git / 文档 / 目录 / 任务 / ADR）
            > 工程总则（engineering_rules.md）
```

同一文件内的权衡原则：**可维护性 > 可扩展性 > 性能 > 开发速度**。

---

## 核心工作流

任何需求按 `ai_workflow_rules.md` 执行，流程深度由变更级别决定：

| 级别 | 判定 | 流程要求 |
|------|------|----------|
| S | 单文件、< 30 行、不涉公共接口 | 简述意图 → 编码 → 简化自检 |
| M | 单模块、< 300 行 | Step 1、2、5、6、7 |
| L | 跨模块 / 公共接口 / 引入新依赖 | 完整七步 + 双方案比较 + 按需 ADR |

完整七步：需求分析 → 影响范围 → 设计方案 → 文档计划 → 编码实现 → 自检 → 最终确认。

---

## 使用方式

1. 将本目录全部文件复制到目标项目的 `.qoder/rules/` 目录。
2. 仅总纲与工作流两份常驻；其余按需加载（glob / model_decision）。
3. **必须**先填写 `engineering_rules.md` 中的「红线约束」与「命令」两个模板位（见下节）。
4. 项目可按自身特点新增项目专属规则（优先级最高），并在 `engineering_rules.md` 索引中登记。
5. 项目初始化时按 `project_structure_rules.md` 建立目录，预置 `docs/` 与 `docs/tasks/`。
6. Monorepo 项目可按子目录配置嵌套规则：子目录规则覆盖顶层（就近优先，AGENTS.md 标准做法）。

---

## 目标项目必须填写：红线与命令（最高价值内容）

template 位于 `engineering_rules.md`，复制后替换为项目真实内容：

**红线约束**（每条针对一个代价高昂的错误，而非抽象风格要求）：

* 禁止修改 `xxx/migrations/` 下的历史文件，只能新增
* 禁止变更已发布的公共头接口签名（走废弃流程）
* 禁止提交 `.env` 或硬编码密钥

**命令**（构建 / 测试 / 覆盖率 / 格式化 / 静态检查），示例：

```
构建：cmake -B build && cmake --build build
测试：ctest --test-dir build
格式化：clang-format -i src/**/*.cpp
静态检查：clang-tidy
```

---

## 规则维护

* 规则文件纳入版本控制，与代码同等评审；建议为规则本身记录变更（changelog）。
* 新增规则的原则：只写「删掉它，AI 会犯光靠读代码无法自行恢复的错误」的内容；优先「默认 + 例外」而非绝对化条款。
* 规则与现实冲突时，先修规则，再执行。
* 常驻内容总量建议控制在 500 行以内；超出时把任务型流程移入按需规则。

---

## 跨工具兼容

本目录为 Qoder 规则格式（frontmatter 触发）。若项目同时使用其他 AI 编码工具：

* 可把工程总纲的浓缩版（架构 + 红线 + 命令）导出为 `AGENTS.md`（跨工具通用入口）或 `CLAUDE.md`
* 无论哪种格式，常驻内容建议 ≤ 100 行，其余按需加载

---

## 配套文档结构（在目标项目内建立）

```
docs/
├── architecture.md          # 系统架构
├── modules.md               # 模块职责
├── api.md / protocol.md     # 接口与协议
├── changelog.md             # 变更日志
├── adr/                     # 架构决策记录（见 architecture_decision_record.md）
└── tasks/                   # 任务跟踪（见 task_management_rules.md）
```
