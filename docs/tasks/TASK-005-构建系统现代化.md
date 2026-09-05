# TASK-005：构建系统现代化

## 基本信息

- 创建日期：2026-09-05
- 状态：待开始
- 涉及模块：`CMakeLists.txt`、全仓源码（格式化）
- 变更级别：**L**（影响全仓构建 + 引入工具链依赖）
- 前置依赖：TASK-001（必须有版本控制才能安全回滚）、TASK-004（CI 需先能跑通）

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
  缩进了 4 空格**：`arena.h`、`benchmark.h`、`fxaa.h`、`image_write.h`、`log.h`、
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
  `clip.h` 的 `clipTriangleFullFrustum`、`shape2d.h` 的 `sr2d::fillRect`），
  本质上更接近「工具函数文件」而非「类文件」
- 重命名 52 个头文件会破坏所有 `#include` 路径与下游用户代码，收益为零

**执行**：在 `.qoder/rules/cpp_project_rules.md`「文件名」小节增加项目专属例外条款：

> 本项目例外（header-only 库）：`src/` 下所有头文件统一 snake_case，与其包含的
> 主类型名保持词根对应（`framebuffer.h` ↔ `Framebuffer`）。理由：header-only 库的
> 每个头文件同时导出类型与自由函数，PascalCase 文件名会误导使用者以为文件只含单个类；
> 且 `#include "core/framebuffer.h"` 的路径风格与 `stb_image.h`、
> `tiny_obj_loader.h` 等第三方依赖一致。

## 进度清单

- [ ] **向用户确认是否安装 LLVM 工具链**（clang-format / clang-tidy）
- [ ] 记录改造前基线：22 个 exe 清单 + 94 测试结果
- [ ] 合并 `include_directories` 到 `softrenderer` INTERFACE
- [ ] 新建 `sr_warnings` INTERFACE 目标，删除 `add_compile_options`
- [ ] 删除 `link_libraries(softrenderer)`，改用 `foreach` + `target_link_libraries`
- [ ] 全新配置验证：删除 build 缓存 → `cmake -S . -B build` → 全量构建 → `ctest`
- [ ] 比对基线：94 测试全绿、22 个 exe 全部产出
- [ ] 单独 commit（便于出问题时 `git revert`）
- [ ] 新增 `SR_ENABLE_SANITIZERS` 选项 + `sr_sanitizers` INTERFACE 目标
- [ ] 实测当前警告数量，清零后加 `/WX` 与 `-Werror`
- [ ] 新建 `.clang-tidy` 与 `third/.clang-tidy`
- [ ] **（TASK-002/003/004 完成后）** 全仓 clang-format，单独 commit
- [ ] 建立 `.git-blame-ignore-revs`
- [ ] 在 `.qoder/rules/cpp_project_rules.md` 增加文件命名例外条款

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
