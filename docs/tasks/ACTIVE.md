# 当前活跃任务

> 本文件是 `ai_workflow_rules.md` Step 0 要求开工前必读的入口。
> 全部任务索引见 [`README.md`](README.md)，已完成任务档案在 [`DONE/`](DONE/)。

## TASK-005：构建系统现代化

**快速状态**：待开始（下一个执行目标）

**目标**：消除 CMake 全局指令、补齐三构建类型与 Sanitizer、接入静态检查与格式化。

**阻塞项**：**需用户确认是否安装 LLVM 工具链**。
实测 `where.exe clang-format` 无输出，`clang-format` 与 `clang-tidy` 均未安装，
因此本任务的「零警告 `/WX`」「`.clang-tidy`」「全仓格式化」三节无法验收。
不安装也可先做 target-based 改造与 Sanitizer 两节。

**下一步**：CMake target-based 改造（消除 6 处 `include_directories` + `link_libraries`）

**关键约束**：改造前必须记录基线（24 个 exe + 120 单元测试 + 3 回归），
改造后**删除 build 缓存全新配置**再逐项比对，出问题直接 `git revert`。

---

## 剩余待修缺陷（按优先级）

| # | 缺陷 | 任务 |
|---|---|---|
| 1 | 无 Sanitizer：TASK-003 修的那类 UB（整数溢出、越界读）目前**没有任何机制能自动发现** | TASK-005 |
| 2 | MSVC 只有 `/W4` 无 `/WX`，规范要求零警告编译 | TASK-005 |
| 3 | `.clang-format` 从未执行：19 个 `src/` 头文件 namespace 缩进违反配置第 92 行 | TASK-005 |
| 4 | `tests/test_core.cpp` 1200+ 行单文件，29+ 套件混在一起 | TASK-006 |
| 5 | 无覆盖率度量，规范要求核心模块行覆盖率 ≥ 80% | TASK-006 |
| 6 | **60 个顶层类型位于全局命名空间**，`Position`/`Light`/`Texture` 对下游集成方构成实际冲突风险 | TASK-007 |

### 需用户手动执行的两项

| 项 | 原因 |
|---|---|
| `git rm src/test/test_framework.h` + `rmdir src\platform` | 删除工具被环境阻断，连续两次报成功而文件仍在。属无害死代码（零引用、不参与编译） |
| 推送远端并确认三平台 CI 全绿 | 本地无法验证 Linux/macOS 构建，TASK-004 的最终验收依赖远端 CI |

---

## 已完成

| 任务 | 核心成果 | 测试数 |
|---|---|---|
| [TASK-004](TASK-004-CI跨平台与测试基础设施.md) | 修复 ubuntu/macos job 必然编译失败；去掉吞失败的 `\|\| true`；接线像素回归并修复两个「永远假通过」陷阱（已反向验证）；ctest 39.89 秒 → 14.06 秒 | 113 → **120** + 3 回归 |
| [DONE/TASK-003](DONE/TASK-003-输入校验与边界安全.md) | `image_loader.h` 整数溢出 UB；`getPixel`/`DepthBuffer::get` Release 越界读 UB；`blend.h` 截断改四舍五入；5 个枚举合规；**`core/log.h` 内置模式重写为变参模板**（消除 4 个缺陷） | 97 → **113** |
| [DONE/TASK-002](DONE/TASK-002-光栅化器缺陷修复.md) | `rasterizer_fast.h` 深度插值重心权重错位（实测误差 0.325）+ 顺时针三角形静默丢弃 | 94 → **97** |
| TASK-001 | git 初始化 + `.gitignore`；README / architecture / modules / changelog / troubleshooting / third 文档；7 个 TASK 建档；规则红线（11 条）与命令 | — |

### 三个最值得记住的教训

1. **一致性测试不能替代正确性测试**。`SSE2MatchesScalar` 在修复前就通过——因为标量版与
   SSE2 版犯了同样的权重错位，两版彼此一致。正确性必须由独立算出的期望值来验证
   （TASK-002 用面积法另写了一份 `barycentricByArea`，刻意不复用被测代码的边函数）。

2. **对称盲点会让测试失效**。三角形几何重心处 `w0=w1=w2=1/3`，正确映射与错位映射
   都得到同一个值。采样点必须取靠近各顶点的非对称位置。同理，插值类测试的
   三个顶点取值**必须互不相等**——这条已写进 `engineering_rules.md` 红线。

3. **不能依赖文件名或既有注释推断事实**。`tiny_gltf_impl.cpp` 位于 `tinygltf/` 目录、
   名字也叫 `tiny_gltf_impl`，但它完全不含 tinygltf 的实现，只提供 stb。
   根 `CMakeLists.txt` 的旧注释也是错的，我最初轻信并写进了两份文档，
   读 `.cpp` 后才发现。
