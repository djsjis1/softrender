# TASK-006：测试目录重构与覆盖率接入

## 基本信息

- 创建日期：2026-09-05
- 状态：待开始
- 涉及模块：`tests/`、`CMakeLists.txt`、`.github/workflows/ci.yml`、`.qoder/rules/testing_rules.md`
- 变更级别：**L**（跨模块 + CI + 规则例外条款）
- 前置依赖：TASK-002、TASK-003、TASK-004（先补完测试用例，再拆分文件，避免重复搬运）

## 目标

把单一巨型测试文件重构为符合规范的目录结构，并接入覆盖率度量。

## 一、测试目录结构不合规

**现状**：单个 `tests/test_core.cpp`，**1079 行 / 94 用例 / 29 套件**。

**要求**：`testing_rules.md`「测试目录结构」与 `project_structure_rules.md` 要求
`tests/unit/<模块>/` 镜像 `src/` 结构，文件命名 `<类名>_test.cpp`。

### 拆分方案

29 个套件按模块归属拆分：

```
tests/
├── unit/
│   ├── core/       arena_test.cpp          blend_test.cpp
│   │               command_buffer_test.cpp depthbuffer_test.cpp
│   │               framebuffer_test.cpp    gbuffer_test.cpp
│   │               resource_manager_test.cpp benchmark_test.cpp
│   ├── math/       vec_test.cpp     mat3_test.cpp   mat4_test.cpp
│   │               quat_test.cpp    rect_test.cpp   transform_test.cpp
│   │               transform2d_test.cpp
│   ├── raster/     barycentric_test.cpp  clip_test.cpp
│   │               rasterizer_fast_test.cpp renderer2d_test.cpp
│   │               shape2d_test.cpp      blit2d_test.cpp
│   └── scene/      camera2d_test.cpp   cubemap_test.cpp  ecs_test.cpp
│                   mesh_test.cpp       particle_test.cpp render_queue_test.cpp
│                   skeleton_test.cpp   texture_test.cpp
├── integration/    render_regression_test.cpp   （TASK-004 建立）
├── fixtures/       （TASK-003 建立：极小合法 PNG）
└── golden/         （TASK-004 建立：基准 TGA）
```

`CMakeLists.txt` 用 `file(GLOB ... CONFIGURE_DEPENDS tests/unit/**/*_test.cpp)` 收集，
与第 213 行 `practice/` 已有的 GLOB 模式保持一致（该模式已在本项目验证可用：
新增/删除文件后构建自动重新配置）。

### 附带收益

拆分是**纯机械移动**，不改测试逻辑。但 `test_core.cpp` 顶部第 7-40 行有 34 个
`#include`，拆分后每个新文件只保留自己用到的几个——顺带解决
`cpp_project_rules.md`「尽量使用前置声明减少不必要的 `#include`（降低编译依赖）」
的部分欠债，并显著改善单文件编译时间。

## 二、用例命名格式：不批量重命名，只约束新增

`testing_rules.md`「命名规范」要求 `被测方法_场景_预期结果`
（如 `decodeFrame_nullPacket_returnsNullopt`）。

现有命名是 `TEST(Vec, DotOrthogonal)`、`TEST(Shape2D, ClipRestrictsDrawing)`——
**套件名 + PascalCase 场景描述**，不符合规定格式，但也**不属于规范禁止的
「`test1`、`testXxx` 等无意义命名」**，可读性其实很好。

**决策：不批量重命名，只对新增用例强制新格式。**

理由：
1. 批量重命名 94 个用例会改变 `gtest_discover_tests` 生成的 ctest 测试名，
   使 TASK-004 建立的像素回归基准与 CI 历史失去可比性
2. 现有命名的信息量并不低于规定格式
3. `engineering_rules.md`「最小变更：只做任务要求的改动，禁止顺手修复、重构无关代码」
   —— 这属于**低价值高扰动**的改动

需在 `.qoder/rules/testing_rules.md` 增加项目专属例外条款（依据
`.qoder/rules/README.md`「规则与现实冲突时，先修规则，再执行」）：

> 本项目例外：既有用例保持 `TEST(模块名, 场景描述)` 的 PascalCase 命名，
> 不做批量重命名（会破坏 ctest 测试名稳定性与 CI 历史可比性）。
> **新增用例必须**采用 `被测方法_场景_预期结果` 格式。

## 三、覆盖率接入

`testing_rules.md`「覆盖率要求」：核心模块**行覆盖率 ≥ 80%、分支覆盖率 ≥ 60%**，
且「覆盖率报告纳入 CI，覆盖率下降必须说明原因」。当前**无任何覆盖率配置**。

**方案**：`ci.yml` 新增一个 job，用 `ubuntu-latest`
（OpenCppCoverage 在 Windows 上配置复杂，且 GCC 的 gcov 生态最成熟）：

- 编译加 `--coverage`
- 用 `lcov` 收集，`genhtml` 生成报告
- 上传为 workflow artifact

**门禁策略：首次运行只报告不设阈值。**

把实测基线写进 `docs/changelog.md`，第二次迭代再启用阈值门禁。

理由：一上来就设 80% 门禁，在实测基线未知的情况下极可能直接红灯，
违反 `git_rules.md`「禁止带红灯合并」而使 CI 失去意义。
先量出基线，再定一个「不许下降」的棘轮式门禁，才是可执行的路径。

## 进度清单

- [ ] 新建 `tests/unit/{core,math,raster,scene}/` 目录
- [ ] 按 29 个套件把 `test_core.cpp` 拆分为 30 个 `<模块>_test.cpp`
- [ ] 每个新文件只保留实际用到的 `#include`
- [ ] `CMakeLists.txt` 改用 GLOB 收集 + `foreach` 注册
- [ ] 全量构建，确认用例总数仍为 **94**（拆分前后必须一致）
- [ ] 确认 `ctest` 全绿
- [ ] 删除原 `tests/test_core.cpp`
- [ ] 在 `.qoder/rules/testing_rules.md` 增加命名例外条款
- [ ] `ci.yml` 新增覆盖率 job（ubuntu + gcov + lcov）
- [ ] 记录实测覆盖率基线到 `docs/changelog.md`

## 风险

- **拆分过程漏掉用例** → 缓解：拆分前后 `unit_tests.exe` 的用例总数必须
  **精确等于 94**，用 `--gtest_list_tests | wc -l` 机械核对，不靠人工检查
- **GLOB 导致新增测试文件不被发现** → 缓解：用 `CONFIGURE_DEPENDS`，
  该模式已在 `practice/` 验证有效
- **拆分后各文件 include 不足导致编译失败** → 属安全失败（编译器会明确指出），
  逐个补齐即可
- **覆盖率 job 拖慢 CI** → 缓解：设为独立 job 与构建 job 并行，不在关键路径上

## 关联文档

- `docs/tasks/TASK-004-CI跨平台与测试基础设施.md`（fixtures / golden / integration 目录由该任务建立）
- `.qoder/rules/testing_rules.md`「测试目录结构」「命名规范」「覆盖率要求」
- `.qoder/rules/project_structure_rules.md`「标准目录结构」

## 变更日志

| 日期 | 变更 |
|------|------|
| 2026-09-05 | 创建任务；确认现状为 1079 行 / 94 用例 / 29 套件单文件 |
