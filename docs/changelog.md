# 变更日志

> 格式：日期 | 版本 | 变更内容 | 影响范围
>
> 按 `documentation_rules.md` 要求，每次有意义的变更都必须在此登记。
> 重大接口变更需同时在变更内容中标注版本号。

## 2026-09-05

### 外部输入校验与边界安全（TASK-003，已完成）

单元测试 97 → **113**，全量构建**零错误零警告**（修前有 2 个 C4840），
20 个离屏示例全部正常。

#### 计划内修复

| 变更 | 影响范围 |
|---|---|
| **修复** `image_loader.h` 有符号整数溢出 UB：`for (int i = 0; i < w * h; ++i)` → `size_t` | `src/scene/image_loader.h` |
| 新增尺寸校验与具名常量 `kMaxImageDimension = 16384`；变量声明即初始化 | 同上 |
| 新增 **RAII 释放**（`unique_ptr` + 自定义删除器）——新增的校验分支提前 return 会泄露图像缓冲 | 同上 |
| **修复** `Framebuffer::getPixel` 在 Release 下越界读 UB：越界返回 `colors::black` | `src/core/framebuffer.h` |
| **修复** `DepthBuffer::get` 越界读 UB（旧版连 `assert` 都没有）：返回 `kFar` | `src/core/depthbuffer.h` |
| **修复** `blend.h` alpha 混合截断而非四舍五入（每次混合系统性偏暗 ≤ 1/255）；新增 `toUint8Round` 统一钳制+取整 | `src/core/blend.h` |
| `blend.h` 补 `#include <algorithm>`（原依赖传递包含） | 同上 |
| 5 个枚举合规：`ClipPlane` 裸 enum → `enum class : uint8_t`；`BlendMode`/`AlphaMode`/`Path`/`Interpolation`/`Filter` 补底层类型 | `clip.h`、`blend.h`、`gltf_types.h`、`texture.h` |
| `clip.h` 新增 `static_assert` 防止 `kClipPlaneCount` 与枚举项数漂移 | `src/raster/clip.h` |

#### 实施中新发现并修复

| 变更 | 发现方式 |
|---|---|
| **`core/log.h` 内置模式重写为变参模板**，消除 4 个缺陷：① 非 POD 类型经 C 可变参传递是 UB；② `{}` 占位符**从不被替换**（格式串按 printf 解析）；③ 日志文本里的字面 `%` 被 `vfprintf` 当转换符解析 → UB；④ `levelName` 的常量数组**漏了 `Off`**（枚举 7 值 / 数组 6 项）→ 越界读 | 给 `image_loader.h` 加日志时构建报出 2 个 **C4840** |
| 新增可测试入口 `sr::formatLog`（从 `Log::log` 拆出），使格式化逻辑可直接断言而无需捕获 stderr | `engineering_rules.md`「代码必须保留可测试入口」 |
| **`gltf_types.h` 的 `Channel::path` 补默认值**：`gltf_loader.h:327-332` 只在 `target_path` 为三个已知值时赋值，而 glTF 2.0 允许 `"weights"` 与扩展值 → 旧代码是不确定值，读取即 UB。另 4 个 int 字段做防御性初始化（统一 `-1` 哨兵） | 改枚举时通读同文件 |
| **三处注释声称不存在的文件**：`image_loader.h:12`（`third/stb/stb_image_impl.cpp`）、`image_write.h:16`（`third/stb/stb_image_write_impl.cpp`）、`CMakeLists.txt:22-24`（「一个 .cpp 同时提供 stb + tinygltf」）均已修正 | 按「修改文件时若头注释过时须同步更新」核查 |

#### 第三方实现单元归属的真相（与文件名直觉相反）

| 实现单元 | `#define` | 提供的符号 |
|---|---|---|
| `third/tinygltf/tiny_gltf_impl.cpp` | `STB_IMAGE_IMPLEMENTATION`、`STB_IMAGE_WRITE_IMPLEMENTATION` | **stb_image + stb_image_write**（不含 tinygltf） |
| `src/scene/gltf_loader_impl.cpp` | `TINYGLTF_IMPLEMENTATION` | **tinygltf** |
| `third/tiny_obj_loader_impl.cpp` | `TINYOBJLOADER_IMPLEMENTATION` | **tinyobjloader** |

> 教训：本次先轻信了 `CMakeLists.txt` 的旧注释并写进了 `third/README.md` 与
> `docs/architecture.md`，读了 `.cpp` 后才发现错误，已全部修正。
> **结论：涉及第三方实现归属时必须读源文件，不能依赖文件名或既有注释推断。**

#### 新增测试（16 个）

| 套件 | 用例 |
|---|---|
| `Framebuffer`（新） | `framebuffer_getPixel_outOfBounds_returnsSentinel` |
| `DepthBuffer` | `depthBuffer_get_outOfBounds_returnsFar` |
| `Blend` | `toUint8Round_roundsAndClamps`、`blendPixels_alphaBlendRoundsInsteadOfTruncating`、`blendPixels_allModesNeverWrapAround` |
| `ImageLoader`（新） | `loadImage_missingFile_returnsFalse`、`loadImage_emptyPath_returnsFalse`、`loadImage_roundTripPreservesPixelsAndOrientation` |
| `Log`（新） | `formatLog_noArgs_returnsFormatString`、`formatLog_substitutesBracesInOrder`、`formatLog_acceptsNonPodTypes`、`formatLog_extraArgsAreAppendedNotDropped`、`formatLog_extraBracesAreKeptLiteral`、`formatLog_literalPercentIsSafe`、`levelName_coversAllEnumValues`、`setLevel_roundTrips` |

> `loadImage` 此前**全仓零调用方**，往返测试是它第一次被真正执行。
> 往返测试写系统临时目录而非仓库，避开提交二进制 fixture。

#### 公共接口影响

| 变更 | 兼容性 |
|---|---|
| `enum ClipPlane` → `enum class ClipPlane : uint8_t` | **API Break**，但实测仅 `clip.h` 内部使用，无外部引用点 |
| `kClipPlaneCount` 由枚举项改为 `static constexpr int` | 同上，仅内部使用 |
| `BlendMode` / `AlphaMode` / `Path` / `Interpolation` / `Filter` 补底层类型 | 兼容（枚举值与用法不变） |
| `sr::Log::log` 由 C 可变参改为变参模板 | 兼容（调用语法不变，且现在接受更多类型） |
| `blendPixels` 结果可能 **+1** | **行为变更**：四舍五入代替截断。影响所有使用 alpha 混合的输出图像 |
| `sr::formatLog`、`sr::levelName`、`toUint8Round` | 新增公共符号（均在全局或 `namespace sr`） |

> ⚠️ `blendPixels` 的行为变更会使**未来的像素回归基准图**与修复前不同。
> TASK-004 接线像素回归时必须以**修复后**的输出为基准。

---

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
