# TASK-003：外部输入校验与边界安全

## 基本信息

- 创建日期：2026-09-05
- 完成日期：2026-09-05
- 状态：**已完成**
- 涉及模块：`src/scene/image_loader.h`、`src/core/framebuffer.h`、`src/core/depthbuffer.h`、`src/core/blend.h`、`src/core/log.h`、`src/core/image_write.h`、`src/raster/clip.h`、`src/scene/gltf_types.h`、`src/scene/texture.h`、`tests/`
- 变更级别：**L**（跨模块 + 触及公共接口）

## 目标

消除三类内存安全隐患与规范违例：整数溢出 UB、Release 下越界读、混合精度截断。

## 缺陷一：`image_loader.h` 有符号整数溢出 UB

### 根因

`src/scene/image_loader.h:38`：

```cpp
for (int i = 0; i < w * h; ++i)
```

`w`、`h` 均为 `int`（第 29 行），`w * h` 是**有符号 int 乘法**。
图像尺寸超过 46340² 时溢出 → **未定义行为**。

注意第 37 行的 `resize` 已正确用了 `static_cast<size_t>(w) * h`，
说明作者知道这个坑，但循环处漏了——典型的改一半。

违反：
- `cpp_project_rules.md`「整数运算注意溢出（优先使用 `std::size_t` 处理大小）」
- `cpp_project_rules.md`「外部传入的偏移量、长度必须验证合法性」
- `engineering_rules.md`「外部输入视为不可信：路径、网络数据、用户输入、长度与偏移量均需校验」

### 同文件的次要问题

| 行 | 问题 | 违反 |
|---|---|---|
| 29 | `int w, h, channels;` 声明未初始化 | 「所有变量必须在声明时初始化」 |
| 32-33 | `if (!data) return false;` 静默失败，无法区分「文件打不开」与「格式不支持」 | 「禁止静默失败；关键路径有日志」 |
| 35-37 | `resize` 前未校验 `w > 0 && h > 0` 与尺寸上限，损坏文件可触发巨额分配 | 「外部输入视为不可信」 |
| 12 | 头注释称「实现在 `third/stb/stb_image_impl.cpp`（CMake 编译为 `stb_image` 库）」——**该文件不存在**。实际是 `third_party_impl` 库，源文件为 `third/tinygltf/tiny_gltf_impl.cpp` 等 | 「头注释的准确性直接影响开发效率」「修改文件时若头注释过时须同步更新」 |

## 缺陷二：Release 构建下越界读 UB

`src/core/framebuffer.h` 的读写路径**保护不对称**：

| 方法 | 行 | 保护 |
|---|---|---|
| `setPixel` | 34-39 | ✅ 运行时边界检查，越界静默 return |
| `getPixel` | 41-45 | ❌ 只有 `assert`，Release（`NDEBUG`）下被编译掉 → 越界读 UB |
| `row(y)` | 48-52 | ❌ 只有 `assert` |

`src/core/depthbuffer.h:49` 的 `get(x, y)` **连 assert 都没有**，直接下标访问。

**为什么危险**：`setPixel` 的注释（第 32-33 行）明确说「越界直接忽略——光栅化时
三角形可能部分在屏幕外，这里兜底最简单」，说明越界访问是**预期会发生**的正常情况。
那么读路径同样会越界，却毫无保护。

### 修复方案

**结论**：越界返回哨兵值，保留 `assert` 供 Debug 期发现逻辑错误。

- `Framebuffer::getPixel` 越界 → 返回 `colors::black`
- `DepthBuffer::get` 越界 → 返回 `DepthBuffer::kFar`
- `row(y)` 保持 assert-only，但在头注释补充契约：「Release 下不做检查，
  调用方必须保证 `0 <= y < height()`」。`renderer.h:102-108` 通过 clamp 包围盒
  满足此契约。

**被拒方案**：越界抛异常。拒绝理由：`cpp_project_rules.md` 要求「项目统一选择一种
错误处理策略，禁止混用」，而本项目现有代码全是「返回 bool / 静默兜底」风格，
无异常使用。混入异常会破坏一致性。

**性能考量**：哨兵方案在热路径上引入一次分支。但 `getPixel` 本身不是光栅化热路径
（热路径走 `row(y)` 直写），影响可忽略。

## 缺陷三：`blend.h` alpha 混合截断而非四舍五入

`src/core/blend.h` 四处 `uint8_t(float表达式)` 直接截断小数：

| 行 | 模式 |
|---|---|
| 38-40 | `AlphaBlend` |
| 47-49 | `Additive` |
| 56-58 | `Multiply` |
| 65-67 | `Premultiplied` |

**后果**：每次混合系统性偏暗最多 1/255。`particle_system.h`、`render_queue.h`、
`shape2d.h` 的 `plotBlend` 都依赖此函数，多层叠加会累积可见的暗化偏差。

**修复**：抽出统一辅助函数，四处共用（「禁止重复实现相同逻辑」）：

```cpp
// 为什么用 +0.5f 而非 std::lround：混合是逐像素热路径，lround 会引入函数调用
// 与当前舍入模式依赖；加法在 MSVC/GCC 下都编译为单条指令。
inline uint8_t toUint8Round(float v) noexcept { return uint8_t(v + 0.5f); }
```

⚠️ **实现时必须逐分支确认上界**：`AlphaBlend`（第 37-41 行）当前**没有** `std::min`
钳制（因为 `sa + da == 1` 数学上不超 255），但浮点误差可能让结果达 255.00001 →
`+0.5f` 后 255.5 → 截断 255，仍安全。其余三个分支已有 `std::min(255.0f, ...)`。
每个分支为何需要/不需要钳制，必须写进注释。

### 同文件的其他问题

- **缺 `#include <algorithm>`**：第 41、47-49、56-58、65-67 行用 `std::min`，
  但第 1-3 行只 include `<cstdint>` 与 `"core/color.h"`，依赖传递包含（IWYU 违例）
- **`enum class BlendMode` 无底层类型**（第 17 行），违反「枚举值必须有明确的底层类型」

## 缺陷四：枚举规范违例

| 文件 | 行 | 问题 | 修复 |
|---|---|---|---|
| `core/blend.h` | 17 | `enum class BlendMode` 无底层类型 | `: uint8_t` |
| `scene/gltf_types.h` | 32 | `enum class AlphaMode` 无底层类型 | `: uint8_t` |
| `scene/gltf_types.h` | 114 | `enum class Interpolation` 无底层类型 | `: uint8_t` |
| `scene/texture.h` | 34 | `enum class Filter` 无底层类型 | `: uint8_t` |
| `raster/clip.h` | 89 | **裸 `enum ClipPlane`** | `enum class ClipPlane : uint8_t` |

已合规的对照样例：`core/log.h:56` `enum class LogLevel : int`、
`scene/cubemap.h:19` `enum class CubeFace : int`、
`scene/render_queue.h:25` `enum class RenderLayer : uint16_t`。

⚠️ 改 `ClipPlane` 为 `enum class` 需同步更新所有引用点（`clip.h` 内部的
`kNear`/`kFar`/`kLeft`/`kRight`/`kBottom`/`kTop`/`kClipPlaneCount`，以及
`renderer.h` 中的使用）。**必须先 grep 全部引用点确认无遗漏**。

这属公共接口变更。本项目尚未有外部使用者，可直接改，但需在 `docs/changelog.md` 记录。

## 缺陷五（**实施中新发现**）：`core/log.h` 内置模式的三个缺陷

给 `image_loader.h` 加失败日志时，全量构建报出 **2 个 C4840**，顺藤摸出三个缺陷。

| # | 缺陷 | 证据 |
|---|---|---|
| 1 | `sr::Log::log` 是真正的 C 可变参函数（`const char* fmt, ...` + `va_list` + `vfprintf`），传 `std::string` 等非 POD 类型经 `...` 是**未定义行为** | MSVC C4840，`image_loader.h:82, 93` |
| 2 | 格式串按 printf 语义解析，但约定写的是 `{}` → **占位符从不被替换**，参数静默丢失 | 阅读 `vfprintf` 路径 |
| 3 | 日志文本里的字面 `%` 被 `vfprintf` 当转换符解析 → UB | 同上 |

**为何缺陷 2 比缺陷 1 更严重**：`unit_tests` 与 20 个示例都**未**调
`sr_enable_logging()`，所以走内置模式 —— 它不是罕见兜底，而是**常用路径**。
也就是说默认构建下所有带参日志一直是无效的。

**修复**：内置模式改为变参模板 + `std::ostringstream`，三个缺陷全部消除：

- 新增 `sr::formatLog(fmt, args...)`：按顺序替换 `{}`。**独立成函数而不藏在
  `Log::log` 里，目的是可测试**（`engineering_rules.md`「代码必须保留可测试入口」）——
  否则验证它得捕获 stderr，在 Windows 上既繁琐又不稳定
- 消息先格式化好再用**单个 `%s`** 输出 → 字面 `%` 安全（消除缺陷 3）
- `levelName()` 用 `switch` 替代旧的常量数组：**旧数组漏了 `Off`**（枚举 7 个值、
  数组只 6 项），传 `LogLevel::Off` 会越界读 —— 这是顺带发现的**第四个缺陷**
- `src/` 仍零第三方依赖（只用 `<sstream>`）

**连带修正**：`image_loader.h` 的 `.c_str()` 变通不再必要，恢复直传 `std::string`；
`engineering_rules.md` 的日志红线从「只准传 POD」改为「禁止改回 C 可变参」；
`docs/architecture.md` §6.2 重写。

**被拒方案**：删掉内置兜底、`src/` 直接用 `mylog`。拒绝理由：会把 spdlog 从
「可选依赖」变成 `src/` 的硬依赖，`SR_ENABLE_SPDLOG=OFF` 不再可用，
违背 `architecture.md` §1 记录的「渲染器本体零依赖」原则。

## 缺陷六（顺带发现）：两处过时头注释声称不存在的文件

| 文件 | 旧注释声称 | 实际 |
|---|---|---|
| `scene/image_loader.h:12` | 实现在 `third/stb/stb_image_impl.cpp`（`stb_image` 库） | **该文件不存在** |
| `core/image_write.h:16` | 实现在 `third/stb/stb_image_write_impl.cpp` | **该文件不存在** |

直接读三个 `.cpp` 后确认的真实分工（**与文件名直觉相反**）：

| 实现单元 | `#define` | 提供的符号 |
|---|---|---|
| `third/tinygltf/tiny_gltf_impl.cpp` | `STB_IMAGE_IMPLEMENTATION`、`STB_IMAGE_WRITE_IMPLEMENTATION` | **stb_image + stb_image_write** |
| `src/scene/gltf_loader_impl.cpp` | `TINYGLTF_IMPLEMENTATION` | **tinygltf** |
| `third/tiny_obj_loader_impl.cpp` | `TINYOBJLOADER_IMPLEMENTATION` | **tinyobjloader** |

> ⚠️ `tiny_gltf_impl.cpp` 位于 `tinygltf/` 目录、名字也叫 `tiny_gltf_impl`，
> 但**完全不含 tinygltf 的实现**。
>
> **教训**：根 `CMakeLists.txt:22-24` 原本的注释也声称「tinygltf 内部会 define
> STB_IMAGE_IMPLEMENTATION，所以一个 .cpp 同时提供 stb + tinygltf 的所有符号」。
> 我最初**轻信了这条注释**并把它写进了 `third/README.md` 与 `docs/architecture.md`，
> 直到读了 `.cpp` 才发现是错的。已全部修正。
> **结论：涉及第三方实现归属时，必须读 `.cpp` 源文件，不能依赖文件名或既有注释推断。**

## 缺陷七（顺带发现）：`gltf_types.h` 成员未初始化

`GltfAnimation::Channel::path` 无默认值，而 `gltf_loader.h:327-332` 只在
`target_path` 等于 `"translation"` / `"rotation"` / `"scale"` 三者之一时才赋值它。
glTF 2.0 还允许 `"weights"`（形变目标）与扩展自定义值，遇到这些值时
三个分支全不命中 → `path` 是**不确定值**，读取即 UB。

同文件的 `samplerIndex` / `targetNode` / `input` / `output` 也无默认值，
但 `gltf_loader.h` 当前对它们都做了无条件赋值，属**防御性修复**而非活跃缺陷。

统一用 `-1` 作「未设置」哨兵：`0` 是合法数组下标，用它做哨兵会静默指向首元素；
`-1` 能被下游校验识别（`animation_player.h:47` 已有
`if (channel.samplerIndex < 0 || ... >= size) return;`），且与同文件
`GltfTexture::imageIndex = -1` 已有约定一致。

## 进度清单

- [x] `image_loader.h:29` 改为 `int w = 0, h = 0, channels = 0;`
- [x] `image_loader.h` 新增尺寸校验 + 具名常量 `kMaxImageDimension = 16384`
- [x] `image_loader.h:38` 循环改用 `size_t`（消除有符号溢出）
- [x] `image_loader.h` 失败路径加日志（区分「无法解码」与「尺寸非法」）
- [x] `image_loader.h` 新增 **RAII 释放**（`unique_ptr` + 自定义删除器）——
      新增的校验分支若提前 return 会泄露整个图像缓冲
- [x] `image_loader.h:5-13` 修正过时的头注释
- [x] `framebuffer.h::getPixel` 越界返回 `colors::black`
- [x] `depthbuffer.h::get` 越界返回 `kFar`
- [x] `framebuffer.h::row` / `depthbuffer.h::row` 头注释补充调用契约
- [x] `blend.h` 新增 `toUint8Round`，四处替换截断
- [x] `blend.h` 补 `#include <algorithm>`
- [x] `blend.h` / `gltf_types.h` / `texture.h` 四个枚举补底层类型
- [x] `clip.h` 裸 `enum ClipPlane` → `enum class ClipPlane : uint8_t`，
      grep 确认全部引用点已更新（实测仅 `clip.h` 内部使用）
- [x] `clip.h` 新增 `static_assert` 防止 `kClipPlaneCount` 与枚举项数漂移
- [x] `core/log.h` 内置模式改为变参模板（消除三个 UB / 失效缺陷）
- [x] `core/log.h` `levelName()` 用 switch 替代漏了 `Off` 的常量数组
- [x] `core/image_write.h:16` 修正过时头注释
- [x] `gltf_types.h` 五个成员补默认值
- [x] `CMakeLists.txt:22-24` 修正过时注释
- [x] `third/README.md` / `docs/architecture.md` 修正我先前写错的实现单元归属
- [x] 新增异常测试：`loadImage_missingFile_returnsFalse`、`loadImage_emptyPath_returnsFalse`
- [x] 新增往返测试 `loadImage_roundTripPreservesPixelsAndOrientation`
      （写系统临时目录而非仓库，避开 fixture 二进制；`loadImage` 此前全仓零调用方，
      这是它**第一次被真正执行**）
- [x] 新增边界测试：`framebuffer_getPixel_outOfBounds_returnsSentinel`、
      `depthBuffer_get_outOfBounds_returnsFar`
- [x] 新增混合测试：`toUint8Round_roundsAndClamps`、
      `blendPixels_alphaBlendRoundsInsteadOfTruncating`、`blendPixels_allModesNeverWrapAround`
- [x] 新增 `Log` 测试套件（8 个用例，直接断言 `formatLog` 返回值）
- [x] 全量构建**零错误零警告**（修前 2 个 C4840）
- [x] 单元测试 97 → **113 全绿**（7 ms，无 SKIPPED）
- [x] 20 个离屏示例全部正常运行（混合精度改动会影响所有输出图像，必须验证）

### 越界测试的 Debug/Release 策略（计划中要求「必须明确决定，不能留模糊」）

**决定：用 `#ifndef NDEBUG` + `GTEST_SKIP()`。**

Debug 下 `assert` 会先中止进程，所以哨兵行为只在 Release（`NDEBUG`）可测。
选择 `GTEST_SKIP()` 而非「给 `unit_tests` 单独定义 `NDEBUG`」的理由：
后者会让**全部** 113 个测试失去 assert 保护，为了测两个边界用例而削弱整体断言能力，
得不偿失。`GTEST_SKIP()` 在测试输出里显式标记为 skipped，不会伪装成通过。

实测：Release 构建下 `NDEBUG` 已定义（CMake 的 `CMAKE_CXX_FLAGS_RELEASE` 自带），
两个用例正常执行，**无 SKIPPED 输出**。

## 风险

- **越界测试在 Debug 下先触发 assert** → 必须在实现时明确决定策略，不能留模糊：
  要么用 `EXPECT_DEATH` 测 Debug 行为、普通断言测 Release 行为；
  要么给 `unit_tests` 目标单独定义 `NDEBUG`。
  **推荐后者**：测试的是「Release 下不 UB」这个契约，用 `NDEBUG` 编译测试目标
  最贴近真实场景，且避免 death test 在 Windows 上的不稳定行为。
- **`toUint8Round` 改变现有混合结果 → 像素回归基准全部失效** → 缓解：本任务
  必须在 TASK-004 的像素回归接线**之前**完成，或接线后立即重新生成基准图并在
  commit 信息中说明原因
- **`ClipPlane` 改 `enum class` 漏改引用点导致编译失败** → 缓解：编译器会全部报错，
  属于安全失败；但仍需先 grep 评估影响面

## 关联文档

- `docs/modules.md`「src/core/」「src/scene/」
- `docs/architecture.md` §3（`core/` 的边界检查契约）
- `.qoder/rules/cpp_project_rules.md`「C++ 安全规范」「枚举规范」「初始化规范」

## 变更日志

| 日期 | 变更 |
|------|------|
| 2026-09-05 | 创建任务；完成四类缺陷的根因分析与修复方案设计 |
