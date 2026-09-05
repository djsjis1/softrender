# 变更日志

> 格式：日期 | 版本 | 变更内容 | 影响范围
>
> 按 `documentation_rules.md` 要求，每次有意义的变更都必须在此登记。
> 重大接口变更需同时在变更内容中标注版本号。

## 2026-09-05

### 光栅化器缺陷修复（TASK-002，已完成）

commit `73122d3`，`--no-ff` 合入 `develop`。

| 变更 | 影响范围 |
|---|---|
| **修复** `rasterizer_fast.h` 深度插值的重心权重错位（同序映射 → 错位映射），标量版 + SSE2 版共 6 处 | `src/raster/rasterizer_fast.h` |
| **修复** `setupEdges` 不做符号归一化导致顺时针三角形被静默丢弃 | `src/raster/rasterizer_fast.h` |
| 改写文件头注释，标注本模块为**实验性**（未接入 `Renderer` 主链路）及两个原因 | `src/raster/rasterizer_fast.h` |
| **新增 3 个回归测试**，单元测试 94 → 97 | `tests/test_core.cpp` |
| 校正 `docs/engineering/54` 全部五节数据漂移，新增「实验性能力」分区 | 文档 |

**修复前实测误差**：

| 采样点 | 期望深度 | 实际深度 | 误差 |
|---|---|---|---|
| 靠近 v0（w0=0.865） | 0.17917 | 0.50417 | **0.325** |
| 靠近 v1 | 0.49583 | 0.82083 | **0.325** |
| 靠近 v2 | 0.82083 | 0.19583 | **0.625** |
| 顺时针绕序 | 与逆时针一致 | `fb` 全黑、`zb` 恒为 `1e9` | **三角形完全丢弃** |

**为何长期未被发现**：原有两个 `RasterizerFast` 用例传入的深度全为
`0.5f / 0.5f / 0.5f`。顶点深度全相等时任何权重排列都算出同一个值，
缺陷被 100% 掩盖。已据此在 `engineering_rules.md` 新增红线：
「禁止用三个顶点取值全相同的数据写插值类测试」。

**公共接口影响**：无。`fillTriangleFast` / `fillTriangleIncremental` / `fillTriangleSSE2`
签名未变，仅修正内部计算与新增绕序容错。

---

### 工程基线建设（TASK-001）

| 变更 | 影响范围 |
|---|---|
| 初始化 git 仓库于 `develop` 分支，首次导入 891 个文件 | 全仓 |
| 新增 `.gitignore`，覆盖构建产物 / IDE / 测试输出 / 第三方构建产物 / 临时文件五类 | 全仓 |
| 新增 `output/.gitkeep`：所有示例以相对路径 `output/xxx` 写出结果，目录缺失会静默失败 | examples/、practice/ |
| 新增根 `README.md`（项目简介 / 特性 / 依赖 / 构建 / 快速启动 / 目录结构） | 文档 |
| 新增 `docs/architecture.md`（分层结构、模块依赖明细、数据流时序图、已知权衡） | 文档 |
| 新增 `docs/modules.md`（6 个 `src/` 子模块 + `platform/` 的职责与扩展点） | 文档 |
| 新增 `docs/changelog.md`、`docs/troubleshooting.md`、`docs/tasks/` | 文档 |
| 新增 `third/README.md`：8 个依赖的版本、许可证与**源码实测证据** | 文档 |
| 填写 `.qoder/rules/engineering_rules.md` 的「红线约束」与「命令」两节（原为模板占位符） | 规则 |

**实测校正的数据**（原 `docs/engineering/54-项目当前状态总览.md` 与实际不符）：

| 项 | 文档原值 | 实测值 | 证据 |
|---|---|---|---|
| GoogleTest 版本 | 1.15 | **1.18.0** | `third/googletest-main/CMakeLists.txt:7` |
| stb_image 版本 | latest | **2.30** | `third/stb/stb_image.h:1` |
| stb_image_write 版本 | latest | **1.16** | `third/stb/stb_image_write.h:1` |
| tinygltf 版本 | 2.9.3 | **v2.9.\***（头文件未锁定修订号） | `third/tinygltf/tiny_gltf.h:28` |
| `src/` 头文件数 | 35 | **52** | 实测统计 |
| 示例程序数 | 20 | **22** | 实测统计 |
| 文档篇数 | 54 | **72** | 实测统计 |
| 单元测试 | 62 用例 / 22 套件 | **94 用例 / 29 套件** | `unit_tests.exe` 实际运行输出 |
| C++ 标准 | 「`src/` 纯 C++17 标准库」 | **C++23**（`vec.h` 用 `requires` 约束） | `CMakeLists.txt:6` |

**新发现的架构事实**（原无处记录）：

- `third/entt/` 下存在**两份版本不同的 EnTT**：`src/` 是 3.14.0（构建实际使用），
  `single_include/` 是陈旧的 3.13.2
- `raster_fast::fillTriangleFast` **从未被 `Renderer` 调用**，仅 `core/benchmark.h`
  与单元测试引用
- `core/benchmark.h → raster/rasterizer_fast.h` 构成 `core/ → raster/` 的**层次倒置**

---

## 欠债登记

按 `documentation_rules.md`「早期原型阶段可暂缓部分文档，但必须在 changelog 记录欠债」：

| 欠债 | 说明 | 跟进 |
|---|---|---|
| 全局命名空间污染 | **60 个**顶层类型位于全局命名空间（分布在 30 个头文件），`Position`/`Light`/`Texture` 等名字对下游集成方构成冲突风险 | TASK-007 |
| `docs/api.md` / `docs/class_reference.md` 缺失 | 公共接口目前只能从 `docs/modules.md` 与各头文件注释获取 | TASK-001 后续 |
| 第三方版本未完全锁定 | googletest 为 main 分支快照；tinygltf / tinyobjloader 未锁定修订号 | `third/README.md`「已知版本锁定缺口」 |
| 无覆盖率度量 | `testing_rules.md` 要求核心模块行覆盖率 ≥ 80%，当前无任何覆盖率配置 | TASK-006 |
