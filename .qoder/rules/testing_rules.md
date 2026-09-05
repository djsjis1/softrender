---
trigger: model_decision
description: 任务涉及编写、修改或运行测试代码（测试用例、GoogleTest、覆盖率）时加载本规范
---
# 测试体系规范

## 本文件定位

本文件定义测试怎么写、测什么场景、如何维护。

与 `engineering_rules.md`（通用测试原则）和 `cpp_project_rules.md`（语言细节）配合使用。

---

# 测试框架

C++ 项目统一使用 GoogleTest（v1.14.0 及以上，必须锁定版本）。

引入方式（二选一，按 `cpp_project_rules.md` 依赖管理规范）：

* 集成到 `third/` 目录，并在 `third/README.md` 记录版本与来源
* CMake FetchContent 锁定版本

禁止：

* 自研测试框架（已有成熟开源方案，不得重复造轮子）
* 用 `main` 函数加 assert 代替测试框架

---

# 测试目录结构

与 `project_structure_rules.md` 对齐：

```
tests/
├── unit/                   # 单元测试（按模块镜像 src/ 结构）
│   ├── core/
│   ├── module_a/
│   └── module_b/
├── integration/            # 集成测试
├── fixtures/               # 测试数据文件
└── CMakeLists.txt
```

测试文件命名：`<类名>_test.cpp`（如 `MotionController_test.cpp`）。

> **SoftRenderer 现状**：单元测试仍集中在单个 `tests/test_core.cpp`
> （1300+ 行 / 120 用例 / 33 套件），尚未按上述结构拆分；
> `tests/integration/`（像素回归）与 `tests/golden/`（基准图）已建立。
> 拆分计划见 `docs/tasks/TASK-006-测试目录重构与覆盖率.md`。
> 拆分前后必须机械核对用例总数不变，不靠人工检查。

依赖测试钩子的用例（验证 UI / 硬件 / 时序等不可自动化路径）与常规单测同目录、同命名；因其依赖钩子构建开关，必须在 `docs/test_hooks.md` 清单中登记对应关系（见下节）。

---

# 四类必测场景

| 类别 | 测什么 | 触发条件 |
|------|--------|----------|
| 单元测试 | 单个类/单个函数的逻辑 | 所有新增公共逻辑 |
| 集成测试 | 多模块协作、协议收发闭环 | 新增模块或协议 |
| 压力测试 | 高负载下的性能与稳定性 | 性能关键路径 |
| 异常测试 | 边界、非法输入、资源失效 | 所有外部输入路径 |

异常测试至少覆盖：

* 空指针 / 非法参数
* 越界下标、溢出长度
* 网络断开、文件缺失、设备离线
* 并发竞争场景（若涉及多线程）

---

# 命名规范

测试用例使用 `被测方法_场景_预期结果` 格式：

```cpp
TEST(DecodeFrameTest, nullPacket_returnsNullopt);
TEST(DecodeFrameTest, validPacket_returnsFrame);
TEST(CommandQueueTest, undoBeyondDepth_keepsInitialState);
```

命名必须表达「在什么输入下，期望什么行为」。

禁止 `test1`、`testXxx` 等无意义命名。

## SoftRenderer 项目例外：存量用例不批量重命名

存量 120 个用例采用 `TEST(模块名, 场景描述)` 的 PascalCase 命名
（如 `TEST(Vec, DotOrthogonal)`、`TEST(Shape2D, ClipRestrictsDrawing)`），
不符合上述 `被测方法_场景_预期结果` 格式，但**不属于禁止的无意义命名**，
可读性不低于规定格式。

**决策：存量不批量重命名，新增用例强制用新格式。**

理由：

1. 批量重命名会改变 `gtest_discover_tests` 生成的 ctest 测试名，使 CI 历史
   与像素回归基准失去可比性
2. 属「低价值高扰动」改动，与 `engineering_rules.md`「最小变更：只做任务要求的
   改动，禁止顺手修复、重构无关代码」相冲
3. 新增用例已按新格式执行（如 `fillTriangleIncremental_slopeDepthMatchesBarycentric`、
   `loadImage_missingFile_returnsFalse`、`compare_uniformSmallDrift_failsOnAvgDelta`），
   新旧共存不会降低可读性

依据：`.qoder/rules/README.md`「规则与现实冲突时，先修规则，再执行」。

## SoftRenderer 项目例外：插值类测试的数据要求

插值 / 混合 / 变换类用例的输入**必须让各顶点取值互不相等**。

本项目已因此漏掉一个真实 bug：`rasterizer_fast.h` 的深度插值重心权重错位，
被两个都传 `z0=z1=z2=0.5f` 的用例 100% 掩盖 —— 顶点取值全相等时，
**任何权重排列都算出同一个值**，测试无法区分正确实现与错误实现。

同理，采样点不得取对称盲点（如三角形几何重心，该处三个权重相等）。
已录入 `engineering_rules.md` 红线约束。

另：**一致性测试不能替代正确性测试**。`SSE2MatchesScalar` 在修复前就通过 ——
因为标量版与 SSE2 版犯了同样的权重错位，两版彼此一致。正确性必须由
**独立算出的期望值**验证（该用例用面积法另写了一份 `barycentricByArea`，
刻意不复用被测代码的边函数）。

---

# 测试编写原则

* 一个用例只验证一个行为
* 测试必须独立：无顺序依赖、无共享可变状态
* Arrange-Act-Assert 结构清晰
* 外部依赖（数据库、网络、文件系统）必须 mock，业务层测试不得真实联网/落盘
* 禁止先写实现再「凑」出让它通过的测试（先明确预期行为）

---

# 覆盖率要求

* 新增逻辑必须有测试覆盖，无例外；无法自动化的路径（UI 点击、硬件依赖、时序敏感）经测试钩子自测并在测试报告中声明，不得静默跳过
* 核心模块：行覆盖率 ≥ 80%，分支覆盖率 ≥ 60%
* 覆盖率报告纳入 CI，覆盖率下降必须说明原因

---

# AI 测试职责

在 `ai_workflow_rules.md` 的 Step 5（编码实现）中必须同步：

1. 为新增逻辑编写对应测试
2. 运行现有测试，确认未破坏既有用例
3. 在 Step 7（最终确认）中声明测试情况（新增用例列表 / 运行结果）
4. 无法自动化覆盖的场景（UI 点击、硬件依赖、时序敏感），通过测试钩子自测；缺少钩子时先按 `cpp_project_rules.md` 的「AI 自测钩子规范」补设计，禁止跳过验证

禁止以「逻辑简单」为由跳过测试。

---

# 测试钩子：清单登记与构建联动

钩子的设计与实现规范见 `cpp_project_rules.md`「AI 自测钩子规范」，本节定义钩子与测试体系的衔接。

## 钩子清单：`docs/test_hooks.md`

新增 / 删除钩子时必须同步维护，格式：

```markdown
| 钩子 | 所在类/文件 | 自测用途 | 依赖用例 | 计划剔除时机 |
|------|------------|----------|----------|--------------|
| testHook_triggerUndo | TimelineController | 验证撤销链路（UI 按钮无法自动点击） | TimelineController_test.cpp | UI 自动化接入后 |
```

* 「计划剔除时机」必填：钩子是临时设施，自动化手段能覆盖后应删除并同步清理清单与用例。
* 剔除时只需关闭构建开关验证主链路不受影响，再删除集中声明的钩子段与对应清单项，无需全局搜索散落代码。

## 构建联动

* 钩子测试与钩子本体共用同一开关（如 `PROJECT_ENABLE_TEST_HOOKS`）：Debug / 测试构建开启，Release 构建必须关闭（见 `cpp_project_rules.md` 构建类型表）
* 开关关闭时，依赖钩子的用例目标不得参与编译（CMake 条件编译），避免发布构建因钩子不可见而编译失败；常规单测不受开关影响，任何构建都必须通过

