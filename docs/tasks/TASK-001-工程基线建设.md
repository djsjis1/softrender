# TASK-001：工程基线建设

## 基本信息

- 创建日期：2026-09-05
- 状态：进行中
- 涉及模块：全仓、`docs/`、`.qoder/rules/`、`third/`
- 变更级别：**L**（跨模块 + 影响全仓 + 建立工程基线）

## 目标

补齐项目缺失的工程基线，使 `.qoder/rules/` 定义的九份规范真正可执行。

当前状态是：规则文件写得很完备，但**版本控制、文档骨架、规则模板位全部缺失**，
导致 `ai_workflow_rules.md` 的 Step 0（开工前读 `docs/tasks/ACTIVE.md` 与
`docs/architecture.md`）根本无法执行。

## 当前状态

git 仓库已建立并完成首次导入；文档骨架已补齐；规则模板位待填写。

## 进度清单

- [x] 创建 `.gitignore`（覆盖 `project_structure_rules.md` 要求的五类）
- [x] 创建 `output/.gitkeep`（保证克隆后示例写图目录存在）
- [x] `git init -b develop` + 首次提交（891 文件，commit `e12b16e`）
- [x] 核对暂存内容无 `build/`（590 MB）与 `output/` 产物泄漏
- [x] 根 `README.md`（`documentation_rules.md` 要求的 6 项内容）
- [x] `docs/architecture.md`（含 Mermaid 分层图、时序图，实测依赖边）
- [x] `docs/modules.md`（6 个 `src/` 子模块 + `platform/` 职责与扩展点）
- [x] `docs/changelog.md`（含实测校正的数据漂移表 + 欠债登记）
- [x] `docs/troubleshooting.md`
- [x] `third/README.md`（8 个依赖的版本 / 许可证 / **源码实测证据**）
- [x] `docs/tasks/`（README 索引 + ACTIVE + 7 个 TASK 档案）
- [ ] 填写 `.qoder/rules/engineering_rules.md` 的「红线约束」与「命令」两节
- [ ] 校正 `docs/engineering/54-项目当前状态总览.md` 的数据漂移
- [ ] 删除空目录 `src/platform/`
- [ ] 处理 `.qoder/rules/1.md`（空的 always_on 规则文件，无意义命名）

## 实测发现的数据漂移

`docs/engineering/54-项目当前状态总览.md` 与实际严重不符，已在
`docs/changelog.md` 列出完整对照表。关键项：

| 项 | 文档原值 | 实测值 |
|---|---|---|
| GoogleTest | 1.15 | 1.18.0 |
| stb_image / stb_image_write | latest | 2.30 / 1.16 |
| `src/` 头文件数 | 35 | 52 |
| 示例程序数 | 20 | 22 |
| 单元测试 | 62 用例 / 22 套件 | 94 用例 / 29 套件 |
| C++ 标准 | 「纯 C++17」 | C++23 |
| 「待做方向」中的 IBL 反射 / glTF 纹理 / 骨骼动画播放 | 列为待做 | **已完成**（`docs/advanced/55,56,57` 已存在对应文档） |

## 关键决策

1. **`output/` 用 `output/*` + `!output/.gitkeep`，而非 `output/`**
   git 无法重新包含被排除目录内的文件，写 `output/` 会让 negation 失效。
   且所有示例以相对路径写图，目录必须存在否则 `fopen` 静默失败。

2. **首次提交不带 `Refs:` footer**
   `git_rules.md` 要求涉及 TASK 的提交关联编号，但 TASK 档案本身在本次提交中
   才创建，此时引用会指向不存在的文件。从下一次提交起正常关联。

3. **不删除 `build_log.txt` / `cmake_log.txt`**
   两者是陈旧构建日志（内容指向已不存在的 `softrender_ws` 目录），
   已由 `.gitignore` 排除、不会入库。删除操作被用户取消，故保留在本地工作区。

4. **第三方源码全部入库**
   符合项目既定的「以源码形式嵌入 `third/`」策略。总量 12.9 MB，可接受。

## 风险

- **首次 `git add` 误纳入 590 MB `build/`** → 已缓解：严格执行「先 `.gitignore`
  再 `git init`」，并在 commit 前用 `git diff --cached --name-only` 逐项核对，
  确认无 `build/`、无 `output/` 产物（仅 `.gitkeep`）、无 `.exe`/`.tga`
- **中文提交信息在 PowerShell 下乱码** → 已缓解：用 UTF-8 文件 + `git commit -F`
  承载，已验证 commit `e12b16e` 的中文信息正确
- **git 对中文路径加引号导致输出难读** → 用 `git -c core.quotepath=false`
  临时绕过（不修改仓库配置）

## 关联文档

- `docs/architecture.md`
- `docs/modules.md`
- `docs/changelog.md`
- `third/README.md`
- `.qoder/rules/project_structure_rules.md`「Git 忽略规范」
- `.qoder/rules/documentation_rules.md`「文档目录规范」「README.md 规范」

## 变更日志

| 日期 | 变更 |
|------|------|
| 2026-09-05 | 创建任务；完成 `.gitignore` + git 初始化 + 首次提交 |
| 2026-09-05 | 完成 README / architecture / modules / changelog / troubleshooting / third 文档 |
| 2026-09-05 | 完成 `docs/tasks/` 建档（7 个 TASK） |
