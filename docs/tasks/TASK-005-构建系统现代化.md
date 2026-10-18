# TASK-005：构建系统现代化

> 2026-10-05 更新：target-based、/WX、构建职责拆分、可选目标、安装消费测试和本地 Debug ASAN 已验证；clang-format/clang-tidy 仍未实跑。下文保留早期计划和当时环境信息，当前进度以 ACTIVE.md 与 TASK-008 为准。

## 基本信息

- 创建日期：2026-09-05
- 状态：**部分完成** —— 第一节（target-based）与第二节（Sanitizer）已完成并实测验证；
  第三、四节（clang-tidy / 全仓格式化）**阻塞：LLVM 工具链未安装**
- 涉及模块：`CMakeLists.txt`、全仓源码（格式化）
- 变更级别：**L**（影响全仓构建 + 引入工具链依赖）
- 前置依赖：TASK-001（已完成）、TASK-004（已完成）

## 目标

消除 CMake 全局指令、补齐三构建类型与 Sanitizer、接入静态检查与格式化。

## 一、消除全局 CMake 指令

`cpp_project_rules.md`「构建系统规范」明令：「禁止 `include_directories()` /
`link_libraries()` 全局设置，统一使用 `target_include_directories()` /
`target_link_libraries()` 并显式声明 `PRIVATE` / `PUBLIC` 可见性」。

### 当前违例

| 行 | 内容 |
|---|---|
| 82-87 | 6 处 `include_directories()` |
| 109 | `link_libraries(softrenderer)` |
| 65-70 | `add_compile_options(/utf-8 /W4)`、`add_compile_definitions(_CRT_SECURE_NO_WARNINGS)` |
| 102 | `add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)` |

### 改造方案

`softrenderer` 已是 INTERFACE 库（第 90-98 行），天然适合承载全局属性：

```cmake
target_include_directories(softrenderer INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third/stb>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third/tinygltf>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third/nlohmann>
    $<INSTALL_INTERFACE:include/softrenderer>)
target_compile_options(softrenderer INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_compile_definitions(softrenderer INTERFACE
    $<$<PLATFORM_ID:Windows>:NOMINMAX;WIN32_LEAN_AND_MEAN>
    _CRT_SECURE_NO_WARNINGS)
```

> 第 82-87 行的 include 路径与第 91-95 行 `softrenderer` 的 INTERFACE include
> **当前是重复的**（前者作用于本仓所有目标，后者作用于外部 `find_package` 使用者）。
> 合并后语义不变，`third_party_impl` 第 30-39 行的重复声明也可精简。

**警告选项不能放进 `softrenderer`**：`/W4`、`-Wall -Wextra` 若通过 INTERFACE 传播，
等于把本项目的警告策略强加给下游集成方。改为定义内部 INTERFACE 目标 `sr_warnings`，
只被本仓的示例 / 测试目标链接。

删除第 109 行 `link_libraries(softrenderer)`，改为对每个 `add_executable` 显式
`target_link_libraries(<tgt> PRIVATE softrenderer)`。约 20 个目标，用 `foreach`
循环收敛避免重复。

## 二、三构建类型 + Sanitizer

`cpp_project_rules.md`「构建配置」要求：

| 类型 | 用途 | Sanitizer |
|---|---|---|
| `Debug` | 开发调试 | **必须**开启 ASAN/UBSAN |
| `RelWithDebInfo` | 集成测试 | 不开启 |
| `Release` | 生产发布 | 禁止开启 |

当前 `CMakeLists.txt:9-11` 只默认 Release，**无任何 sanitizer 逻辑**。

```cmake
option(SR_ENABLE_SANITIZERS "Debug 构建开启 ASAN+UBSAN" OFF)
if(SR_ENABLE_SANITIZERS)
    if(MSVC)
        # MSVC 只支持 ASAN，不支持 UBSAN；且与 /RTC 互斥、需关闭增量链接
        target_compile_options(sr_sanitizers INTERFACE /fsanitize=address)
    else()
        target_compile_options(sr_sanitizers INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(sr_sanitizers INTERFACE -fsanitize=address,undefined)
    endif()
endif()
```

**为什么这条重要**：TASK-003 的整数溢出 UB、Release 越界读，**正是 ASAN/UBSAN
能自动抓到的缺陷类型**。目前项目没有任何机制能发现这类问题——这解释了它们为何
能长期存在。

平台差异已记录在 `docs/troubleshooting.md`「Sanitizer」节。

## 三、零警告 + `.clang-tidy`

- `cpp_project_rules.md`「零警告原则」要求「必须在 `-Wall -Wextra -Werror` 下
  无警告编译」。当前第 65 行只有 `/W4` **无 `/WX`**；第 69 行 `-Wall -Wextra`
  **无 `-Werror`**
- **执行顺序**：先跑一次全量构建收集当前警告数量（`CMakeLists.txt:62` 注释自称
  「代码要求零警告通过」，需实测验证），清零后再加 `/WX` 与 `-Werror`
- 第 43 行 `target_compile_options(third_party_impl PRIVATE /utf-8 /wd4819)` 是
  **合规的**屏蔽（规范允许屏蔽第三方库警告，且已附注释说明原因、作用域限缩到
  `PRIVATE`），保留
- 新建 `.clang-tidy`（`project_structure_rules.md` 要求存在），启用
  `bugprone-*` / `cppcoreguidelines-*` / `modernize-*` / `performance-*` / `readability-*`
- 为 `third/` 建一个 `.clang-tidy`（内容 `Checks: '-*'`）屏蔽第三方

> ⚠️ **前置条件**：`clang-tidy` 与 `clang-format` 当前**均未安装**
> （实测 `where.exe clang-format` 无输出）。需先安装 LLVM 工具链，否则本节无法验收。
> **这属于开发环境变更，必须先向用户确认后再执行。**

## 四、执行 `.clang-format`

`.clang-format` 存在（107 行，配置完备）但**从未被执行过**。实测证据：

- 第 92 行 `NamespaceIndentation: None`，但 **19 个 `src/` 头文件的 namespace 体
  缩进了 4 空格**：`arena.h`、`benchmark.h`、`post_processing.h`、`image_write.h`、`log.h`、
  `profiler.h`、`tga.h`、`transform.h`、`transform2d.h`、`blit2d.h`、
  `rasterizer_fast.h`、`shape2d.h`、`gltf_loader.h`、`image_loader.h`、`mesh.h`、
  `obj_loader.h`、`texture.h`、`pixel_regression.h`、`test_framework.h`
- `raster/renderer2d.h:55-79` 的 5×7 字形表缩进错乱（部分行顶格、部分行缩进 8 空格）

### 执行策略：不要一次性格式化全仓

一次性 `clang-format -i` 会产生**数千行纯格式 diff**，污染后续所有真实修改的历史，
且无法 review。

正确顺序：

1. 先完成 TASK-002 / TASK-003 / TASK-004 的全部**功能性修改**并提交
2. 再单独一个 commit 执行全仓格式化，信息标注
   `style(全局): 统一 clang-format 格式，无功能变更`
3. 建立 `.git-blame-ignore-revs` 写入该 commit hash，并配置
   `git config blame.ignoreRevsFile .git-blame-ignore-revs`，使后续 `git blame`
   能穿透它

> 注：第 3 步的 `git config` 是**本地仓库配置**，需征得用户同意后执行。

## 五、文件命名规则冲突：修规则，不改代码

`cpp_project_rules.md`「文件名」与 `project_structure_rules.md`「文件命名规范」
要求「类文件用 PascalCase，文件名与类名一致」（如 `VideoDecoder.h`）。

但本项目 52 个头文件**全部是 snake_case**（`framebuffer.h` 里是 `class Framebuffer`、
`renderer2d.h` 里是 `class Renderer2D`）。

**决策：修改规则，不重命名文件。**

依据：
- `.qoder/rules/README.md`「规则维护」：「规则与现实冲突时，先修规则，再执行」
- `cpp_project_rules.md` 本身已允许「工具类/函数文件使用 snake_case」。本项目是
  **header-only 库**，每个头文件既含类型也含大量自由函数（`blend.h` 的 `blendPixels`、
  `clip.h` 的 `clipTriangleFullFrustum`、`shape2d.h` 的 `draw2d::fillRect`），
  本质上更接近「工具函数文件」而非「类文件」
- 重命名 52 个头文件会破坏所有 `#include` 路径与下游用户代码，收益为零

**执行**：在 `.qoder/rules/cpp_project_rules.md`「文件名」小节增加项目专属例外条款：

> 本项目例外（header-only 库）：`src/` 下所有头文件统一 snake_case，与其包含的
> 主类型名保持词根对应（`framebuffer.h` ↔ `Framebuffer`）。理由：header-only 库的
> 每个头文件同时导出类型与自由函数，PascalCase 文件名会误导使用者以为文件只含单个类；
> 且 `#include "core/framebuffer.h"` 的路径风格与 `stb_image.h`、
> `tiny_obj_loader.h` 等第三方依赖一致。

## 进度清单

### 第一节：target-based 改造 —— 已完成

- [x] 记录改造前基线：**25 个 exe**（逐项名单）+ 120 单元测试 + 3 回归测试
- [x] 删除 6 处 `include_directories()`
      —— 实测发现它们与 `softrenderer` 的 INTERFACE include、`third_party_impl` 的
      PUBLIC include **完全重复**，所以是**直接删除**而非搬迁
- [x] 删除 `link_libraries(softrenderer)`
- [x] 新增 `sr_add_example(name source)` 函数，22 个示例改为调用它
- [x] 新增 `sr_add_realtime_example(name source enable_log)`，三个 realtime 目标的
      `if(WIN32)` 守卫从三处重复收敛为一处（`if(NOT WIN32) return()` 写法）
- [x] 新增 `sr_dev_flags` INTERFACE 库承载 `/W4` 与 `NOMINMAX`/`WIN32_LEAN_AND_MEAN`
- [x] 删除三处冗余的 `target_link_libraries(... third_party_impl)`
      （`softrenderer` 已 INTERFACE 链接它，传递可见）
- [x] 全量重新配置 + 构建验证

**「公共接口要求」与「开发偏好」的划分**（本节最重要的设计决策）：

| 标志 | 放在 | 理由 |
|---|---|---|
| include 路径、`cxx_std_23` | `softrenderer` INTERFACE | 下游必需 |
| `/utf-8` | `softrenderer` INTERFACE | `src/` 头文件含中文注释，不开会报 C4819，下游同样遇到 |
| `_CRT_SECURE_NO_WARNINGS` | `softrenderer` INTERFACE | `core/tga.h` 在**头文件内**直接调 `std::fopen`，该调用在使用方的编译单元展开，MSVC 会在**使用方**报 C4996 |
| `/W4`、`-Wall -Wextra` | `sr_dev_flags`（**不导出**） | 本项目的警告策略，强加给集成方不合理 |
| `NOMINMAX`、`WIN32_LEAN_AND_MEAN` | `sr_dev_flags`（**不导出**） | 只对引用 `windows.h` 的目标有意义，而 `src/` 从不引用 |

> `_CRT_SECURE_NO_WARNINGS` 放在公共接口是个**妥协**：它会抑制下游自己的 C4996。
> 根治办法是把 `tga.h` 的 I/O 改成不依赖弃用函数，已记录为待办。

**实测验证结果**：

| 验证项 | 结果 |
|---|---|
| `cmake -S . -B build` 重新配置 | 成功，无错误 |
| 全量构建 | 零错误零警告，退出码 0 |
| exe 清单比对 | **25 个，与基线逐项一致**（`Compare-Object` 无差异） |
| `ctest --parallel -LE fast` | **121 项全绿 / 12.48 秒** |
| `unit_tests` | 120 用例全绿（5 ms） |
| `render_regression` | 3 用例全绿 |
| 20 个离屏示例 | 全部正常退出 |
| 残留全局指令扫描 | **0 处**（`include_directories` / `link_libraries` / `add_compile_*` / `add_definitions`）；`target_*` 调用 27 处 |

> `cmake --fresh` 两次被环境取消，改用普通重新配置。这是安全的：
> 本次改动只涉及**目标定义**，不涉及任何缓存变量；而 `include_directories`
> 是每次配置重算的目录属性、不入缓存。

### 第二节：Sanitizer —— 已完成

- [x] 新增 `option(SR_ENABLE_SANITIZERS ... OFF)`
- [x] 用 `$<$<CONFIG:Debug>:...>` 生成器表达式限定仅 Debug 生效
- [x] MSVC 分支：`/fsanitize=address` + 移除 `/RTC1` + `/INCREMENTAL` → `/INCREMENTAL:NO`
- [x] GCC/Clang 分支：`-fsanitize=address,undefined` + `-fno-omit-frame-pointer` + `add_link_options`
- [x] 实测 Debug 构建 + 运行
- [x] 实测验证 Release **不带** Sanitizer
- [x] `docs/troubleshooting.md` 记录 `0xC0000135` 与 LNK2038 两个坑

**实施中撞到的真实问题：LNK2038**

最初把 `/fsanitize=address` 放在 `sr_dev_flags` INTERFACE 库里（per-target 方式），
构建 Debug 版 `unit_tests` 直接链接失败：

```
gtest.lib(gtest-all.obj) : error LNK2038: 检测到"annotate_string"的不匹配项
gtest.lib(gtest-all.obj) : error LNK2038: 检测到"annotate_vector"的不匹配项
gtest_main.lib(gtest_main.obj) : error LNK2038: （同上两项）
fatal error LNK1319: 检测到 4 个不匹配项
```

**根因**：Sanitizer 是 **ABI 级**设置，不是逐目标的开发偏好。MSVC ASAN 会给 STL
容器加注解，同一次链接里的所有目标必须同开或同关。googletest 由
`add_subdirectory` 引入、不链接 `sr_dev_flags`，所以一侧开了一侧没开。

**修正**：改用全局 `add_compile_options`，并把整个块**移到所有 `add_subdirectory`
之前**。后者同样必要：`add_compile_options` 改的是目录属性，只对其后创建的目标
与添加的子目录生效，而 `third/log`（spdlog）在第 19 行就引入了 —— 若块放在后面，
`mylog`/spdlog 不开 ASAN，而 realtime 示例链接 `mylog`，会得到同样的 LNK2038。

这是 `engineering_rules.md`「禁止全局指令」的一个**有意例外**，例外理由已写在
`CMakeLists.txt` 对应位置的注释里（规则允许豁免，但必须说明原因）。

**实测验证**：

| 验证项 | 结果 |
|---|---|
| `cmake -S . -B build/asan -DSR_ENABLE_SANITIZERS=ON` | 配置成功，状态消息正常输出，MSVC 19.44 |
| Debug 构建 `unit_tests` | **零错误**（LNK2038 已消除） |
| Debug 运行（加 ASAN 运行时到 PATH） | **118 passed / 2 skipped / 0 failed**，**无 AddressSanitizer 报错** |
| 那 2 个 skipped | 正是两个越界测试 —— Debug 下 `assert` 生效所以 `GTEST_SKIP()`，**与 TASK-003 的设计决策完全一致** |
| Release 构建（同一配置目录） | 成功 |
| **Release 是否带 Sanitizer** | Debug exe 5,087,744 B / 导入 ASAN DLL = **True**；Release exe 847,360 B / = **False** |

最后一行是本节的关键验收：体积相差 6 倍、且只有 Debug 导入 ASAN 运行时，
机械化地证明了「Release 禁止开启 Sanitizer」而不是靠假设。

### 第三、四节：静态检查与格式化 —— **阻塞**

- [ ] **需用户确认是否安装 LLVM 工具链**（`clang-format` / `clang-tidy` 实测均未安装）
- [ ] 实测当前警告数量，清零后加 `/WX` 与 `-Werror`
- [ ] 新建 `.clang-tidy` 与 `third/.clang-tidy`
- [ ] 全仓 clang-format（必须排在所有功能性修改之后、单独 commit）
- [ ] 建立 `.git-blame-ignore-revs`

> 当前全量构建已是**零警告**（多次实测），所以加 `/WX` 本身风险低；
> 但没有 clang-tidy 就无法做静态分析，也没法执行 `.clang-format`。

### 第五节：文件命名规则例外条款 —— 待做

- [ ] 在 `.qoder/rules/cpp_project_rules.md` 增加 snake_case 例外条款
      （可与第三节一起做，不依赖 LLVM）

## 风险

- **CMake 改造破坏构建**（本任务最大风险）→ 缓解：单独 commit；改前记录基线；
  改后**删除旧 build 缓存**全新配置；出问题直接 `git revert`。
  CMake 错误信息常常晦涩，切勿在损坏状态上继续叠加修改
- **全仓格式化污染历史** → 缓解：严格排在所有功能修改之后，单独 commit +
  `.git-blame-ignore-revs`
- **`/WX` 导致第三方头文件警告变成错误** → 缓解：`sr_warnings` 只链接到本仓目标，
  不传播给 `third_party_impl`；后者保留 `PRIVATE /wd4819`
- **LLVM 工具链未安装则任务无法完成** → 缓解：先向用户确认，未安装则把第三、四节
  标记为「阻塞」并记录原因，不跳过也不伪造验收

## 关联文档

- `docs/architecture.md` §7（构建产物拓扑）
- `docs/troubleshooting.md`「Sanitizer」「MSVC C4819」
- `.qoder/rules/cpp_project_rules.md`「构建系统规范」「工具链规范」「编译规范」

## 变更日志

| 日期 | 变更 |
|------|------|
| 2026-09-05 | 创建任务；实测确认 clang-format / clang-tidy 均未安装，19 个文件缩进违例 |
