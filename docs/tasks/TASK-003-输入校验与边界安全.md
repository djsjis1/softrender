# TASK-003：外部输入校验与边界安全

## 基本信息

- 创建日期：2026-09-05
- 状态：待开始
- 涉及模块：`src/scene/image_loader.h`、`src/core/framebuffer.h`、`src/core/depthbuffer.h`、`src/core/blend.h`、`src/raster/clip.h`、`src/scene/gltf_types.h`、`src/scene/texture.h`、`tests/`
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

## 进度清单

- [ ] `image_loader.h:29` 改为 `int w = 0, h = 0, channels = 0;`
- [ ] `image_loader.h` 新增尺寸校验 + 具名常量 `kMaxImageDimension`
- [ ] `image_loader.h:38` 循环改用 `size_t`（消除有符号溢出）
- [ ] `image_loader.h` 失败路径加日志（区分「打不开」与「尺寸非法」）
- [ ] `image_loader.h:5-13` 修正过时的头注释
- [ ] `framebuffer.h::getPixel` 越界返回 `colors::black`
- [ ] `depthbuffer.h::get` 越界返回 `kFar`
- [ ] `framebuffer.h::row` 头注释补充调用契约
- [ ] `blend.h` 新增 `toUint8Round`，四处替换截断
- [ ] `blend.h` 补 `#include <algorithm>`
- [ ] 五个枚举补底层类型 / 改 `enum class`
- [ ] grep 确认 `ClipPlane` 全部引用点已更新
- [ ] 新增异常测试：`loadImage_missingFile_returnsFalse`、
      `loadImage_emptyPath_returnsFalse`、`loadImage_validPng_dimensionsMatchFile`
- [ ] 新增 `tests/fixtures/` 与一个极小合法 PNG
- [ ] 新增边界测试：`framebuffer_getPixel_outOfBounds_returnsSentinel`、
      `depthBuffer_get_outOfBounds_returnsFar`
- [ ] 全量构建 + 94 测试通过

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
