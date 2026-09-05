# TASK-004：CI 跨平台修复与测试基础设施

## 基本信息

- 创建日期：2026-09-05
- 状态：待开始
- 涉及模块：`CMakeLists.txt`、`.github/workflows/ci.yml`、`src/test/`、`tests/`
- 变更级别：**L**（跨模块 + CI + 新增测试类型）
- 前置依赖：TASK-001（无 git 仓库则 CI 从未运行过，无法验收）

## 目标

让 CI 第一次真正跑起来并全绿，清理死代码，接线像素回归测试。

## 缺陷一：CI 在 Linux / macOS 上必然编译失败

### 根因（已验证）

`platform/win32_window.h:11` 用 `#ifdef _WIN32` 包住**整个文件内容**。
非 Windows 平台该头文件展开为空，`Win32Window` 类不存在。

而三个示例**无条件**使用它：

| 文件 | include 行 | 使用位置 |
|---|---|---|
| `examples/stage1_sprite_anim.cpp` | 20 | 106、148、152 |
| `examples/stage6_realtime.cpp` | 18 | 40、105-128 |
| `examples/stage8_realtime.cpp` | 17 | 47、98-123 |

`CMakeLists.txt` 对这三个目标的 `add_executable` **没有平台守卫**：
第 119-120 行、第 128 行、第 143 行。`if(WIN32)` 只包住了
`target_link_libraries(... gdi32 user32)`（第 122-124、130-132、145-147 行），
**没包住编译本身**。

结论：`ci.yml:13` 的矩阵 `os: [windows-latest, ubuntu-latest, macos-latest]` +
第 29 行 `cmake --build build`（构建全部目标）→ **ubuntu 与 macos 两个 job 100% 编译失败**。

因为仓库从未 push 过（无 git），这个 CI 从未运行，问题一直未暴露。

### 方案对比

**方案 A：CMake 层加平台守卫（推荐）**

```cmake
if(WIN32)
    add_executable(stage1_sprite_anim examples/stage1_sprite_anim.cpp)
    target_include_directories(stage1_sprite_anim PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(stage1_sprite_anim PRIVATE gdi32 user32)
    # stage6_realtime / stage8_realtime 同理
endif()
```

- 优点：改动约 15 行；`src/` 一行不动，完全符合 `CMakeLists.txt:76-78` 声明的
  「渲染器本体永远接触不到平台代码」的分层意图；离线示例（`stage6_offline` /
  `stage8_offline`）仍在三平台构建，跨平台能力依然被覆盖
- 缺点：非 Windows 平台无法验证窗口代码路径（但 Win32 API 本就不存在，无从验证）

**方案 B：源码层加 `#ifdef _WIN32` + 非 Windows 退化为离屏输出**

- 优点：三平台都能构建同名目标
- 缺点：需改 3 个示例的 `main` 结构，每个写两套逻辑，约 150 行；与「示例代码
  保持简洁易读」的教学定位冲突

**选 A**：`engineering_rules.md` 权衡原则「可维护性 > 可扩展性 > 性能 > 开发速度」，
A 在可维护性与改动量上均更优。

## 缺陷二：CI smoke test 用 `|| true` 吞掉所有失败

`ci.yml:47`：

```bash
"$BIN/$stage" || "$BIN/$stage.exe" || true
```

末尾的 `|| true` 让 smoke test **永远不会失败**。这是典型的掩盖症状，违反
`engineering_rules.md`「根因原则：禁止掩盖症状（如吞异常、绕过报错）」。

**修复**：先探测可执行文件名再直接调用，让非零退出码正常传播：

```bash
for stage in ...; do
  if [ -x "$BIN/$stage" ]; then "$BIN/$stage"
  elif [ -f "$BIN/$stage.exe" ]; then "$BIN/$stage.exe"
  fi
done
```

## 缺陷三：smoke test 清单遗漏

`ci.yml:44` 的清单缺：`stage12_threaded`（多线程光栅化，纯离屏，三平台都该跑）、
`stage1_sprite`、`demo_2d`、`demo_ibl_pbr`。

`demo_gltf` / `demo_pbr_material` / `demo_skinning` 是**自产自销的往返测试**
（运行时先写 `.gltf` 到 `output/` 再读回），只要 `output/` 存在就不需要外部资产，
**可以安全加入**清单。

## 缺陷四：死代码

| 目标 | 现状 | 处置 |
|---|---|---|
| `src/test/test_framework.h`（96 行，`minitest` 自研框架） | 全仓引用扫描：**除自身外零引用** | **删除**。`testing_rules.md`「禁止自研测试框架（已有成熟开源方案，不得重复造轮子）」，项目已用 GoogleTest |
| `src/platform/`（空目录） | 平台代码实际在根 `platform/win32_window.h` | **删除**，避免结构误导 |

## 缺陷五：像素回归测试未接线

`src/test/pixel_regression.h`（111 行）**除自身外零引用**，`tests/golden/` 为空。
但 `docs/engineering/27-像素级回归测试.md` 与 `docs/engineering/54` 都把像素回归
列为已有能力。

### 决策：保留并接线，不删除

**理由**：这是本项目最有价值的测试缺口。当前 94 个单元测试全是「局部性质断言」
（某像素是否被涂色、某向量点积是否为 0），**没有任何一个测试锁定完整渲染结果的
视觉正确性**。TASK-002 那个深度插值 bug 之所以能长期潜伏，正是因为缺少端到端的
视觉回归网。

### 接线步骤

1. 新建 `tests/integration/render_regression_test.cpp`
2. 选 3 个**确定性、无随机、无时间依赖**的场景生成基准图：
   `stage4_zbuffer`（深度遮挡）、`stage6_offline`（三种着色模式）、
   `stage10_pbr`（PBR 光照）。
   **跳过** `demo_stars`（含随机星点）与 `stage12_threaded`（多线程顺序不确定）
3. 在 `CMakeLists.txt` 注册为独立可执行目标并 `add_test`

### 陷阱一：基准图相对路径在 CI 中失效（会导致永远假通过）

`pixel_regression.h:78, 87` 硬编码相对路径 `"tests/golden/" + name + ".tga"`，
只有从**仓库根目录**运行才正确。CI 的 `ctest --test-dir build`（`ci.yml:32`）
工作目录是 `build/`，会解析成 `build/tests/golden/` → 找不到基准图 →
走第 91-100 行的「基准图不存在则自动创建并返回 passed」分支 → **测试永远假通过**。

**修复**：给 `saveGolden` / `compareWithGolden` 增加基准目录参数，由 CMake 通过
`target_compile_definitions` 注入 `${CMAKE_SOURCE_DIR}/tests/golden` 的**绝对路径**，
保留现有单参重载作为默认。

### 陷阱二：自动创建基准 = CI 中的静默失败

第 91-100 行「基准图不存在则自动创建并返回 passed」违反「禁止静默失败」。

**修复**：由编译期开关控制——本地开发（定义 `SR_UPDATE_GOLDEN` 时）才自动创建；
CI 中基准图缺失必须**直接 FAIL** 并给出明确信息。

### 验收方式

修完必须在 CI 里**故意删掉一个基准图**，验证确实会红灯。
「假通过」的测试比没有测试更危险。

## 缺陷六：ctest 40 秒 vs 测试本身 28 毫秒

| 运行方式 | 实测耗时 |
|---|---|
| `ctest --test-dir build --output-on-failure` | **39.89 秒** |
| `.\build\bin\Release\unit_tests.exe`（单进程跑完 94 用例） | **28 毫秒**（进程墙钟 0.79 秒） |

**根因**：`CMakeLists.txt:225` 的 `gtest_discover_tests(unit_tests)` 把**每个
GTest 用例注册为独立 ctest 测试项**，94 用例 = 94 次进程创建，实测每项 0.33~0.45 秒，
几乎全是 Windows 进程启动 + GTest 初始化开销。`ci.yml:32` 的 `ctest` 未加 `-j`，
串行放大到 40 秒。

**修复（两步都做）**：

1. `ci.yml:32` 与 `engineering_rules.md`「命令」节加 `--parallel`
   → 墙钟降到约 2~3 秒
2. 保留 `gtest_discover_tests`（CI 失败时能精确定位到单个用例，价值大于开销），
   但**额外**注册一个整体冒烟项：
   ```cmake
   add_test(NAME unit_tests_all COMMAND unit_tests)
   set_tests_properties(unit_tests_all PROPERTIES LABELS "fast")
   ```
   开发迭代时 `ctest --test-dir build -L fast` 只跑这一项（28 毫秒）

## 进度清单

- [ ] `CMakeLists.txt` 给三个 realtime 目标加 `if(WIN32)` 守卫（方案 A）
- [ ] `ci.yml` 去掉 `|| true`，改为探测后直接调用
- [ ] `ci.yml` smoke 清单补 `stage12_threaded`、`stage1_sprite`、`demo_2d`、
      `demo_ibl_pbr`、`demo_gltf`、`demo_pbr_material`、`demo_skinning`
- [ ] `ci.yml` 的 `ctest` 加 `--parallel`
- [ ] 删除 `src/test/test_framework.h`
- [ ] 删除空目录 `src/platform/`
- [ ] `pixel_regression.h` 增加基准目录参数（CMake 注入绝对路径）
- [ ] `pixel_regression.h` 自动创建基准改为 `SR_UPDATE_GOLDEN` 开关控制
- [ ] 新建 `tests/integration/render_regression_test.cpp`（3 个确定性场景）
- [ ] `CMakeLists.txt` 注册回归测试目标 + `add_test`
- [ ] 生成并提交 `tests/golden/*.tga` 基准图
- [ ] `CMakeLists.txt` 增加 `unit_tests_all` 冒烟测试项 + `fast` label
- [ ] **推送到 GitHub，确认三个平台 job 全绿**
- [ ] **故意删除一个基准图，确认 CI 红灯**（验证不是假通过）

## 风险

- **无法本地验证 Linux / macOS 构建** → 缓解：明确标注「验收依赖远端 CI」。
  本地可用临时把 `if(WIN32)` 改成 `if(FALSE)` 的方式验证目标确实被跳过、
  其余目标仍能全量构建
- **像素回归假通过** → 缓解：见「验收方式」，必须用故意删基准图的方式反向验证
- **基准图跨平台不一致**（不同编译器浮点结果微差）→ 缓解：`compare` 已允许
  0.1% 像素偏差 + 平均差 < 0.5 的容差；若三平台仍不稳定，改为只在
  `windows-latest` 跑像素回归，其余平台跳过并在 `docs/troubleshooting.md` 记录
- **删除 `test_framework.h` 后编译失败** → 已验证零引用，风险极低；
  删除后必须全量构建确认

## 关联文档

- `docs/architecture.md` §8（平台隔离机制）
- `docs/troubleshooting.md`「非 Windows 平台构建失败」「ctest 跑一次要 40 秒」
- `docs/engineering/27-像素级回归测试.md`、`docs/engineering/40-CI-CD配置.md`
- `.qoder/rules/testing_rules.md`「四类必测场景」「测试目录结构」

## 变更日志

| 日期 | 变更 |
|------|------|
| 2026-09-05 | 创建任务；完成 CI 失败根因验证（确认 `#ifdef _WIN32` 导致非 Windows 编译失败） |
| 2026-09-05 | 实测确认 ctest 40 秒 vs 单进程 28 毫秒的差异根因 |
